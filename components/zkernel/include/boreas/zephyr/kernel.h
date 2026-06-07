/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas: Zephyr-compatible kernel API for ESP-IDF.
 *
 * This header provides the primary kernel primitives:
 *   k_timer, k_work, k_work_delayable, k_sem, k_mutex,
 *   k_msgq, k_event, k_thread, k_sleep/k_msleep
 *
 * Each primitive wraps FreeRTOS / ESP-IDF equivalents with
 * Zephyr-compatible API conventions.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_timer.h"

#if CONFIG_IDF_TARGET_LINUX
#include <time.h>
#endif

#include "zephyr/sys/atomic.h"
#include "zephyr/sys/byteorder.h"
#include "zephyr/sys/dlist.h"
#include "zephyr/sys/time_units.h"
#include "zephyr/sys/util.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upstream-compatible alias for the per-target stack element type. Lets
 * code ported from Zephyr declare `k_thread_stack_t *stack` parameters
 * verbatim. On Boreas this is FreeRTOS's StackType_t. */
typedef StackType_t k_thread_stack_t;

/* ----------------------------------------------------------------
 * Uptime
 * ---------------------------------------------------------------- */

/**
 * @brief Monotonic microsecond clock (internal helper).
 *
 * @note Single time source for all "uptime"-flavored APIs (k_uptime_*,
 *       k_timer_expires_ticks) and the k_timer linux backend, so they
 *       share one clock domain. On hardware this is esp_timer_get_time();
 *       on linux, where esp_timer is headers-only, it is CLOCK_MONOTONIC
 *       -- monotonic, not wall clock, so immune to NTP/date jumps.
 */
#if CONFIG_IDF_TARGET_LINUX
static inline int64_t z_uptime_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
#else
static inline int64_t z_uptime_us(void)
{
	return esp_timer_get_time();
}
#endif

/** Get system uptime in milliseconds (monotonic). */
static inline int64_t k_uptime_get(void)
{
	return z_uptime_us() / 1000;
}

/** Get system uptime in 32-bit milliseconds (wraps). */
static inline uint32_t k_uptime_get_32(void)
{
	return (uint32_t)k_uptime_get();
}

/**
 * @brief Get system uptime in FreeRTOS-tick-period units, derived from
 *        the monotonic microsecond clock. Mirrors upstream Zephyr's
 *        k_uptime_ticks().
 *
 * @return Tick count since boot, as k_ticks_t.
 *
 * @note Boreas implementation derives this from the monotonic
 *       microsecond clock (esp_timer on hardware, CLOCK_MONOTONIC on
 *       linux -- the same clock domain as k_uptime_get and
 *       k_timer_expires_ticks), NOT from xTaskGetTickCount. This keeps
 *       all "uptime"-flavored APIs
 *       in a single clock domain and makes
 *       `k_uptime_ticks() * portTICK_PERIOD_MS ~= k_uptime_get()`.
 *       Code that genuinely wants the FreeRTOS scheduler tick counter
 *       should call xTaskGetTickCount() directly.
 */
static inline k_ticks_t k_uptime_ticks(void)
{
	return (k_ticks_t)(z_uptime_us() / ((int64_t)portTICK_PERIOD_MS * 1000));
}

/**
 * Compute delta since *reftime, update *reftime to now.
 * Overflow-safe.
 */
static inline int64_t k_uptime_delta(int64_t *reftime)
{
	int64_t now = k_uptime_get();
	int64_t delta = now - *reftime;
	*reftime = now;
	return delta;
}

/* ----------------------------------------------------------------
 * Sleep
 * ---------------------------------------------------------------- */

static inline void k_msleep(int32_t ms)
{
	vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * Sleep for the given timeout. Returns the time remaining if the
 * sleep was interrupted; 0 otherwise. Boreas does not currently
 * support interruptible sleeps, so this always returns 0 — the
 * return type matches upstream Zephyr so ports compile cleanly.
 */
static inline int32_t k_sleep(k_timeout_t timeout)
{
	if (k_timeout_is_no_wait(timeout)) {
		taskYIELD();
	} else if (k_timeout_is_forever(timeout)) {
		vTaskDelay(portMAX_DELAY);
	} else {
		vTaskDelay(k_timeout_to_ticks(timeout));
	}
	return 0;
}

/**
 * Sleep for @p us microseconds. Returns the time remaining if
 * interrupted; 0 otherwise. See k_sleep() for the interruption
 * caveat. Sub-millisecond sleeps busy-wait via esp_rom_delay_us
 * (FreeRTOS tick granularity does not allow finer task-yield).
 */
static inline int32_t k_usleep(int32_t us)
{
	if (us >= 1000) {
		vTaskDelay(pdMS_TO_TICKS(us / 1000));
	} else if (us > 0) {
		esp_rom_delay_us(us);
	}
	return 0;
}

static inline void k_yield(void)
{
	taskYIELD();
}

/* ----------------------------------------------------------------
 * Semaphore
 * ---------------------------------------------------------------- */

/* Notification-backed (no FreeRTOS control block): count/limit/waiter
 * list live here, guarded by the lock; blocking rides direct-to-task
 * notifications on a reserved index, whose state is kernel-owned. See
 * the README design principle -- when k_sem_take returns, the kernel
 * holds no references into this struct. */
struct k_sem {
	uint32_t count;
	uint32_t limit;
	sys_dlist_t waiters; /* of z_sem_waiter, caller-stack resident */
	portMUX_TYPE lock;
};

/**
 * Statically define a fully-initialized semaphore. True compile-time
 * initializer (matches upstream Zephyr): usable from constructors and
 * SYS_INIT callbacks without any runtime init step.
 */
#define K_SEM_DEFINE(name, _initial, _limit)                                                       \
	struct k_sem name = {                                                                      \
		.count = (_initial),                                                               \
		.limit = (_limit),                                                                 \
		.waiters = SYS_DLIST_STATIC_INIT(&name.waiters),                                   \
		.lock = portMUX_INITIALIZER_UNLOCKED,                                              \
	};                                                                                         \
	BUILD_ASSERT(((_limit) != 0) && ((_initial) <= (_limit)),                                  \
		     "K_SEM_DEFINE: limit must be nonzero and >= initial") /* upstream parity */

/**
 * @retval 0 on success
 * @retval -EINVAL if @p limit is zero or @p initial_count exceeds it
 *         (matches upstream)
 */
int k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit);
/**
 * @retval 0 on success
 * @retval -EBUSY if K_NO_WAIT and the semaphore was unavailable
 * @retval -EAGAIN on timeout, or if the semaphore was reset while
 *         waiting (matches upstream k_sem_reset semantics)
 *
 * @note Divergence: a thread blocked in k_sem_take must NOT be
 *       aborted (k_thread_abort / vTaskDelete). Upstream Zephyr
 *       unpends an aborted thread from any wait queue; Boreas cannot
 *       reach into the semaphore's waiter list from abort, so the
 *       dead thread would leave a dangling waiter node. The same
 *       applies to aborting a thread that is inside k_sem_give.
 * @note ISR context: legal only with K_NO_WAIT (the upstream
 *       contract). The K_NO_WAIT paths take only the ISR-safe
 *       spinlock (no task-notify or blocking FreeRTOS calls), and
 *       the function is IRAM-resident so the contract holds in
 *       IRAM-only ISR contexts (e.g. esp_timer ISR dispatch).
 * @note Divergence: a waiter's priority is sampled when it enqueues;
 *       k_thread_priority_set on a blocked thread does not re-sort
 *       the wake order (upstream re-sorts the pend queue).
 */
int k_sem_take(struct k_sem *sem, k_timeout_t timeout);
/** Give the semaphore (ISR-safe). Wakes the highest-priority waiter,
 *  FIFO among equal priorities (upstream wake order). */
void k_sem_give(struct k_sem *sem);
/** Zero the count and wake all waiters; their takes return -EAGAIN
 *  (upstream parity -- the previous FreeRTOS-backed implementation
 *  could only drain the count).
 *
 *  @note Task context only -- must not be called from an ISR. Matches
 *        upstream, whose k_sem_reset is likewise not isr-ok. */
void k_sem_reset(struct k_sem *sem);
unsigned int k_sem_count_get(struct k_sem *sem);

/* ----------------------------------------------------------------
 * Mutex
 *
 * Uses a non-recursive FreeRTOS mutex (which has priority inheritance)
 * with manual re-entrancy tracking. This gives Zephyr-compatible
 * behavior: re-entrant locking AND priority inheritance.
 * ---------------------------------------------------------------- */

struct k_mutex {
	SemaphoreHandle_t handle;
	StaticSemaphore_t buffer;
	TaskHandle_t owner; /* current owner for re-entrancy */
	uint32_t count;     /* recursion depth */
#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
	uint8_t order;      /* lock ordering -- lower must be acquired first */
	uint32_t lock_time; /* tick count at lock acquisition */
#endif
};

#define K_MUTEX_DEFINE(name) struct k_mutex name = {0}

int k_mutex_init(struct k_mutex *mutex);
int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
int k_mutex_unlock(struct k_mutex *mutex);

#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
int k_mutex_init_ordered(struct k_mutex *mutex, uint8_t order);
#define K_MUTEX_DEFINE_ORDERED(name, ord) struct k_mutex name = {.order = (ord)}
#endif

/* ----------------------------------------------------------------
 * Message Queue
 * ---------------------------------------------------------------- */

struct k_msgq {
	QueueHandle_t handle;
	StaticQueue_t buffer;
	uint8_t *storage;
	size_t msg_size;
	uint32_t max_msgs;
};

#define K_MSGQ_DEFINE(name, _msg_size, _max_msgs, _align)                                          \
	static uint8_t                                                                             \
		__attribute__((aligned(_align))) _k_msgq_buf_##name[(_msg_size) * (_max_msgs)];    \
	struct k_msgq name = {                                                                     \
		.storage = _k_msgq_buf_##name,                                                     \
		.msg_size = (_msg_size),                                                           \
		.max_msgs = (_max_msgs),                                                           \
	}

int k_msgq_init(struct k_msgq *msgq, char *buffer, size_t msg_size, uint32_t max_msgs);
int k_msgq_put(struct k_msgq *msgq, const void *data, k_timeout_t timeout);
int k_msgq_get(struct k_msgq *msgq, void *data, k_timeout_t timeout);
int k_msgq_peek(struct k_msgq *msgq, void *data);
void k_msgq_purge(struct k_msgq *msgq);
uint32_t k_msgq_num_used_get(struct k_msgq *msgq);
uint32_t k_msgq_num_free_get(struct k_msgq *msgq);

/* ----------------------------------------------------------------
 * Event
 * ---------------------------------------------------------------- */

/* Notification-backed (no FreeRTOS event group): the full 32-bit
 * events word and the waiter list live here, guarded by the lock --
 * the old event-group backend capped usable events at 24 bits. See
 * the README design principle: when a wait returns, the kernel holds
 * no references into this struct. */
struct k_event {
	uint32_t events;
	sys_dlist_t waiters; /* of z_event_waiter, caller-stack resident */
	portMUX_TYPE lock;
};

/** Statically define a fully-initialized event object (compile-time
 *  initializer, matching upstream). */
#define K_EVENT_DEFINE(name)                                                                       \
	struct k_event name = {                                                                    \
		.events = 0,                                                                       \
		.waiters = SYS_DLIST_STATIC_INIT(&name.waiters),                                   \
		.lock = portMUX_INITIALIZER_UNLOCKED,                                              \
	}

void k_event_init(struct k_event *event);
/** Merge (OR) @p events into the tracked set; all waiters whose
 *  conditions become met wake. @return the previous value of the
 *  posted events bits. ISR-safe. */
uint32_t k_event_post(struct k_event *event, uint32_t events);
/** REPLACE the tracked set with @p events (upstream semantics --
 *  setting differs from posting). @return the previous events value.
 *  ISR-safe. */
uint32_t k_event_set(struct k_event *event, uint32_t events);
/** Overwrite only the bits selected by @p events_mask. @return the
 *  previous value of the masked bits. ISR-safe. */
uint32_t k_event_set_masked(struct k_event *event, uint32_t events, uint32_t events_mask);
/** Clear @p events from the tracked set. @return their previous
 *  value. ISR-safe. */
uint32_t k_event_clear(struct k_event *event, uint32_t events);
/**
 * Wait for ANY of @p events. @p reset zeroes the ENTIRE tracked set
 * before waiting (upstream semantics). @return the matching events
 * (left set -- use the _safe variant to consume them), or 0 on
 * timeout.
 *
 * @note Like k_sem_take: blocking is forbidden in ISRs (K_NO_WAIT is
 *       fine), and a thread blocked here must not be aborted.
 */
uint32_t k_event_wait(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout);
/** As k_event_wait, but requires ALL of @p events. */
uint32_t k_event_wait_all(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout);
/** As k_event_wait, but atomically CLEARS the matched events before
 *  returning (upstream parity). */
uint32_t k_event_wait_safe(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout);
/** As k_event_wait_all, but atomically clears the matched events. */
uint32_t k_event_wait_all_safe(struct k_event *event, uint32_t events, bool reset,
			       k_timeout_t timeout);

/** Non-blocking poll: the currently-set events matching
 *  @p events_mask (not cleared). ISR-safe. */
static inline uint32_t k_event_test(struct k_event *event, uint32_t events_mask)
{
	return k_event_wait(event, events_mask, false, K_NO_WAIT);
}

/* ----------------------------------------------------------------
 * Timer (over esp_timer)
 * ---------------------------------------------------------------- */

struct k_timer;

/**
 * Timer expiry function type.
 *
 * @warning Callback context — matches upstream Zephyr when
 *          CONFIG_K_TIMER_DISPATCH_ISR=y (the default).
 *
 *          **Default (ISR dispatch):** Callback runs in true ISR context
 *          via ESP_TIMER_ISR. The function must be declared IRAM_ATTR
 *          and must be ISR-safe:
 *            - Must NOT call blocking APIs (k_sem_take with timeout,
 *              k_mutex_lock, k_msleep, k_thread_join).
 *            - The following are ISR-safe and may be called:
 *              k_sem_give, k_work_submit, k_event_set/post/clear,
 *              k_msgq_put (K_NO_WAIT).
 *            - LOG_* is NOT ISR-safe (vsnprintf in the emit path is
 *              flash-resident). Defer logging via k_work_submit.
 *            - Must not call malloc, printf, or any flash-resident
 *              function (IRAM_ATTR is required for cache survival).
 *
 *          **Fallback (CONFIG_K_TIMER_DISPATCH_ISR=n):** Callback runs
 *          on the ESP_TIMER_TASK, a normal FreeRTOS task. Blocking calls
 *          are permitted. This diverges from upstream Zephyr.
 *
 *          **Linux target:** CONFIG_K_TIMER_DISPATCH_ISR is unavailable
 *          (esp_timer is headers-only on linux). Callbacks run on a
 *          dedicated FreeRTOS dispatcher task (see k_timer_linux.c) --
 *          task-context semantics, same as the fallback above. Resolution
 *          is one FreeRTOS tick (1ms at the Boreas default of 1000 Hz).
 */
typedef void (*k_timer_expiry_t)(struct k_timer *timer);

/**
 * Timer stop function type. Runs in the context of the caller of
 * k_timer_stop() (typically a normal task on Boreas).
 */
typedef void (*k_timer_stop_t)(struct k_timer *timer);

struct k_timer {
	/* On linux, esp_timer is headers-only: the type exists but no timer
	 * is ever created. The linux backend keeps `handle` as the shared
	 * initialized/uninitialized sentinel (k_timer_start and k_thread
	 * abort-safety both test handle == NULL) by setting it to a dummy
	 * non-NULL value in k_timer_init. */
	esp_timer_handle_t handle;
	k_timer_expiry_t expiry_fn;
	k_timer_stop_t stop_fn;
	void *user_data;
	uint32_t status; /* expiry count since last status read */
	/* status_sync wake latch (binary sem, upstream's single-waiter
	 * model): given after each expiry and by k_timer_stop. The COUNT
	 * lives in `status`; the sem only carries the wake. */
	struct k_sem sync_sem;
	bool running;
	bool is_periodic;            /* last k_timer_start was periodic; one-shot if false */
	bool first_interval_pending; /* start-once then switch to periodic */
	uint64_t period_us;          /* stored for deferred periodic start */
#if CONFIG_IDF_TARGET_LINUX
	sys_dnode_t node;     /* armed-list linkage (k_timer_linux.c dispatcher) */
	uint64_t deadline_us; /* next expiry, z_uptime_us() clock domain */
#endif
};

#define K_TIMER_DEFINE(name, _expiry_fn, _stop_fn)                                                 \
	struct k_timer name = {                                                                    \
		.expiry_fn = (_expiry_fn),                                                         \
		.stop_fn = (_stop_fn),                                                             \
		.sync_sem = {.count = 0,                                                           \
			     .limit = 1,                                                           \
			     .waiters = SYS_DLIST_STATIC_INIT(&name.sync_sem.waiters),             \
			     .lock = portMUX_INITIALIZER_UNLOCKED},                                \
	}

void k_timer_init(struct k_timer *timer, k_timer_expiry_t expiry_fn, k_timer_stop_t stop_fn);
/**
 * @brief Start or restart a timer.
 *
 * @param timer   Timer to start.
 * @param duration Time until the first expiry.
 * @param period  Repeat interval after the first expiry (K_NO_WAIT for one-shot).
 *
 * @warning Must not be called while the timer's expiry callback is
 *          executing (same constraint as upstream Zephyr). With
 *          CONFIG_K_TIMER_DISPATCH_ISR=y, esp_timer_stop does not
 *          synchronize with an in-flight ISR callback on SMP targets.
 *          On linux, k_timer_stop likewise does not synchronize with a
 *          callback the dispatcher task has already dequeued.
 */
void k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period);
void k_timer_stop(struct k_timer *timer);

/** Read and reset the timer expiration count since last read.
 *  Non-blocking. Returns 0 if no expirations since last call. */
uint32_t k_timer_status_get(struct k_timer *timer);

/**
 * Block until the timer's expiry count is non-zero, or the timer is
 * stopped. Returns the count and resets it to zero atomically.
 *
 * Returns immediately if the count is already non-zero, or if the
 * timer is already stopped.
 *
 * @note Must not be called from an interrupt handler (blocks) --
 *       matches upstream.
 *
 * @note Blocks on the timer's embedded semaphore, woken by expiry or
 *       k_timer_stop (upstream's single-waiter wait-queue model: one
 *       thread waits per timer). Like upstream, a thread blocked here
 *       must not be aborted (see the @note on k_sem_take).
 */
uint32_t k_timer_status_sync(struct k_timer *timer);

/** Time remaining until the next expiry, in milliseconds. Returns 0
 *  if the timer is stopped or has already expired. */
uint32_t k_timer_remaining_get(struct k_timer *timer);

/**
 * @brief Get time remaining before the next timer expiry, in FreeRTOS ticks.
 *
 * @param timer The timer to query.
 * @return Remaining ticks until the next expiry, or 0 if the timer is
 *         stopped or has already expired.
 */
k_ticks_t k_timer_remaining_ticks(const struct k_timer *timer);

/**
 * @brief Get the absolute uptime (in FreeRTOS ticks since boot) at which
 *        the timer will next expire.
 *
 * @param timer The timer to query.
 * @return Absolute tick count of the next expiry, or the current uptime
 *         in ticks if the timer is not running. Matches upstream Zephyr's
 *         contract.
 *
 * @warning When the timer is stopped, this returns the CURRENT uptime --
 *          NOT zero. Callers checking for "no expiry pending" should use
 *          k_timer_remaining_ticks(), which returns 0 if the timer is
 *          stopped or has already expired.
 */
k_ticks_t k_timer_expires_ticks(const struct k_timer *timer);

static ALWAYS_INLINE void k_timer_user_data_set(struct k_timer *timer, void *user_data)
{
	timer->user_data = user_data;
}

static ALWAYS_INLINE void *k_timer_user_data_get(const struct k_timer *timer)
{
	return timer->user_data;
}

/* ----------------------------------------------------------------
 * Work Queue
 *
 * Backed by an intrusive sys_dlist_t of pending work items + a
 * counting k_sem to wake the worker thread. Mutations are guarded
 * by a per-queue spinlock (portMUX_TYPE), ISR-safe.
 *
 * k_work_cancel synchronously removes a queued item from the list
 * (O(1) via sys_dlist_remove) -- matches upstream Zephyr's contract.
 * ---------------------------------------------------------------- */

struct k_work;
struct k_work_q;
typedef void (*k_work_handler_t)(struct k_work *work);

/* Work item flag bit indices. Mirror upstream Zephyr's bit ordering
 * (zephyr/include/zephyr/kernel.h K_WORK_*_BIT enum) so that any
 * code that serializes or compares against bit numbers is portable
 * between Boreas and upstream. */
enum {
	K_WORK_RUNNING_BIT = 0,
	K_WORK_CANCELING_BIT = 1,
	K_WORK_QUEUED_BIT = 2,
	K_WORK_DELAYED_BIT = 3,
	K_WORK_FLUSHING_BIT = 4,
};

/* Work item flag masks. Inspect via k_work_busy_get().
 * K_WORK_DELAYED and K_WORK_FLUSHING are defined for upstream parity
 * but Boreas does not currently set them: */
#define K_WORK_RUNNING   BIT(K_WORK_RUNNING_BIT)
#define K_WORK_CANCELING BIT(K_WORK_CANCELING_BIT)
#define K_WORK_QUEUED    BIT(K_WORK_QUEUED_BIT)
#define K_WORK_DELAYED   BIT(K_WORK_DELAYED_BIT)
#define K_WORK_FLUSHING  BIT(K_WORK_FLUSHING_BIT)

struct k_work_sync {
	struct k_sem sem;
};

struct k_work {
	k_work_handler_t handler;
	sys_dnode_t node;         /* queue linkage */
	uint32_t flags;           /* K_WORK_QUEUED | RUNNING | CANCELING | ... */
	struct k_work_q *queue;   /* set while QUEUED, cleared on completion */
	struct k_work_sync *sync; /* non-NULL when a flush is pending */
};

/**
 * Work queue configuration. Mirrors upstream Zephyr's
 * struct k_work_queue_config.
 *
 * @param name       Thread name for diagnostics. May be NULL.
 * @param no_yield   If true, the worker does not yield between work
 *                   items. Lets a high-priority worker drain its
 *                   queue without preemption, at the cost of
 *                   responsiveness for equal-or-lower-priority work.
 * @param essential  Currently ignored on Boreas; reserved for parity
 *                   with upstream.
 */
struct k_work_queue_config {
	const char *name;
	bool no_yield;
	bool essential;
};

struct k_work_q {
	sys_dlist_t pending; /* head of pending k_work list */
	struct k_sem sem;    /* counted: 1 per pending submit */
	portMUX_TYPE lock;   /* protects `pending` mutations */
	StaticTask_t tcb;
	TaskHandle_t thread;
	const char *name;
	uint32_t flags; /* internal queue state, not user-visible */
	bool no_yield;
};

/** Static initializer for an embedded struct k_work field. */
#define K_WORK_INIT(_handler)                                                                      \
	{                                                                                          \
		.handler = (_handler),                                                             \
	}

#define K_WORK_DEFINE(name, _handler) struct k_work name = K_WORK_INIT(_handler)

void k_work_init(struct k_work *work, k_work_handler_t handler);

/**
 * Submit a work item to a queue. Return values match upstream Zephyr:
 *
 * @retval 0 if already queued (no change)
 * @retval 1 if it was idle and has been queued
 * @retval 2 if it was running and has been queued again
 * @retval -EBUSY if a cancellation of @p work is in progress (see
 *         k_work_cancel_sync())
 * @retval -EINVAL if @p work has no handler (upstream asserts on this
 *         instead of returning)
 * @retval -ENODEV if the queue has not been started
 *
 * @note The "was running" detection (retval 2) is a snapshot taken
 *       under the queue lock; like upstream's under-lock decision it
 *       may be stale by the time the caller acts on it.
 * @note Divergence: a running item submitted to a DIFFERENT queue is
 *       queued there as requested -- upstream instead redirects it to
 *       the queue that is running it (preventing parallel execution
 *       of the same item). Don't submit a possibly-running item to
 *       another queue.
 */
int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work);
/** Submit to the system work queue; return values as
 *  k_work_submit_to_queue(). */
int k_work_submit(struct k_work *work);

/**
 * Cancel a pending work item without waiting.
 *
 * Synchronously removes a queued instance from its queue's pending
 * list (O(1) via sys_dlist_remove) -- including an instance that was
 * queued again while a previous one is still running. Does not
 * interrupt a running handler; use k_work_cancel_sync() to wait for
 * completion.
 *
 * @note If a flush waiter (k_work_flush / k_work_cancel_sync) is
 *       attached when a queued, not-running item is cancelled, the
 *       waiter is released here -- the cancelled item will never run,
 *       so the worker can no longer signal it.
 *
 * @return the k_work_busy_get() state after the cancellation steps
 *         complete (upstream parity): 0 if the item is now idle;
 *         K_WORK_RUNNING and/or K_WORK_CANCELING may remain set.
 */
int k_work_cancel(struct k_work *work);
bool k_work_is_pending(struct k_work *work);

/**
 * Block until @p work has completed (or returns immediately if not
 * pending).
 *
 * @note Only one flush may be in flight per work item at a time --
 *       k_work_flush and the cancel_sync functions share the
 *       work->sync slot. A concurrent second call overwrites the
 *       first waiter's sync pointer, leaving the first caller blocked
 *       forever. Boreas does not implement upstream Zephyr's
 *       wait-queue model.
 */
int k_work_flush(struct k_work *work, struct k_work_sync *sync);

/**
 * Cancel a work item and wait for any in-flight handler to complete.
 *
 * While the cancellation is in progress, submission of this item is
 * rejected with -EBUSY (including from the delayable timer-expiry
 * path), so a handler re-submitting itself cannot survive the cancel.
 * On return the item is idle unless something submits it afterwards.
 *
 * @note See the single-waiter limitation on k_work_flush().
 * @note Must not be called from the work queue thread running @p work.
 *
 * @retval true if the work was pending (a queued/running instance was
 *         cancelled or waited out)
 * @retval false if it was already idle
 */
bool k_work_cancel_sync(struct k_work *work, struct k_work_sync *sync);

/** Snapshot of the raw work item flags mask. Boreas sets only
 *  K_WORK_RUNNING, K_WORK_CANCELING, and K_WORK_QUEUED; K_WORK_DELAYED
 *  and K_WORK_FLUSHING exist for upstream source compatibility but are
 *  never set (see the mask definitions above). */
uint32_t k_work_busy_get(const struct k_work *work);

/**
 * Start a work queue thread.
 *
 * Mirrors upstream Zephyr signature. Caller provides the stack
 * (typically via K_THREAD_STACK_DEFINE) and a config struct.
 *
 * @param queue       Queue object (zero-initialized struct k_work_q).
 * @param stack       Stack buffer (typically K_THREAD_STACK_DEFINE).
 * @param stack_size  Size of @p stack in bytes.
 * @param prio        FreeRTOS priority.
 * @param cfg         Optional configuration (name, no_yield, essential).
 *                    May be NULL.
 */
void k_work_queue_start(struct k_work_q *queue, k_thread_stack_t *stack, size_t stack_size,
			int prio, const struct k_work_queue_config *cfg);

/**
 * Block until all pending work on @p queue has completed.
 *
 * Submits a sentinel work item and waits for it to run, which
 * guarantees every earlier item submitted *before* the drain call
 * has finished. Items submitted *during* drain are NOT guaranteed
 * to be drained -- they race normally with the sentinel.
 *
 * @param queue  Queue to drain.
 * @param plug   Currently ignored on Boreas; reserved for upstream
 *               parity. Upstream Zephyr blocks new submissions until
 *               k_work_queue_unplug() is called; Boreas does not
 *               implement plugging.
 *
 * @return 0 on success; negative errno on failure.
 */
int k_work_queue_drain(struct k_work_q *queue, bool plug);

/**
 * System work queue.
 *
 * Auto-initialized before main() via constructor. Priority and stack
 * size come from CONFIG_SYSTEM_WORKQUEUE_PRIORITY /
 * CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE (Kconfig).
 *
 * @note ESP-IDF Xtensa: as with K_SEM_DEFINE'd sems, the system
 *       workqueue is NOT guaranteed ready inside arbitrary user
 *       constructors -- ESP-IDF iterates .init_array in descending
 *       order, and within a TU the LAST-declared constructor runs
 *       first. The queue IS ready by app_main() and SYS_INIT
 *       callbacks. k_work_submit() returns -ENODEV until then.
 */
extern struct k_work_q k_sys_work_q;

/* ----------------------------------------------------------------
 * Delayable Work
 * ---------------------------------------------------------------- */

struct k_work_delayable {
	struct k_work work;
	struct k_timer timer;
	struct k_work_q *queue; /* target queue (NULL = system queue) */
};

/* BEHAVIORAL DELTA: Unlike Zephyr, this macro does NOT produce a ready-to-use
 * item. The embedded k_timer wraps esp_timer_handle_t which requires a runtime
 * esp_timer_create() call. You MUST call k_work_init_delayable() before the
 * first k_work_schedule() / k_work_reschedule(). */
#define K_WORK_DELAYABLE_DEFINE(name, _handler)                                                    \
	struct k_work_delayable name = {                                                           \
		.work = {.handler = (_handler)},                                                   \
	}

void k_work_init_delayable(struct k_work_delayable *dwork, k_work_handler_t handler);

/**
 * Schedule delayable work on a queue, unless it is already scheduled.
 * Return values match upstream Zephyr:
 *
 * @retval 0 if already delayed, queued, or canceling (no change). An
 *         item that is only RUNNING is scheduled (the next occurrence
 *         may be scheduled while the current one executes).
 * @retval 1 if the timeout was armed
 * @retval 2 if @p delay is K_NO_WAIT and the work was running and has
 *         been queued again
 * @retval -EBUSY / -EINVAL / -ENODEV propagated from the K_NO_WAIT submit path
 *         (see k_work_submit_to_queue())
 *
 * @note Unlike upstream (which serializes the decision under a global
 *       work lock), scheduling or rescheduling the same delayable
 *       concurrently from multiple contexts is not synchronized and
 *       must not be done.
 */
int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *dwork,
			      k_timeout_t delay);
/** Schedule on the system work queue; return values as
 *  k_work_schedule_for_queue(). */
int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);

/**
 * Cancel any existing schedule and (re)schedule. Return values match
 * upstream Zephyr:
 *
 * @retval 1 if the timeout was armed
 * @retval 2 if @p delay is K_NO_WAIT and the work was running and has
 *         been queued again
 * @retval 0 if @p delay is K_NO_WAIT and the queued instance could
 *         not be removed (cancel is best-effort across queues; see
 *         k_work_cancel()) -- nothing changed
 * @retval -EBUSY / -EINVAL / -ENODEV propagated from the K_NO_WAIT submit path
 *         (see k_work_submit_to_queue())
 */
int k_work_reschedule_for_queue(struct k_work_q *queue, struct k_work_delayable *dwork,
				k_timeout_t delay);
/** Reschedule on the system work queue; return values as
 *  k_work_reschedule_for_queue(). */
int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
/**
 * Cancel delayable work without waiting: stops a pending timeout and
 * removes a queued instance. Does not wait for a running handler.
 *
 * @return the k_work_busy_get() state after the cancellation steps
 *         complete (upstream parity): 0 if now idle; K_WORK_RUNNING
 *         and/or K_WORK_CANCELING may remain set.
 */
int k_work_cancel_delayable(struct k_work_delayable *dwork);

/**
 * Cancel delayable work and wait until it cannot run again: stops a
 * pending timeout, removes a queued instance, and waits out a running
 * handler. While the cancellation is in progress, submission is
 * rejected with -EBUSY (including the timer-expiry path), and any
 * timeout a handler re-arms during the cancel is stopped before
 * returning -- so a self-rescheduling handler is reliably stopped
 * with this one call.
 *
 * @note See the single-waiter limitation on k_work_flush().
 * @note Must not be called from the work queue thread running the
 *       work item.
 *
 * @retval true if the work was pending (scheduled, queued, or running)
 * @retval false if it was already idle
 */
bool k_work_cancel_delayable_sync(struct k_work_delayable *dwork, struct k_work_sync *sync);

bool k_work_delayable_is_pending(struct k_work_delayable *dwork);
int64_t k_work_delayable_remaining_get(struct k_work_delayable *dwork);

/* ----------------------------------------------------------------
 * Thread
 * ---------------------------------------------------------------- */

typedef void (*k_thread_entry_t)(void *p1, void *p2, void *p3);

struct k_thread {
	TaskHandle_t handle;
	StaticTask_t tcb;
	StackType_t *stack;
	uint32_t stack_size;
	const char *name;
	k_thread_entry_t entry;
	void *p1;
	void *p2;
	void *p3;
	bool _start_suspended;       /* self-suspend before entry */
	bool _completed;             /* entry returned (__atomic_* access only) */
	struct k_timer _delay_timer; /* used for finite-delay start */
};

/* Priority helpers -- map to FreeRTOS priority scheme.
 * FreeRTOS does not distinguish cooperative vs preemptible threads,
 * so both macros map identically (higher number = higher priority).
 * In real Zephyr, cooperative threads have negative priority values. */
#define K_PRIO_PREEMPT(p) (configMAX_PRIORITIES - 1 - (p))
#define K_PRIO_COOP(p)    (configMAX_PRIORITIES - 1 - (p))

#define K_THREAD_STACK_DEFINE(name, size) static StackType_t name[size / sizeof(StackType_t)]

/** Define a thread stack in PSRAM (external SPI RAM).
 *  Requires CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y.
 *  The k_thread struct (containing the TCB) remains in internal DRAM. */
#define K_THREAD_STACK_DEFINE_PSRAM(name, size)                                                    \
	static StackType_t                                                                         \
		__attribute__((aligned(16))) EXT_RAM_BSS_ATTR name[(size) / sizeof(StackType_t)]

#define K_THREAD_STACK_SIZEOF(stack) (sizeof(stack))

typedef TaskHandle_t k_tid_t;

/**
 * Create and start (or defer start of) a thread. Returns the thread
 * ID. Matches upstream Zephyr's return type and "always returns a
 * valid tid" contract: failure of the underlying xTaskCreateStatic
 * (only possible on programmer error -- misaligned stack, etc.)
 * triggers __ASSERT rather than returning NULL.
 *
 * @note Fire-and-forget threads (never joined or aborted) must use
 *       static or otherwise permanent storage for @p thread and
 *       @p stack: after the entry function returns, the kernel still
 *       references both until k_thread_join() or k_thread_abort()
 *       reclaims the task (the completed task parks suspended on
 *       silicon; on linux its TCB awaits idle-task reaping). The
 *       storage may be reused or freed only after join/abort returns.
 */
k_tid_t k_thread_create(struct k_thread *thread, StackType_t *stack, size_t stack_size,
			k_thread_entry_t entry, void *p1, void *p2, void *p3, int prio,
			uint32_t options, k_timeout_t delay);
void k_thread_name_set(struct k_thread *thread, const char *name);
/**
 * @brief Abort a thread (deletes the underlying FreeRTOS task).
 *
 * @note On the linux target this blocks for two ticks after deletion:
 *       the POSIX port defers pthread teardown to the idle task, which
 *       dereferences the TCB and per-task port state -- the caller's
 *       k_thread struct and stack must stay valid until that cleanup
 *       runs. The window is best-effort (sufficient for host test
 *       runners, not guaranteed under idle starvation). Upstream
 *       Zephyr's k_thread_abort does not block this way; on silicon
 *       reclamation is synchronous and does not block.
 *
 * @note Divergence: do not abort a thread that is blocked in
 *       k_sem_take (or inside k_sem_give) -- upstream unpends aborted
 *       threads from wait queues; Boreas cannot reach into the
 *       notification-backed semaphore's waiter list from here (see
 *       the @note on k_sem_take).
 */
void k_thread_abort(struct k_thread *thread);
void k_thread_suspend(struct k_thread *thread);
void k_thread_resume(struct k_thread *thread);
/**
 * @brief Wait for a thread's entry function to return.
 *
 * @note Implemented by polling (FreeRTOS has no native join), so
 *       wakeup granularity is ~10 ms rather than upstream Zephyr's
 *       immediate wake. On completion the task is reclaimed before
 *       returning: on silicon it is deleted synchronously from this
 *       context, so the caller-owned-memory guarantee is
 *       deterministic. On the linux target join instead blocks two
 *       ticks so the POSIX port's idle task can reap the pthread
 *       while the caller's k_thread struct and stack are still valid
 *       (see k_thread_abort) -- a best-effort window, sufficient for
 *       host test runners, NOT a guarantee under idle starvation.
 *
 * @note Unlike upstream Zephyr, joining the same thread from multiple
 *       contexts concurrently (or racing a join against an abort) is
 *       not synchronized and must not be done.
 */
int k_thread_join(struct k_thread *thread, k_timeout_t timeout);
int k_thread_stack_space_get(struct k_thread *thread, size_t *unused);

static inline k_tid_t k_current_get(void)
{
	return xTaskGetCurrentTaskHandle();
}

#ifdef __cplusplus
}
#endif
