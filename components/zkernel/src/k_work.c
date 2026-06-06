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
			__atomic_and_fetch(&work->flags, (uint32_t)~K_WORK_QUEUED,
					   __ATOMIC_RELAXED);
			__atomic_store_n(&work->queue, NULL, __ATOMIC_RELEASE);
		}
		portEXIT_CRITICAL(&queue->lock);

		if (work == NULL || work->handler == NULL) {
			continue;
		}

		work->handler(work);

		__atomic_and_fetch(&work->flags, (uint32_t)~K_WORK_RUNNING, __ATOMIC_RELAXED);

		/* Signal any flush/cancel_sync waiter. Exchange so that
		 * exactly one of {worker, cancel unqueue, waiter detach}
		 * consumes the slot -- see z_work_sync_wait_or_detach(). */
		struct k_work_sync *sync = __atomic_exchange_n(&work->sync, NULL, __ATOMIC_ACQ_REL);
		if (sync) {
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
	if (work->handler == NULL) {
		/* Upstream __ASSERTs here; return -EINVAL instead of
		 * queueing an item the worker would silently skip. */
		return -EINVAL;
	}

	z_work_lock(queue);

	uint32_t flags = __atomic_load_n(&work->flags, __ATOMIC_RELAXED);

	if (flags & K_WORK_CANCELING) {
		/* Rejected while a cancellation is in progress -- checked
		 * before the already-queued case, matching upstream's
		 * order. See k_work_cancel_sync(). */
		z_work_unlock(queue);
		return -EBUSY;
	}

	if (flags & K_WORK_QUEUED) {
		/* Already queued -- no-op (upstream retval 0) */
		z_work_unlock(queue);
		return 0;
	}

	__atomic_or_fetch(&work->flags, K_WORK_QUEUED, __ATOMIC_RELAXED);
	__atomic_store_n(&work->queue, queue, __ATOMIC_RELEASE);
	sys_dlist_append(&queue->pending, &work->node);

	z_work_unlock(queue);

	/* k_sem_give is already ISR-safe (uses xSemaphoreGiveFromISR). */
	k_sem_give(&queue->sem);

	/* Upstream retvals: 1 = was idle, now queued; 2 = was running and
	 * has been queued again. The RUNNING snapshot was taken under the
	 * queue lock above; like upstream's under-lock decision it can be
	 * stale by the time the caller acts on it. */
	return (flags & K_WORK_RUNNING) ? 2 : 1;
}

int K_ISR_SAFE k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work)
{
	if (!(__atomic_load_n(&queue->flags, __ATOMIC_RELAXED) & Z_WORK_QUEUE_STARTED)) {
		return -ENODEV; /* matches upstream "queue not started" */
	}
	return k_work_submit_internal(queue, work);
}

int K_ISR_SAFE k_work_submit(struct k_work *work)
{
	return k_work_submit_to_queue(&k_sys_work_q, work);
}

int k_work_cancel(struct k_work *work)
{
	struct k_work_q *queue = __atomic_load_n(&work->queue, __ATOMIC_ACQUIRE);
	if (queue == NULL) {
		/* Not queued. RUNNING/CANCELING may remain (upstream busy
		 * snapshot); a running handler is not interrupted. */
		return (int)k_work_busy_get(work);
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
		return (int)k_work_busy_get(work);
	}

	uint32_t flags = __atomic_load_n(&work->flags, __ATOMIC_RELAXED);

	if (flags & K_WORK_QUEUED) {
		/* Remove the queued instance even if a previous instance is
		 * still RUNNING (it was queued again while running) --
		 * otherwise that second instance survives the cancel and
		 * runs after a cancel_sync returns. Matches upstream. */
		sys_dlist_remove(&work->node);
		__atomic_and_fetch(&work->flags, (uint32_t)~K_WORK_QUEUED, __ATOMIC_RELAXED);
		__atomic_store_n(&work->queue, NULL, __ATOMIC_RELEASE);

		/* If a flush waiter (k_work_flush / k_work_cancel_sync) is
		 * attached AND no handler is running, release it here -- the
		 * cancelled item will never run to completion on a worker
		 * thread, so the worker won't give the sem. When a handler
		 * IS running, the worker gives the sem at its completion;
		 * releasing here would wake the waiter early. Atomic
		 * exchange so only one path consumes it. k_sem_give itself
		 * is hoisted outside the critical section to avoid calling
		 * FreeRTOS API from portENTER_CRITICAL. */
		if (!(flags & K_WORK_RUNNING)) {
			sync_to_release = __atomic_exchange_n(&work->sync, NULL, __ATOMIC_ACQ_REL);
		}
	}

	z_work_unlock(queue);

	if (sync_to_release != NULL) {
		k_sem_give(&sync_to_release->sem);
	}

	/* The sem give that submit issued for this item will cause one
	 * spurious worker wakeup; it pops a NULL and loops. Harmless. */
	return (int)k_work_busy_get(work);
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

/* Attach a caller-owned sync waiter to the work item's (single) sync
 * slot. Set up BEFORE inspecting work state to avoid TOCTOU: if the
 * work finishes between the caller's check and its wait, the worker
 * has already signaled the sem. */
static void z_work_sync_attach(struct k_work *work, struct k_work_sync *sync)
{
	k_sem_init(&sync->sem, 0, 1);

	struct k_work_sync *prev = __atomic_exchange_n(&work->sync, sync, __ATOMIC_ACQ_REL);

	__ASSERT(prev == NULL, "concurrent k_work_flush/cancel_sync on one item");
	(void)prev;
}

/* Complete a sync wait: either block until the slot's sem is given
 * (wait == true), or detach the waiter. Detaching races the slot
 * consumers (worker completion, cancel unqueue) -- all sides use
 * atomic exchange, so exactly one party owns the slot. Losing the
 * exchange means a k_sem_give on our caller-owned sync is in flight;
 * consume it so the sync outlives the give (the giver does not touch
 * the sem after the taker is released -- same premise as the drain
 * sentinel). */
static void z_work_sync_wait_or_detach(struct k_work *work, struct k_work_sync *sync, bool wait)
{
	if (wait || __atomic_exchange_n(&work->sync, NULL, __ATOMIC_ACQ_REL) == NULL) {
		k_sem_take(&sync->sem, K_FOREVER);
	}
}

int k_work_flush(struct k_work *work, struct k_work_sync *sync)
{
	z_work_sync_attach(work, sync);
	z_work_sync_wait_or_detach(work, sync, k_work_is_pending(work));
	return 0;
}

bool k_work_cancel_sync(struct k_work *work, struct k_work_sync *sync)
{
	bool pending = k_work_busy_get(work) != 0;

	/* Attach sync and set CANCELING before cancelling: from here until
	 * the clear below, submit rejects this item with -EBUSY, so nothing
	 * can requeue it behind our back. */
	z_work_sync_attach(work, sync);
	__atomic_or_fetch(&work->flags, K_WORK_CANCELING, __ATOMIC_RELAXED);

	k_work_cancel(work);

	z_work_sync_wait_or_detach(
		work, sync,
		(__atomic_load_n(&work->flags, __ATOMIC_RELAXED) & K_WORK_RUNNING) != 0);

	__atomic_and_fetch(&work->flags, (uint32_t)~K_WORK_CANCELING, __ATOMIC_RELAXED);
	return pending;
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
	/* Upstream no-ops (retval 0) when the item is delayed, queued, or
	 * canceling -- but DOES schedule when it is only running (the next
	 * occurrence may be scheduled while the current one executes). */
	if ((k_work_busy_get(&dwork->work) & (uint32_t)~K_WORK_RUNNING) != 0 ||
	    __atomic_load_n(&dwork->timer.running, __ATOMIC_ACQUIRE)) {
		return 0; /* already scheduled -- no change */
	}

	dwork->queue = queue;

	if (k_timeout_is_no_wait(delay)) {
		return k_work_submit_to_queue(queue, &dwork->work);
	}

	k_timer_start(&dwork->timer, delay, K_NO_WAIT);
	return 1; /* upstream: scheduled (timeout armed) */
}

int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	return k_work_reschedule_for_queue(&k_sys_work_q, dwork, delay);
}

int k_work_reschedule_for_queue(struct k_work_q *queue, struct k_work_delayable *dwork,
				k_timeout_t delay)
{
	k_work_cancel_delayable(dwork);
	dwork->queue = queue;

	if (k_timeout_is_no_wait(delay)) {
		return k_work_submit_to_queue(queue, &dwork->work);
	}

	k_timer_start(&dwork->timer, delay, K_NO_WAIT);
	return 1; /* upstream: scheduled (timeout armed) */
}

int k_work_cancel_delayable(struct k_work_delayable *dwork)
{
	k_timer_stop(&dwork->timer);
	k_work_cancel(&dwork->work);
	/* Upstream: the busy state after the cancellation steps complete.
	 * RUNNING/CANCELING may remain; delayed and queued states are gone. */
	return (int)k_work_busy_get(&dwork->work);
}

bool k_work_cancel_delayable_sync(struct k_work_delayable *dwork, struct k_work_sync *sync)
{
	bool pending = k_work_delayable_is_pending(dwork);

	/* Own CANCELING for the whole operation: submit (including the
	 * timer-expiry path) rejects this item with -EBUSY until the clear
	 * below, so neither a handler nor a racing expiry can requeue it. */
	z_work_sync_attach(&dwork->work, sync);
	__atomic_or_fetch(&dwork->work.flags, K_WORK_CANCELING, __ATOMIC_RELAXED);

	k_timer_stop(&dwork->timer); /* pending expiry */
	k_work_cancel(&dwork->work); /* queued instance */

	z_work_sync_wait_or_detach(
		&dwork->work, sync,
		(__atomic_load_n(&dwork->work.flags, __ATOMIC_RELAXED) & K_WORK_RUNNING) != 0);

	/* A handler may legally re-arm the timer during the cancel (the
	 * delayed reschedule path is not CANCELING-gated, matching
	 * upstream); now that no handler can be running, kill any such
	 * re-arm before clearing CANCELING. */
	k_timer_stop(&dwork->timer);

	__atomic_and_fetch(&dwork->work.flags, (uint32_t)~K_WORK_CANCELING, __ATOMIC_RELAXED);
	return pending;
}

bool k_work_delayable_is_pending(struct k_work_delayable *dwork)
{
	return k_work_is_pending(&dwork->work) ||
	       __atomic_load_n(&dwork->timer.running, __ATOMIC_ACQUIRE);
}

int64_t k_work_delayable_remaining_get(struct k_work_delayable *dwork)
{
	return k_timer_remaining_get(&dwork->timer);
}
