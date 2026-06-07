/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Notification-backed k_event (same architecture as k_sem): the events
 * word and waiter list live in the caller-owned struct under its lock;
 * blocking rides direct-to-task notifications on the reserved index.
 * Nothing of the caller's memory ever enters a kernel list (the
 * object-lifetime principle -- zkernel README, #40). Replaces the
 * FreeRTOS event-group backend, which capped usable events at 24 bits
 * (EventBits_t reserves the top byte) and could not express upstream's
 * set-replaces / reset-before-wait / 0-on-timeout semantics.
 */

#include "zephyr/kernel.h"

#include "esp_attr.h"
#include "sdkconfig.h"

#include "zkernel_internal.h"

struct z_event_waiter {
	sys_dnode_t node;
	TaskHandle_t task;
	uint32_t mask;    /* events the waiter is interested in */
	bool all;         /* require all bits vs any bit */
	bool clear;       /* _safe variant: consume matched bits on wake */
	uint32_t matched; /* set by the waker under the lock */
	bool woken;       /* a post/set targeted this waiter */
};

static uint32_t z_event_match(uint32_t current, uint32_t mask, bool all)
{
	uint32_t hit = current & mask;

	if (all && hit != mask) {
		return 0;
	}
	return hit;
}

/* Apply `events` under `mask`, wake every waiter whose condition
 * becomes met (upstream: all satisfied waiters unpend), and return the
 * previous value of the masked events. Satisfied waiters are unlinked
 * into a local chain in ONE lock pass and notified after unlock --
 * touching the stack-resident nodes post-unlock is safe because
 * woken==true forces each waiter into the consume path, where it
 * blocks until our notification lands (same argument as k_sem_give's
 * handle copy, extended to the chained node). */
static uint32_t K_ISR_SAFE z_event_post_internal(struct k_event *event, uint32_t events,
						 uint32_t mask)
{
	sys_dlist_t woken_chain;

	sys_dlist_init(&woken_chain);

	z_kernel_lock(&event->lock);

	uint32_t previous = event->events & mask;

	event->events = (event->events & ~mask) | (events & mask);

	sys_dnode_t *n = sys_dlist_peek_head(&event->waiters);
	uint32_t clear_accum = 0;

	while (n != NULL) {
		sys_dnode_t *next = sys_dlist_peek_next(&event->waiters, n);
		struct z_event_waiter *w = CONTAINER_OF(n, struct z_event_waiter, node);
		uint32_t hit = z_event_match(event->events, w->mask, w->all);

		if (hit != 0) {
			w->matched = hit;
			w->woken = true;
			if (w->clear) {
				/* Accumulate; applied AFTER the walk so every
				 * waiter is evaluated against the same posted
				 * state (upstream applies clear_events at the
				 * end of its wait-queue walk -- clearing
				 * mid-walk would starve overlapping waiters). */
				clear_accum |= hit;
			}
			sys_dlist_remove(&w->node);
			sys_dlist_append(&woken_chain, &w->node);
		}
		n = next;
	}

	event->events &= ~clear_accum;

	z_kernel_unlock(&event->lock);

	for (n = sys_dlist_get(&woken_chain); n != NULL; n = sys_dlist_get(&woken_chain)) {
		struct z_event_waiter *w = CONTAINER_OF(n, struct z_event_waiter, node);

		if (xPortInIsrContext()) {
			BaseType_t woken = pdFALSE;

			vTaskNotifyGiveIndexedFromISR(w->task, Z_KERNEL_NOTIFY_INDEX, &woken);
			portYIELD_FROM_ISR(woken);
		} else {
			xTaskNotifyGiveIndexed(w->task, Z_KERNEL_NOTIFY_INDEX);
		}
	}

	return previous;
}

void k_event_init(struct k_event *event)
{
	event->events = 0;
	sys_dlist_init(&event->waiters);
	event->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
}

uint32_t K_ISR_SAFE k_event_post(struct k_event *event, uint32_t events)
{
	return z_event_post_internal(event, events, events);
}

uint32_t K_ISR_SAFE k_event_set(struct k_event *event, uint32_t events)
{
	return z_event_post_internal(event, events, UINT32_MAX);
}

uint32_t K_ISR_SAFE k_event_set_masked(struct k_event *event, uint32_t events, uint32_t events_mask)
{
	return z_event_post_internal(event, events, events_mask);
}

uint32_t K_ISR_SAFE k_event_clear(struct k_event *event, uint32_t events)
{
	return z_event_post_internal(event, 0, events);
}

static uint32_t z_event_wait_internal(struct k_event *event, uint32_t events, bool all, bool reset,
				      bool clear, k_timeout_t timeout)
{
	/* The waiter node lives on THIS stack frame; it is unlinked under
	 * the event lock (by a waker or by our timeout path) before we
	 * return -- synchronous severance by construction. Identity is
	 * sampled OUTSIDE the lock and only on the must-block path, via
	 * the unlock-sample-relock-recheck loop (see k_sem_take: keeps
	 * the fast paths pre-scheduler-safe and FreeRTOS calls out of
	 * the critical section). */
	struct z_event_waiter w = {0};

	for (;;) {
		z_kernel_lock(&event->lock);

		if (reset) {
			/* Upstream: zero the ENTIRE tracked set before
			 * waiting -- once, not on every recheck pass. */
			event->events = 0;
			reset = false;
		}

		uint32_t hit = z_event_match(event->events, events, all);

		if (hit != 0) {
			if (clear) {
				event->events &= ~hit;
			}
			z_kernel_unlock(&event->lock);
			return hit;
		}

		if (k_timeout_is_no_wait(timeout)) {
			z_kernel_unlock(&event->lock);
			return 0;
		}

		if (w.task != NULL) {
			break; /* sampled on a previous pass; enqueue under THIS lock */
		}

		z_kernel_unlock(&event->lock);
		w.task = xTaskGetCurrentTaskHandle();
		w.mask = events;
		w.all = all;
		w.clear = clear;
	}

	sys_dlist_append(&event->waiters, &w.node);
	z_kernel_unlock(&event->lock);

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

		uint32_t got = ulTaskNotifyTakeIndexed(Z_KERNEL_NOTIFY_INDEX, pdTRUE, wait);

		z_kernel_lock(&event->lock);
		if (w.woken) {
			/* A waker unlinked us. If our wait timed out in the
			 * same instant (got == 0), its notification is still
			 * in flight -- consume it before returning so it
			 * cannot poison a later blocking call on this task. */
			uint32_t matched = w.matched;

			z_kernel_unlock(&event->lock);
			if (got == 0) {
				(void)ulTaskNotifyTakeIndexed(Z_KERNEL_NOTIFY_INDEX, pdTRUE,
							      portMAX_DELAY);
			}
			return matched;
		}

		if (got == 0 && !forever) {
			/* Timed out, not targeted: unlink ourselves. After
			 * this unlock no waker can see the node. */
			sys_dlist_remove(&w.node);
			z_kernel_unlock(&event->lock);
			return 0;
		}

		/* Spurious wake (stale notification from code misusing the
		 * reserved index): loop; the wait recompute above absorbs
		 * the elapsed time. */
		z_kernel_unlock(&event->lock);
	}
}

uint32_t k_event_wait(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout)
{
	return z_event_wait_internal(event, events, false, reset, false, timeout);
}

uint32_t k_event_wait_all(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout)
{
	return z_event_wait_internal(event, events, true, reset, false, timeout);
}

uint32_t k_event_wait_safe(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout)
{
	return z_event_wait_internal(event, events, false, reset, true, timeout);
}

uint32_t k_event_wait_all_safe(struct k_event *event, uint32_t events, bool reset,
			       k_timeout_t timeout)
{
	return z_event_wait_internal(event, events, true, reset, true, timeout);
}
