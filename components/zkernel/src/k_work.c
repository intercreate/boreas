/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * k_work backed by sys_dlist_t + counting k_sem (no FreeRTOS xQueue).
 * Cancel is O(1) actually-removable via sys_dlist_remove.
 */

#include "zephyr/kernel.h"
#include "sdkconfig.h"

#include <errno.h>
#include "esp_attr.h"

/* Internal queue flag (k_work_q.flags). Not exposed in the public
 * header because it's an implementation detail. */
#define Z_WORK_QUEUE_STARTED BIT(0)

/* ----------------------------------------------------------------
 * System Work Queue
 *
 * Stack lives outside the queue struct (matches upstream pattern).
 * Auto-initialized before main() via constructor.
 * ---------------------------------------------------------------- */

#ifndef CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE
#define CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE 4096
#endif
#ifndef CONFIG_SYSTEM_WORKQUEUE_PRIORITY
#define CONFIG_SYSTEM_WORKQUEUE_PRIORITY 5
#endif

static StackType_t sys_wq_stack[CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE / sizeof(StackType_t)];
struct k_work_q k_sys_work_q;

/* Branch-once helpers around portENTER/EXIT_CRITICAL: ESP-IDF needs
 * different macros for ISR vs task context. Inlined to avoid runtime
 * overhead in the common (task) case. */
static ALWAYS_INLINE void z_work_lock(struct k_work_q *queue)
{
	if (xPortInIsrContext()) {
		portENTER_CRITICAL_ISR(&queue->lock);
	} else {
		portENTER_CRITICAL(&queue->lock);
	}
}

static ALWAYS_INLINE void z_work_unlock(struct k_work_q *queue)
{
	if (xPortInIsrContext()) {
		portEXIT_CRITICAL_ISR(&queue->lock);
	} else {
		portEXIT_CRITICAL(&queue->lock);
	}
}

/* ----------------------------------------------------------------
 * Work Queue Thread
 * ---------------------------------------------------------------- */

static void k_work_queue_thread(void *p1)
{
	struct k_work_q *queue = (struct k_work_q *)p1;

	for (;;) {
		k_sem_take(&queue->sem, K_FOREVER);

		/* Pop one item under lock. Spurious wakeups (cancelled item
		 * gave the sem before being removed) yield NULL -- just loop. */
		struct k_work *work = NULL;
		portENTER_CRITICAL(&queue->lock);
		sys_dnode_t *node = sys_dlist_get(&queue->pending);
		if (node != NULL) {
			work = CONTAINER_OF(node, struct k_work, node);
			__atomic_or_fetch(&work->flags, K_WORK_RUNNING, __ATOMIC_RELAXED);
			__atomic_and_fetch(&work->flags, ~K_WORK_QUEUED, __ATOMIC_RELAXED);
			__atomic_store_n(&work->queue, NULL, __ATOMIC_RELEASE);
		}
		portEXIT_CRITICAL(&queue->lock);

		if (work == NULL || work->handler == NULL) {
			continue;
		}

		work->handler(work);

		__atomic_and_fetch(&work->flags, ~K_WORK_RUNNING, __ATOMIC_RELAXED);

		/* Signal any flush/cancel_sync waiter */
		struct k_work_sync *sync = __atomic_load_n(&work->sync, __ATOMIC_ACQUIRE);
		if (sync) {
			__atomic_store_n(&work->sync, NULL, __ATOMIC_RELEASE);
			k_sem_give(&sync->sem);
		}

		if (!queue->no_yield) {
			taskYIELD();
		}
	}
}

/* ----------------------------------------------------------------
 * Work
 * ---------------------------------------------------------------- */

void k_work_init(struct k_work *work, k_work_handler_t handler)
{
	work->handler = handler;
	work->flags = 0;
	work->queue = NULL;
	work->sync = NULL;
	work->node.next = NULL;
	work->node.prev = NULL;
}

static int K_ISR_SAFE k_work_submit_internal(struct k_work_q *queue, struct k_work *work)
{
	z_work_lock(queue);

	if (__atomic_load_n(&work->flags, __ATOMIC_RELAXED) & K_WORK_QUEUED) {
		/* Already queued -- idempotent */
		z_work_unlock(queue);
		return 1;
	}

	__atomic_or_fetch(&work->flags, K_WORK_QUEUED, __ATOMIC_RELAXED);
	__atomic_store_n(&work->queue, queue, __ATOMIC_RELEASE);
	sys_dlist_append(&queue->pending, &work->node);

	z_work_unlock(queue);

	/* k_sem_give is already ISR-safe (uses xSemaphoreGiveFromISR). */
	k_sem_give(&queue->sem);
	return 0;
}

int K_ISR_SAFE k_work_submit(struct k_work *work)
{
	if (!(__atomic_load_n(&k_sys_work_q.flags, __ATOMIC_RELAXED) & Z_WORK_QUEUE_STARTED)) {
		return -EINVAL;
	}
	return k_work_submit_internal(&k_sys_work_q, work);
}

int K_ISR_SAFE k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work)
{
	if (!(__atomic_load_n(&queue->flags, __ATOMIC_RELAXED) & Z_WORK_QUEUE_STARTED)) {
		return -EINVAL;
	}
	return k_work_submit_internal(queue, work);
}

bool k_work_cancel(struct k_work *work)
{
	if (__atomic_load_n(&work->flags, __ATOMIC_RELAXED) & K_WORK_RUNNING) {
		return false; /* Can't cancel while running */
	}

	struct k_work_q *queue = __atomic_load_n(&work->queue, __ATOMIC_ACQUIRE);
	if (queue == NULL) {
		/* Not queued -- idempotent success */
		return true;
	}

	struct k_work_sync *sync_to_release = NULL;

	z_work_lock(queue);

	/* Re-validate the queue under lock: between our read of work->queue
	 * and acquiring this queue's lock, the worker may have popped the
	 * item AND another thread may have re-submitted it to a *different*
	 * queue. We hold the wrong lock for that case; bail rather than
	 * remove from a list we don't own. Cancel is best-effort. */
	if (__atomic_load_n(&work->queue, __ATOMIC_RELAXED) != queue) {
		z_work_unlock(queue);
		return true;
	}

	if (__atomic_load_n(&work->flags, __ATOMIC_RELAXED) & K_WORK_QUEUED) {
		sys_dlist_remove(&work->node);
		__atomic_and_fetch(&work->flags, ~K_WORK_QUEUED, __ATOMIC_RELAXED);
		__atomic_store_n(&work->queue, NULL, __ATOMIC_RELEASE);

		/* If a flush waiter (k_work_flush / k_work_cancel_sync) is
		 * attached, release it here -- the cancelled item will never
		 * run to completion on a worker thread, so the worker won't
		 * give the sem. Atomic exchange so only one path consumes it
		 * (defensive against a concurrent worker giving for a stale
		 * sync pointer). k_sem_give itself is hoisted outside the
		 * critical section to avoid calling FreeRTOS API from
		 * portENTER_CRITICAL. */
		sync_to_release = __atomic_exchange_n(&work->sync, NULL, __ATOMIC_ACQ_REL);
	}

	z_work_unlock(queue);

	if (sync_to_release != NULL) {
		k_sem_give(&sync_to_release->sem);
	}

	/* The sem give that submit issued for this item will cause one
	 * spurious worker wakeup; it pops a NULL and loops. Harmless. */
	return true;
}

bool k_work_is_pending(struct k_work *work)
{
	return (__atomic_load_n(&work->flags, __ATOMIC_RELAXED) &
		(K_WORK_QUEUED | K_WORK_RUNNING)) != 0;
}

uint32_t k_work_busy_get(const struct k_work *work)
{
	return __atomic_load_n(&work->flags, __ATOMIC_RELAXED);
}

int k_work_flush(struct k_work *work, struct k_work_sync *sync)
{
	/* Set up sync BEFORE checking state to avoid TOCTOU: if work
	 * finishes between check and sem_take, the worker will have
	 * already signaled the sem. */
	k_sem_init(&sync->sem, 0, 1);
	__atomic_store_n(&work->sync, sync, __ATOMIC_RELEASE);

	if (!k_work_is_pending(work)) {
		__atomic_store_n(&work->sync, NULL, __ATOMIC_RELEASE);
		return 0;
	}

	return k_sem_take(&sync->sem, K_FOREVER);
}

int k_work_cancel_sync(struct k_work *work, struct k_work_sync *sync)
{
	k_sem_init(&sync->sem, 0, 1);
	__atomic_store_n(&work->sync, sync, __ATOMIC_RELEASE);
	__atomic_or_fetch(&work->flags, K_WORK_CANCELING, __ATOMIC_RELAXED);

	k_work_cancel(work);

	if (__atomic_load_n(&work->flags, __ATOMIC_RELAXED) & K_WORK_RUNNING) {
		int ret = k_sem_take(&sync->sem, K_FOREVER);
		__atomic_and_fetch(&work->flags, ~K_WORK_CANCELING, __ATOMIC_RELAXED);
		return ret;
	}

	__atomic_store_n(&work->sync, NULL, __ATOMIC_RELEASE);
	__atomic_and_fetch(&work->flags, ~K_WORK_CANCELING, __ATOMIC_RELAXED);
	return 0;
}

/* ----------------------------------------------------------------
 * Work Queue Lifecycle
 * ---------------------------------------------------------------- */

void k_work_queue_start(struct k_work_q *queue, k_thread_stack_t *stack, size_t stack_size,
			int prio, const struct k_work_queue_config *cfg)
{
	/* Idempotent: no-op if already started */
	if (__atomic_load_n(&queue->flags, __ATOMIC_RELAXED) & Z_WORK_QUEUE_STARTED) {
		return;
	}

	sys_dlist_init(&queue->pending);
	k_sem_init(&queue->sem, 0, INT32_MAX);
	queue->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
	queue->name = cfg ? cfg->name : NULL;
	queue->no_yield = cfg ? cfg->no_yield : false;

	queue->thread = xTaskCreateStatic(
		k_work_queue_thread, queue->name ? queue->name : "k_work_q",
		stack_size / sizeof(StackType_t), queue, prio, stack, &queue->tcb);
	__ASSERT(queue->thread != NULL, "k_work_queue_start: xTaskCreateStatic failed");

	__atomic_or_fetch(&queue->flags, Z_WORK_QUEUE_STARTED, __ATOMIC_RELEASE);
}

/* ----------------------------------------------------------------
 * Drain
 * ---------------------------------------------------------------- */

struct z_work_drain_sentinel {
	struct k_work work;
	struct k_sem sem;
};

static void z_work_drain_handler(struct k_work *work)
{
	struct z_work_drain_sentinel *s = CONTAINER_OF(work, struct z_work_drain_sentinel, work);
	k_sem_give(&s->sem);
}

int k_work_queue_drain(struct k_work_q *queue, bool plug)
{
	(void)plug; /* Boreas does not implement plugging; reserved for upstream parity. */

	/* Sentinel is stack-allocated -- safe because xSemaphoreGive does
	 * not access the sem after returning, so by the time k_sem_take
	 * wakes the drain caller, the worker is finished with `s`. */
	struct z_work_drain_sentinel s;
	k_work_init(&s.work, z_work_drain_handler);
	k_sem_init(&s.sem, 0, 1);

	int ret = k_work_submit_to_queue(queue, &s.work);
	if (ret < 0) {
		return ret;
	}
	return k_sem_take(&s.sem, K_FOREVER);
}

/* Auto-initialize the system work queue before main() */
static void __attribute__((constructor)) sys_work_q_auto_init(void)
{
	static const struct k_work_queue_config sys_cfg = {
		.name = "sys_wq",
#ifdef CONFIG_SYSTEM_WORKQUEUE_NO_YIELD
		.no_yield = true,
#else
		.no_yield = false,
#endif
	};
	k_work_queue_start(&k_sys_work_q, sys_wq_stack, sizeof(sys_wq_stack),
			   CONFIG_SYSTEM_WORKQUEUE_PRIORITY, &sys_cfg);
}

/* ----------------------------------------------------------------
 * Delayable Work
 * ---------------------------------------------------------------- */

static void K_ISR_SAFE k_work_delayable_timer_expiry(struct k_timer *timer)
{
	struct k_work_delayable *dwork = CONTAINER_OF(timer, struct k_work_delayable, timer);
	if (dwork->queue) {
		k_work_submit_to_queue(dwork->queue, &dwork->work);
	} else {
		k_work_submit(&dwork->work);
	}
}

void k_work_init_delayable(struct k_work_delayable *dwork, k_work_handler_t handler)
{
	k_work_init(&dwork->work, handler);
	k_timer_init(&dwork->timer, k_work_delayable_timer_expiry, NULL);
	dwork->queue = NULL;
}

int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	return k_work_schedule_for_queue(&k_sys_work_q, dwork, delay);
}

int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *dwork,
			      k_timeout_t delay)
{
	if (k_work_is_pending(&dwork->work)) {
		return 0; /* Already scheduled */
	}

	dwork->queue = queue;

	if (k_timeout_is_no_wait(delay)) {
		return k_work_submit_to_queue(queue, &dwork->work);
	}

	k_timer_start(&dwork->timer, delay, K_NO_WAIT);
	return 0;
}

int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	k_work_cancel_delayable(dwork);

	if (k_timeout_is_no_wait(delay)) {
		return k_work_submit(&dwork->work);
	}

	k_timer_start(&dwork->timer, delay, K_NO_WAIT);
	return 0;
}

int k_work_cancel_delayable(struct k_work_delayable *dwork)
{
	k_timer_stop(&dwork->timer);
	k_work_cancel(&dwork->work);
	return 0;
}

bool k_work_delayable_is_pending(struct k_work_delayable *dwork)
{
	return k_work_is_pending(&dwork->work) || dwork->timer.running;
}

int64_t k_work_delayable_remaining_get(struct k_work_delayable *dwork)
{
	return k_timer_remaining_get(&dwork->timer);
}
