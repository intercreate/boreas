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

static inline void k_sleep(k_timeout_t timeout)
{
	if (k_timeout_is_no_wait(timeout)) {
		taskYIELD();
	} else if (k_timeout_is_forever(timeout)) {
		vTaskDelay(portMAX_DELAY);
	} else {
		vTaskDelay(k_timeout_to_ticks(timeout));
	}
}

static inline void k_usleep(int32_t us)
{
	/* FreeRTOS tick resolution limits this; sub-ms sleeps busy-wait */
	if (us >= 1000) {
		vTaskDelay(pdMS_TO_TICKS(us / 1000));
	} else if (us > 0) {
		esp_rom_delay_us(us);
	}
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

#define K_SEM_DEFINE(name, initial, limit)                                                         \
	struct k_sem name = {0}; /* call k_sem_init at runtime                                     \
				  */

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
uint32_t k_event_set(struct k_event *event, uint32_t events);
uint32_t k_event_clear(struct k_event *event, uint32_t events);
uint32_t k_event_wait(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout);
uint32_t k_event_wait_all(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout);

/* ----------------------------------------------------------------
 * Timer (over esp_timer)
 *
 * BEHAVIORAL DELTA: Timer expiry callbacks run in the ESP_TIMER_TASK
 * context (a normal FreeRTOS task), not in ISR context as in Zephyr.
 * This means callbacks CAN call blocking APIs, but they ARE
 * preemptible by higher-priority tasks.
 * ---------------------------------------------------------------- */

struct k_timer;
typedef void (*k_timer_expiry_t)(struct k_timer *timer);
typedef void (*k_timer_stop_t)(struct k_timer *timer);

struct k_timer {
	esp_timer_handle_t handle;
	k_timer_expiry_t expiry_fn;
	k_timer_stop_t stop_fn;
	void *user_data;
	uint32_t status; /* expiry count since last status read */
	bool running;
	bool first_interval_pending; /* start-once then switch to periodic */
	uint64_t period_us;          /* stored for deferred periodic start */
};

#define K_TIMER_DEFINE(name, _expiry_fn, _stop_fn)                                                 \
	struct k_timer name = {                                                                    \
		.expiry_fn = (_expiry_fn),                                                         \
		.stop_fn = (_stop_fn),                                                             \
	}

void k_timer_init(struct k_timer *timer, k_timer_expiry_t expiry_fn, k_timer_stop_t stop_fn);
void k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period);
void k_timer_stop(struct k_timer *timer);

/** Read and reset the timer expiration count since last read.
 *  Non-blocking. Returns 0 if no expirations since last call. */
uint32_t k_timer_status_get(struct k_timer *timer);

/** Block until the timer next expires, then return and reset the count.
 *  Returns 0 immediately if the timer is stopped. */
uint32_t k_timer_status_sync(struct k_timer *timer);

int64_t k_timer_remaining_get(struct k_timer *timer);
void k_timer_user_data_set(struct k_timer *timer, void *user_data);
void *k_timer_user_data_get(struct k_timer *timer);

/* ----------------------------------------------------------------
 * Work Queue
 * ---------------------------------------------------------------- */

struct k_work;
typedef void (*k_work_handler_t)(struct k_work *work);

struct k_work_sync {
	struct k_sem sem;
};

struct k_work {
	k_work_handler_t handler;
	sys_dnode_t node; /* queue linkage */
	uint32_t flags;
	struct k_work_sync *sync; /* non-NULL when a flush is pending */
};

struct k_work_queue {
	QueueHandle_t queue;
	StaticQueue_t queue_buffer;
	TaskHandle_t thread;
	StaticTask_t tcb;
	StackType_t *stack;
	uint8_t *storage;
	uint32_t depth;
	uint32_t stack_size;
	const char *name;
};

#define K_WORK_DEFINE(name, _handler)                                                              \
	struct k_work name = {                                                                     \
		.handler = (_handler),                                                             \
	}

/**
 * Statically define a work queue with its own storage.
 *
 * @param _name  Variable name for the work queue.
 * @param _depth Maximum number of pending work items.
 * @param _stack_size Stack size in bytes for the worker thread.
 */
#define K_WORK_QUEUE_DEFINE(_name, _depth, _stack_size)                                            \
	static uint8_t _k_wq_storage_##_name[(_depth) * sizeof(struct k_work *)];                  \
	static StackType_t _k_wq_stack_##_name[(_stack_size) / sizeof(StackType_t)];               \
	struct k_work_queue _name = {                                                              \
		.storage = _k_wq_storage_##_name,                                                  \
		.stack = _k_wq_stack_##_name,                                                      \
		.depth = (_depth),                                                                 \
		.stack_size = (_stack_size),                                                       \
	}

void k_work_init(struct k_work *work, k_work_handler_t handler);
int k_work_submit(struct k_work *work);
int k_work_submit_to_queue(struct k_work_queue *queue, struct k_work *work);

/**
 * Cancel a pending work item.
 *
 * Clears the QUEUED flag so the handler will not run. However, the item
 * cannot be removed from the underlying FreeRTOS queue -- it will be
 * dequeued and silently discarded by the worker thread. This means a
 * cancelled item still occupies a queue slot until it is drained.
 *
 * Cannot cancel a work item that is currently running.
 *
 * @return true if cancelled, false if the item is currently running.
 */
bool k_work_cancel(struct k_work *work);
bool k_work_is_pending(struct k_work *work);
int k_work_flush(struct k_work *work, struct k_work_sync *sync);
int k_work_cancel_sync(struct k_work *work, struct k_work_sync *sync);

void k_work_queue_init(struct k_work_queue *queue);
void k_work_queue_start(struct k_work_queue *queue, const char *name, uint32_t stack_size,
			int prio);

/**
 * System work queue.
 *
 * Auto-initialized before main() via constructor. Ready to use
 * from app_main(), SYS_INIT callbacks, and other constructors that
 * run after zkernel's constructor. k_work_queue_start() is idempotent,
 * so explicit init calls are safe but unnecessary.
 */
extern struct k_work_queue k_sys_work_q;

/* ----------------------------------------------------------------
 * Delayable Work
 * ---------------------------------------------------------------- */

struct k_work_delayable {
	struct k_work work;
	struct k_timer timer;
	struct k_work_queue *queue; /* target queue (NULL = system queue) */
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
int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);
int k_work_schedule_for_queue(struct k_work_queue *queue, struct k_work_delayable *dwork,
			      k_timeout_t delay);
int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
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

void k_thread_create(struct k_thread *thread, StackType_t *stack, size_t stack_size,
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
