/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Notification-backed k_sem: the count, limit, and waiter list live in
 * the caller-owned struct k_sem and are only ever touched under its
 * lock by THIS file; blocking and wakeup ride FreeRTOS direct-to-task
 * notifications, whose state lives inside the TCB (kernel-owned).
 * Nothing of the caller's memory ever enters a kernel list, which
 * structurally enforces the object-lifetime principle (zkernel README;
 * issues #21/#22/#40): when k_sem_take returns, the kernel holds zero
 * references into the caller's storage.
 */

#include "zephyr/kernel.h"

#include <errno.h>

#include "esp_attr.h"
#include "sdkconfig.h"

#include "zkernel_internal.h"

#if configTASK_NOTIFICATION_ARRAY_ENTRIES < 2
#error "Boreas zkernel reserves task-notification index 1 for blocking primitives (index 0 stays free for ESP-IDF internals such as esp_ipc/pthread/eth). Set CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES=2 (or higher) in sdkconfig."
#endif

/* Reserved for zkernel: index 0 is used by ESP-IDF internals. */
#define Z_SEM_NOTIFY_INDEX 1

struct z_sem_waiter {
	sys_dnode_t node;
	TaskHandle_t task;
	UBaseType_t prio; /* cached at enqueue (no FreeRTOS calls under the sem lock) */
	bool woken;       /* a give/reset targeted this waiter; set under the lock */
	bool reset;       /* woken by k_sem_reset -> take returns -EAGAIN */
};

int k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit)
{
	if (limit == 0 || initial_count > limit) {
		return -EINVAL; /* matches upstream */
	}

	sem->count = initial_count;
	sem->limit = limit;
	sys_dlist_init(&sem->waiters);
	sem->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
	return 0;
}

/* Pop the wake target: highest cached priority, FIFO among equals
 * (upstream wakes the highest-priority waiter). Caller holds the lock.
 * Plain code over the caller-owned list -- no FreeRTOS calls. */
static struct z_sem_waiter *z_sem_pop_waiter(struct k_sem *sem)
{
	struct z_sem_waiter *best = NULL;
	sys_dnode_t *n;

	for (n = sys_dlist_peek_head(&sem->waiters); n != NULL;
	     n = sys_dlist_peek_next(&sem->waiters, n)) {
		struct z_sem_waiter *w = CONTAINER_OF(n, struct z_sem_waiter, node);

		if (best == NULL || w->prio > best->prio) {
			best = w;
		}
	}
	if (best != NULL) {
		sys_dlist_remove(&best->node);
		best->woken = true;
	}
	return best;
}

int k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	/* The waiter node lives on THIS stack frame. It is only ever
	 * linked while we are inside this function, and it is unlinked
	 * (by the giver or by our timeout path) under the sem lock before
	 * we return -- synchronous severance by construction. */
	struct z_sem_waiter w = {0};

	/* Task identity is sampled OUTSIDE the lock (uxTaskPriorityGet
	 * enters FreeRTOS's own critical section -- no FreeRTOS calls
	 * happen under the sem lock), and ONLY on the must-block path:
	 * the fast paths must work before the scheduler starts (NULL
	 * current task), which is what makes K_SEM_DEFINE usable from
	 * constructors. Unlock-sample-relock requires re-checking the
	 * count, hence the loop. */
	for (;;) {
		z_kernel_lock(&sem->lock);

		if (sem->count > 0) {
			sem->count--;
			z_kernel_unlock(&sem->lock);
			return 0;
		}

		if (k_timeout_is_no_wait(timeout)) {
			z_kernel_unlock(&sem->lock);
			return -EBUSY;
		}

		if (w.task != NULL) {
			break; /* sampled on a previous pass; enqueue under THIS lock */
		}

		z_kernel_unlock(&sem->lock);
		w.task = xTaskGetCurrentTaskHandle();
		w.prio = uxTaskPriorityGet(NULL);
	}

	sys_dlist_append(&sem->waiters, &w.node);
	z_kernel_unlock(&sem->lock);

	bool forever = k_timeout_is_forever(timeout);
	TickType_t total = forever ? 0 : k_timeout_to_ticks(timeout);
	TickType_t start = xTaskGetTickCount();

	for (;;) {
		TickType_t wait = portMAX_DELAY;

		if (!forever) {
			/* Unsigned tick diff is wraparound-safe. */
			TickType_t elapsed = xTaskGetTickCount() - start;

			wait = (elapsed >= total) ? 0 : (total - elapsed);
		}

		uint32_t got = ulTaskNotifyTakeIndexed(Z_SEM_NOTIFY_INDEX, pdTRUE, wait);

		z_kernel_lock(&sem->lock);
		if (w.woken) {
			/* A giver/reset unlinked us. If our wait timed out in
			 * the same instant (got == 0), its notification is
			 * still in flight -- consume it before returning so
			 * it cannot poison a later take on this task (the
			 * consume-the-in-flight-give pattern from the k_work
			 * sync slot). */
			bool reset = w.reset;

			z_kernel_unlock(&sem->lock);
			if (got == 0) {
				(void)ulTaskNotifyTakeIndexed(Z_SEM_NOTIFY_INDEX, pdTRUE,
							      portMAX_DELAY);
			}
			return reset ? -EAGAIN : 0;
		}

		if (got == 0 && !forever) {
			/* Timed out, not targeted: unlink ourselves. After
			 * this unlock no giver can see the node. */
			sys_dlist_remove(&w.node);
			z_kernel_unlock(&sem->lock);
			return -EAGAIN;
		}

		/* Spurious wake (stale notification from code misusing the
		 * reserved index): loop; the wait recompute above absorbs
		 * the elapsed time. */
		z_kernel_unlock(&sem->lock);
	}
}

void K_ISR_SAFE k_sem_give(struct k_sem *sem)
{
	TaskHandle_t to_wake = NULL;

	z_kernel_lock(&sem->lock);
	struct z_sem_waiter *w = z_sem_pop_waiter(sem);

	if (w != NULL) {
		/* Copy the handle under the lock; never touch the waiter's
		 * stack-resident node after unlock. The waiter cannot return
		 * from k_sem_take until our notification lands (woken==true
		 * forces it into the consume path), so the handle stays
		 * valid. */
		to_wake = w->task;
	} else if (sem->count < sem->limit) {
		sem->count++;
	}
	z_kernel_unlock(&sem->lock);

	if (to_wake != NULL) {
		if (xPortInIsrContext()) {
			BaseType_t woken = pdFALSE;

			vTaskNotifyGiveIndexedFromISR(to_wake, Z_SEM_NOTIFY_INDEX, &woken);
			portYIELD_FROM_ISR(woken);
		} else {
			xTaskNotifyGiveIndexed(to_wake, Z_SEM_NOTIFY_INDEX);
		}
	}
}

void k_sem_reset(struct k_sem *sem)
{
	/* Upstream semantics: zero the count and wake all waiters, whose
	 * k_sem_take calls return -EAGAIN. (The FreeRTOS-backed
	 * implementation could only drain the count.) */
	for (;;) {
		TaskHandle_t to_wake = NULL;

		z_kernel_lock(&sem->lock);
		struct z_sem_waiter *w = z_sem_pop_waiter(sem);

		if (w != NULL) {
			w->reset = true;
			to_wake = w->task;
		} else {
			sem->count = 0;
		}
		z_kernel_unlock(&sem->lock);

		if (to_wake == NULL) {
			return;
		}
		xTaskNotifyGiveIndexed(to_wake, Z_SEM_NOTIFY_INDEX);
	}
}

unsigned int k_sem_count_get(struct k_sem *sem)
{
	return __atomic_load_n(&sem->count, __ATOMIC_RELAXED);
}
