/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Linux-target backend for k_timer. esp_timer is headers-only on the
 * linux target, so expiries are dispatched from a dedicated FreeRTOS
 * task instead: armed timers sit in a deadline-sorted dlist; the
 * dispatcher blocks on a semaphore with a timeout equal to the time
 * until the nearest deadline, fires due timers in task context, and
 * re-arms periodic ones. This mirrors hardware's ESP_TIMER_TASK
 * dispatch architecture (one high-priority service task, sorted
 * pending list), so callback-context semantics match across targets.
 *
 * Why not POSIX timers (SIGEV_THREAD)? SIGEV_THREAD callbacks run on
 * foreign pthreads the FreeRTOS POSIX port knows nothing about. The
 * port schedules by suspending/resuming task pthreads keyed off
 * xTaskGetCurrentTaskHandle(), and its critical sections are per-thread
 * signal masks -- any FreeRTOS call from a foreign thread (and a timer
 * expiry exists to make such calls: k_work_submit -> k_sem_give, ...)
 * manipulates the *current task's* scheduler state from outside it and
 * corrupts the port. A FreeRTOS-native dispatcher task is the only safe
 * callback context.
 *
 * Expiry resolution is one FreeRTOS tick (1ms at the Boreas default
 * CONFIG_FREERTOS_HZ=1000), gated by the POSIX port's SIGALRM tick.
 */

#include "zephyr/kernel.h"

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "k_timer";

static sys_dlist_t z_armed_list = SYS_DLIST_STATIC_INIT(&z_armed_list);
static portMUX_TYPE z_armed_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t z_recalc_sem;

/* Insert into the armed list keeping it sorted by ascending deadline.
 * Caller must hold z_armed_lock. */
static void z_armed_insert_locked(struct k_timer *timer)
{
	sys_dnode_t *pos;

	SYS_DLIST_FOR_EACH_NODE(&z_armed_list, pos) {
		struct k_timer *t = CONTAINER_OF(pos, struct k_timer, node);
		if (timer->deadline_us < t->deadline_us) {
			sys_dlist_insert(pos, &timer->node);
			return;
		}
	}
	sys_dlist_append(&z_armed_list, &timer->node);
}

static void z_timer_dispatcher(void *arg)
{
	(void)arg;
	const int64_t tick_us = (int64_t)portTICK_PERIOD_MS * 1000;

	for (;;) {
		struct k_timer *due = NULL;
		TickType_t wait = portMAX_DELAY;

		portENTER_CRITICAL(&z_armed_lock);
		sys_dnode_t *head = sys_dlist_peek_head(&z_armed_list);
		if (head != NULL) {
			struct k_timer *t = CONTAINER_OF(head, struct k_timer, node);
			int64_t now = z_uptime_us();

			if ((int64_t)t->deadline_us <= now) {
				sys_dlist_remove(&t->node);
				if (t->is_periodic) {
					/* Re-arm relative to the previous deadline,
					 * not `now`, so periodic timers don't
					 * accumulate dispatch-latency drift. */
					t->deadline_us += t->period_us;
					z_armed_insert_locked(t);
				}
				due = t;
			} else {
				/* Round up so we never wake a tick early and
				 * spin; the head re-check on wake handles any
				 * timer started while we slept. */
				int64_t delta_us = (int64_t)t->deadline_us - now;
				int64_t wait_ticks = (delta_us + tick_us - 1) / tick_us;
				/* Clamp: a far deadline must not wrap TickType_t.
				 * portMAX_DELAY (block until signaled) is correct
				 * -- any start/stop gives z_recalc_sem. Compare
				 * unsigned: portMAX_DELAY is all-ones, so a signed
				 * cast would be -1 and clamp everything. */
				wait = ((uint64_t)wait_ticks >= (uint64_t)portMAX_DELAY)
					       ? portMAX_DELAY
					       : (TickType_t)wait_ticks;
			}
		}
		portEXIT_CRITICAL(&z_armed_lock);

		if (due != NULL) {
			/* Fire outside the critical section -- user callbacks
			 * may give semaphores, submit work, or block. Ordering
			 * mirrors k_timer.c's k_timer_esp_callback. */
			__atomic_fetch_add(&due->status, 1, __ATOMIC_RELEASE);
			if (due->expiry_fn) {
				due->expiry_fn(due);
			}
			/* One-shot is done after this expiry; clear running so
			 * subsequent status_sync / remaining_get behave like an
			 * upstream stopped timer. Cleared AFTER expiry_fn (see
			 * k_timer.c for the rationale). */
			if (!__atomic_load_n(&due->is_periodic, __ATOMIC_ACQUIRE)) {
				__atomic_store_n(&due->running, false, __ATOMIC_RELEASE);
			}
			continue; /* more timers may already be due */
		}

		xSemaphoreTake(z_recalc_sem, wait);
	}
}

/* Bring the dispatcher up before main() -- same pattern as the system
 * workqueue constructor in k_work.c. On the linux port, xTaskCreate
 * before vTaskStartScheduler just queues the task; it runs once the
 * scheduler starts. */
static void __attribute__((constructor)) z_timer_backend_init(void)
{
	z_recalc_sem = xSemaphoreCreateBinary();
	__ASSERT(z_recalc_sem != NULL, "k_timer dispatcher sem alloc failed");

	/* Depth is in StackType_t words: bytes/words coincide on ESP-IDF
	 * hardware only because portSTACK_TYPE is uint8_t there; the linux
	 * port uses unsigned long. Same idiom as k_thread_create. */
	BaseType_t ret =
		xTaskCreate(z_timer_dispatcher, "k_timer",
			    CONFIG_K_TIMER_LINUX_DISPATCHER_STACK_SIZE / sizeof(StackType_t), NULL,
			    CONFIG_K_TIMER_LINUX_DISPATCHER_PRIORITY, NULL);
	__ASSERT(ret == pdPASS, "k_timer dispatcher task create failed");
	(void)ret;
}

void k_timer_init(struct k_timer *timer, k_timer_expiry_t expiry_fn, k_timer_stop_t stop_fn)
{
	/* Re-init of a still-armed timer is a caller error upstream, but a
	 * memset'd/reused struct would corrupt the armed list here (the
	 * node is intrusive). Defensively unlink by address comparison --
	 * never dereference timer->node itself, since an uninitialized
	 * struct holds garbage there. On hardware the equivalent mistake
	 * merely leaks an esp_timer handle. */
	portENTER_CRITICAL(&z_armed_lock);
	sys_dnode_t *pos;
	SYS_DLIST_FOR_EACH_NODE(&z_armed_list, pos) {
		if (pos == &timer->node) {
			sys_dlist_remove(pos);
			break;
		}
	}
	portEXIT_CRITICAL(&z_armed_lock);

	timer->expiry_fn = expiry_fn;
	timer->stop_fn = stop_fn;
	timer->user_data = NULL;
	timer->status = 0;
	timer->running = false;
	timer->is_periodic = false;
	timer->first_interval_pending = false; /* unused on linux; deadline math is direct */
	timer->period_us = 0;
	timer->deadline_us = 0;
	timer->node.next = NULL;
	timer->node.prev = NULL;
	/* No esp_timer exists on linux; `handle` serves only as the shared
	 * initialized-sentinel (see struct k_timer). Any non-NULL value. */
	timer->handle = (esp_timer_handle_t)timer;
}

void k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period)
{
	if (timer->handle == NULL) {
		ESP_LOGE(TAG, "Timer not initialized");
		return;
	}

	bool periodic = !k_timeout_is_no_wait(period);
	uint64_t duration_us = k_timeout_to_us(duration);

	portENTER_CRITICAL(&z_armed_lock);
	if (sys_dnode_is_linked(&timer->node)) {
		sys_dlist_remove(&timer->node); /* restart: drop the old deadline */
	}
	__atomic_store_n(&timer->status, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&timer->is_periodic, periodic, __ATOMIC_RELEASE);
	timer->period_us = periodic ? k_timeout_to_us(period) : 0;
	timer->deadline_us = (uint64_t)z_uptime_us() + duration_us;
	z_armed_insert_locked(timer);
	__atomic_store_n(&timer->running, true, __ATOMIC_RELEASE);
	portEXIT_CRITICAL(&z_armed_lock);

	/* Wake the dispatcher so it recomputes its sleep against the
	 * (possibly nearer) new head deadline. */
	xSemaphoreGive(z_recalc_sem);
}

void k_timer_stop(struct k_timer *timer)
{
	if (!__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE)) {
		return;
	}

	portENTER_CRITICAL(&z_armed_lock);
	if (sys_dnode_is_linked(&timer->node)) {
		sys_dlist_remove(&timer->node);
	}
	__atomic_store_n(&timer->running, false, __ATOMIC_RELEASE);
	portEXIT_CRITICAL(&z_armed_lock);

	if (timer->stop_fn) {
		timer->stop_fn(timer);
	}
}

uint32_t k_timer_status_get(struct k_timer *timer)
{
	return __atomic_exchange_n(&timer->status, 0, __ATOMIC_ACQ_REL);
}

uint32_t k_timer_status_sync(struct k_timer *timer)
{
	/* Same poll-based implementation as the hardware backend in
	 * k_timer.c -- see the divergence note there. */
	while (__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE) &&
	       __atomic_load_n(&timer->status, __ATOMIC_ACQUIRE) == 0) {
		k_msleep(1);
	}
	return __atomic_exchange_n(&timer->status, 0, __ATOMIC_ACQ_REL);
}

/* Locked snapshot of deadline_us: the dispatcher re-arms periodic timers
 * (and k_timer_start rewrites the deadline) under z_armed_lock, so an
 * unlocked read is a data race and a potential torn read on 32-bit. */
static uint64_t z_timer_deadline_us(const struct k_timer *timer)
{
	portENTER_CRITICAL(&z_armed_lock);
	uint64_t deadline_us = timer->deadline_us;
	portEXIT_CRITICAL(&z_armed_lock);
	return deadline_us;
}

uint32_t k_timer_remaining_get(struct k_timer *timer)
{
	if (!__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE) || timer->handle == NULL) {
		return 0;
	}
	int64_t remaining_us = (int64_t)z_timer_deadline_us(timer) - z_uptime_us();
	if (remaining_us <= 0) {
		return 0;
	}
	int64_t remaining_ms = remaining_us / 1000;
	/* Saturate at UINT32_MAX (~49.7 days) rather than wrapping. */
	return (remaining_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)remaining_ms;
}

k_ticks_t k_timer_remaining_ticks(const struct k_timer *timer)
{
	if (!__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE) || timer->handle == NULL) {
		return 0;
	}
	int64_t remaining_us = (int64_t)z_timer_deadline_us(timer) - z_uptime_us();
	if (remaining_us <= 0) {
		return 0;
	}
	/* Round UP so any positive remaining time reports at least 1 tick --
	 * the contract is that 0 means stopped or already expired. */
	const int64_t tick_us = (int64_t)portTICK_PERIOD_MS * 1000;
	return (k_ticks_t)((remaining_us + tick_us - 1) / tick_us);
}

k_ticks_t k_timer_expires_ticks(const struct k_timer *timer)
{
	const int64_t tick_us = (int64_t)portTICK_PERIOD_MS * 1000;

	/* Upstream: when the timer is not running, returns the CURRENT
	 * uptime (not zero). z_uptime_us() is the shared monotonic clock
	 * domain with k_uptime_ticks(). */
	if (!__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE) || timer->handle == NULL) {
		return (k_ticks_t)(z_uptime_us() / tick_us);
	}
	return (k_ticks_t)(z_timer_deadline_us(timer) / (uint64_t)tick_us);
}
