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
#include <sys/time.h>
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

/** Get system uptime in milliseconds (monotonic). */
static inline int64_t k_uptime_get(void)
{
#if CONFIG_IDF_TARGET_LINUX
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#else
	return esp_timer_get_time() / 1000;
#endif
}

/** Get system uptime in 32-bit milliseconds (wraps). */
static inline uint32_t k_uptime_get_32(void)
{
	return (uint32_t)k_uptime_get();
}

/**
 * @brief Get system uptime in FreeRTOS-tick-period units, derived from
 *        the esp_timer microsecond clock. Mirrors upstream Zephyr's
 *        k_uptime_ticks().
 *
 * @return Tick count since boot, as k_ticks_t.
 *
 * @note Boreas implementation derives this from esp_timer (the same
 *       clock domain as k_uptime_get and k_timer_expires_ticks), NOT
 *       from xTaskGetTickCount. This keeps all "uptime"-flavored APIs
 *       in a single clock domain and makes
 *       `k_uptime_ticks() * portTICK_PERIOD_MS ~= k_uptime_get()`.
 *       Code that genuinely wants the FreeRTOS scheduler tick counter
 *       should call xTaskGetTickCount() directly.
 */
static inline k_ticks_t k_uptime_ticks(void)
{
	return (k_ticks_t)(esp_timer_get_time() / ((int64_t)portTICK_PERIOD_MS * 1000));
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

struct k_sem {
	SemaphoreHandle_t handle;
	StaticSemaphore_t buffer;
};

/**
 * Statically declare a semaphore and auto-initialize it with the
 * given @p initial count and @p limit before main() runs.
 *
 * Implementation note: FreeRTOS counting semaphores require a runtime
 * xSemaphoreCreateCountingStatic() call, so true compile-time init
 * isn't possible. The macro emits a per-instance constructor that
 * runs at startup.
 *
 * @note ESP-IDF Xtensa caveat: ESP-IDF iterates `.init_array` in
 *       DESCENDING order on Xtensa (see esp_system/startup.c
 *       do_global_ctors). Default-priority user constructors run
 *       BEFORE prioritized ones, and within a single TU the LAST-
 *       declared constructor runs first. Adding a constructor
 *       priority does not help -- prioritized constructors run
 *       AFTER unprioritized ones in the descending iteration. As a
 *       result the K_SEM_DEFINE'd sem is NOT guaranteed ready
 *       inside arbitrary user constructors on ESP-IDF Xtensa. It IS
 *       ready by:
 *         - app_main()
 *         - SYS_INIT() callbacks
 *         - constructors declared earlier in the same TU
 *           (textually) than the K_SEM_DEFINE
 *
 *       For sems consumed from constructor context, prefer manual
 *       k_sem_init() in a SYS_INIT callback.
 *
 * @note Archive-stripping: when this macro expands inside a static
 *       archive, the constructor can be stripped by the linker
 *       unless pulled in via WHOLE_ARCHIVE (idf_component_register
 *       WHOLE_ARCHIVE). User application code typically isn't in
 *       such an archive; library/component code is.
 */
/* Indirection helpers so __LINE__ expands before token-pasting. The
 * line-number-based ctor name avoids generating a file-scope
 * identifier that begins with underscore (reserved by C11 7.1.3) and
 * is robust to caller-supplied names that themselves begin with
 * underscore. Caveat: two K_SEM_DEFINE on the same source line will
 * collide; not expected in practice. */
#define K_SEM_CONCAT_(a, b)        a##b
#define K_SEM_CONCAT(a, b)         K_SEM_CONCAT_(a, b)
#define K_SEM_INIT_CTOR_NAME(line) K_SEM_CONCAT(k_sem_init_ctor_, line)

#define K_SEM_DEFINE(name, _initial, _limit)                                                       \
	struct k_sem name = {0};                                                                   \
	__attribute__((constructor)) static void K_SEM_INIT_CTOR_NAME(__LINE__)(void)              \
	{                                                                                          \
		k_sem_init(&name, (_initial), (_limit));                                           \
	}

int k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit);
int k_sem_take(struct k_sem *sem, k_timeout_t timeout);
void k_sem_give(struct k_sem *sem);
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
 *
 * BEHAVIORAL DELTA: FreeRTOS EventGroup reserves bits 24-31 for
 * internal use. Only bits 0-23 (24 bits) are available, vs
 * Zephyr's full 32 bits.
 * ---------------------------------------------------------------- */

struct k_event {
	EventGroupHandle_t handle;
	StaticEventGroup_t buffer;
};

#define K_EVENT_DEFINE(name) struct k_event name = {0}

int k_event_init(struct k_event *event);
uint32_t k_event_post(struct k_event *event, uint32_t events);
/** @note In ISR context, returns 0 on failure (timer command queue full)
 *        rather than the previous event state. FreeRTOS's
 *        xEventGroupSetBitsFromISR defers to the timer daemon and cannot
 *        report the prior bits synchronously. */
uint32_t k_event_set(struct k_event *event, uint32_t events);
uint32_t k_event_clear(struct k_event *event, uint32_t events);
uint32_t k_event_wait(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout);
uint32_t k_event_wait_all(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout);

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
 */
typedef void (*k_timer_expiry_t)(struct k_timer *timer);

/**
 * Timer stop function type. Runs in the context of the caller of
 * k_timer_stop() (typically a normal task on Boreas).
 */
typedef void (*k_timer_stop_t)(struct k_timer *timer);

struct k_timer {
	esp_timer_handle_t handle;
	k_timer_expiry_t expiry_fn;
	k_timer_stop_t stop_fn;
	void *user_data;
	uint32_t status; /* expiry count since last status read */
	bool running;
	bool is_periodic;            /* last k_timer_start was periodic; one-shot if false */
	bool first_interval_pending; /* start-once then switch to periodic */
	uint64_t period_us;          /* stored for deferred periodic start */
};

#define K_TIMER_DEFINE(name, _expiry_fn, _stop_fn)                                                 \
	struct k_timer name = {                                                                    \
		.expiry_fn = (_expiry_fn),                                                         \
		.stop_fn = (_stop_fn),                                                             \
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
 * @note Must not be called from an interrupt handler (blocks).
 *
 * @note Boreas implementation polls every k_msleep(1). Upstream
 *       Zephyr blocks the calling thread on a wait queue. Same
 *       caller-visible semantics; the wake granularity here is
 *       bounded by the FreeRTOS tick period (portTICK_PERIOD_MS,
 *       set by CONFIG_FREERTOS_HZ -- 1ms at the Boreas default of
 *       1000 Hz, 10ms at the ESP-IDF Kconfig default of 100 Hz).
 *       A wake can therefore be up to one tick period late.
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
int k_work_submit(struct k_work *work);
int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work);

/**
 * Cancel a pending work item.
 *
 * Synchronously removes the item from its queue's pending list (O(1)
 * via sys_dlist_remove). Cannot cancel a work item that is currently
 * running -- use k_work_cancel_sync() to wait for completion.
 *
 * @note If a flush waiter (k_work_flush / k_work_cancel_sync) is
 *       attached when a queued item is cancelled, the waiter is
 *       released here -- the cancelled item will never run, so
 *       the worker can no longer signal it.
 *
 * @return true if the item was cancelled (or was not queued), false
 *         if the item is currently running.
 */
bool k_work_cancel(struct k_work *work);
bool k_work_is_pending(struct k_work *work);

/**
 * Block until @p work has completed (or returns immediately if not
 * pending).
 *
 * @note Only one flush may be in flight per work item at a time --
 *       k_work_flush and k_work_cancel_sync share the work->sync
 *       slot. A concurrent second call overwrites the first
 *       waiter's sync pointer, leaving the first caller blocked
 *       forever. Boreas does not implement upstream Zephyr's
 *       wait-queue model.
 */
int k_work_flush(struct k_work *work, struct k_work_sync *sync);
int k_work_cancel_sync(struct k_work *work, struct k_work_sync *sync);

/** Snapshot of work item flags. Returns a mask of K_WORK_RUNNING,
 *  K_WORK_CANCELING, K_WORK_QUEUED, K_WORK_DELAYED, K_WORK_FLUSHING. */
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
 *       callbacks. k_work_submit() returns -EINVAL until then.
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
/** @note Boreas returns 0 on success; upstream Zephyr returns 1 (newly scheduled)
 * or 2 (running, requeued). Negative errno values are not returned. */
int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);
/** @note Boreas returns 0 on success; upstream Zephyr returns 1 (newly scheduled)
 * or 2 (running, requeued). Negative errno values are not returned. */
int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *dwork,
			      k_timeout_t delay);
/** @note Boreas returns 0 on success; upstream Zephyr returns 1 (newly scheduled)
 * or 2 (running, requeued). Negative errno values are not returned. */
int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
/** @note Boreas returns 0 on success; upstream Zephyr returns 1 (newly scheduled)
 * or 2 (running, requeued). Negative errno values are not returned. */
int k_work_reschedule_for_queue(struct k_work_q *queue, struct k_work_delayable *dwork,
				k_timeout_t delay);
int k_work_cancel_delayable(struct k_work_delayable *dwork);
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
 */
k_tid_t k_thread_create(struct k_thread *thread, StackType_t *stack, size_t stack_size,
			k_thread_entry_t entry, void *p1, void *p2, void *p3, int prio,
			uint32_t options, k_timeout_t delay);
void k_thread_name_set(struct k_thread *thread, const char *name);
void k_thread_abort(struct k_thread *thread);
void k_thread_suspend(struct k_thread *thread);
void k_thread_resume(struct k_thread *thread);
int k_thread_join(struct k_thread *thread, k_timeout_t timeout);
int k_thread_stack_space_get(struct k_thread *thread, size_t *unused);

static inline k_tid_t k_current_get(void)
{
	return xTaskGetCurrentTaskHandle();
}

#ifdef __cplusplus
}
#endif
