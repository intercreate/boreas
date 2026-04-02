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
    return esp_timer_get_time() / 1000;
}

/** Get system uptime in 32-bit milliseconds (wraps). */
static inline uint32_t k_uptime_get_32(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
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
    if (!k_timeout_is_forever(timeout) && !k_timeout_is_no_wait(timeout)) {
        vTaskDelay(k_timeout_to_ticks(timeout));
    } else if (k_timeout_is_forever(timeout)) {
        vTaskDelay(portMAX_DELAY);
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

#define K_SEM_DEFINE(name, initial, limit) \
    struct k_sem name = {0}; /* call k_sem_init at runtime */

int  k_sem_init(struct k_sem *sem, unsigned int initial_count,
                unsigned int limit);
int  k_sem_take(struct k_sem *sem, k_timeout_t timeout);
void k_sem_give(struct k_sem *sem);
void k_sem_reset(struct k_sem *sem);
unsigned int k_sem_count_get(struct k_sem *sem);

/* ----------------------------------------------------------------
 * Mutex
 * ---------------------------------------------------------------- */

struct k_mutex {
    SemaphoreHandle_t handle;
    StaticSemaphore_t buffer;
#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
    uint8_t  order;     /* lock ordering -- lower must be acquired first */
    uint32_t lock_time; /* tick count at lock acquisition */
#endif
};

#define K_MUTEX_DEFINE(name) \
    struct k_mutex name = {0}

int  k_mutex_init(struct k_mutex *mutex);
int  k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
int  k_mutex_unlock(struct k_mutex *mutex);

#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
int  k_mutex_init_ordered(struct k_mutex *mutex, uint8_t order);
#define K_MUTEX_DEFINE_ORDERED(name, ord) \
    struct k_mutex name = {.order = (ord)}
#endif

/* ----------------------------------------------------------------
 * Message Queue
 * ---------------------------------------------------------------- */

struct k_msgq {
    QueueHandle_t   handle;
    StaticQueue_t   buffer;
    uint8_t        *storage;
    size_t          msg_size;
    uint32_t        max_msgs;
};

#define K_MSGQ_DEFINE(name, _msg_size, _max_msgs, _align) \
    static uint8_t __attribute__((aligned(_align))) \
        _k_msgq_buf_##name[(_msg_size) * (_max_msgs)]; \
    struct k_msgq name = { \
        .storage = _k_msgq_buf_##name, \
        .msg_size = (_msg_size), \
        .max_msgs = (_max_msgs), \
    }

int  k_msgq_init(struct k_msgq *msgq, char *buffer, size_t msg_size,
                 uint32_t max_msgs);
int  k_msgq_put(struct k_msgq *msgq, const void *data, k_timeout_t timeout);
int  k_msgq_get(struct k_msgq *msgq, void *data, k_timeout_t timeout);
int  k_msgq_peek(struct k_msgq *msgq, void *data);
void k_msgq_purge(struct k_msgq *msgq);
uint32_t k_msgq_num_used_get(struct k_msgq *msgq);
uint32_t k_msgq_num_free_get(struct k_msgq *msgq);

/* ----------------------------------------------------------------
 * Event
 * ---------------------------------------------------------------- */

struct k_event {
    EventGroupHandle_t handle;
    StaticEventGroup_t buffer;
};

#define K_EVENT_DEFINE(name) \
    struct k_event name = {0}

int      k_event_init(struct k_event *event);
uint32_t k_event_post(struct k_event *event, uint32_t events);
uint32_t k_event_set(struct k_event *event, uint32_t events);
uint32_t k_event_clear(struct k_event *event, uint32_t events);
uint32_t k_event_wait(struct k_event *event, uint32_t events,
                      bool reset, k_timeout_t timeout);
uint32_t k_event_wait_all(struct k_event *event, uint32_t events,
                          bool reset, k_timeout_t timeout);

/* ----------------------------------------------------------------
 * Timer (over esp_timer)
 * ---------------------------------------------------------------- */

struct k_timer;
typedef void (*k_timer_expiry_t)(struct k_timer *timer);
typedef void (*k_timer_stop_t)(struct k_timer *timer);

struct k_timer {
    esp_timer_handle_t handle;
    k_timer_expiry_t   expiry_fn;
    k_timer_stop_t     stop_fn;
    void              *user_data;
    uint32_t           status;     /* expiry count since last status read */
    bool               running;
};

#define K_TIMER_DEFINE(name, _expiry_fn, _stop_fn) \
    struct k_timer name = { \
        .expiry_fn = (_expiry_fn), \
        .stop_fn = (_stop_fn), \
    }

void     k_timer_init(struct k_timer *timer, k_timer_expiry_t expiry_fn,
                      k_timer_stop_t stop_fn);
void     k_timer_start(struct k_timer *timer, k_timeout_t duration,
                       k_timeout_t period);
void     k_timer_stop(struct k_timer *timer);
uint32_t k_timer_status_get(struct k_timer *timer);
uint32_t k_timer_status_sync(struct k_timer *timer);
int64_t  k_timer_remaining_get(struct k_timer *timer);
void     k_timer_user_data_set(struct k_timer *timer, void *user_data);
void    *k_timer_user_data_get(struct k_timer *timer);

/* ----------------------------------------------------------------
 * Work Queue
 * ---------------------------------------------------------------- */

struct k_work;
typedef void (*k_work_handler_t)(struct k_work *work);

struct k_work {
    k_work_handler_t handler;
    sys_dnode_t      node;     /* queue linkage */
    uint32_t         flags;
};

struct k_work_queue {
    QueueHandle_t  queue;
    StaticQueue_t  queue_buffer;
    TaskHandle_t   thread;
    const char    *name;
};

#define K_WORK_DEFINE(name, _handler) \
    struct k_work name = { \
        .handler = (_handler), \
    }

void k_work_init(struct k_work *work, k_work_handler_t handler);
int  k_work_submit(struct k_work *work);
int  k_work_submit_to_queue(struct k_work_queue *queue, struct k_work *work);
bool k_work_cancel(struct k_work *work);
bool k_work_is_pending(struct k_work *work);

void k_work_queue_init(struct k_work_queue *queue);
void k_work_queue_start(struct k_work_queue *queue, const char *name,
                        uint32_t stack_size, int prio);

/* System work queue -- initialized at boot */
extern struct k_work_queue k_sys_work_q;

/* ----------------------------------------------------------------
 * Delayable Work
 * ---------------------------------------------------------------- */

struct k_work_delayable {
    struct k_work  work;
    struct k_timer timer;
};

#define K_WORK_DELAYABLE_DEFINE(name, _handler) \
    struct k_work_delayable name = { \
        .work = {.handler = (_handler)}, \
    }

void k_work_init_delayable(struct k_work_delayable *dwork,
                           k_work_handler_t handler);
int  k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);
int  k_work_schedule_for_queue(struct k_work_queue *queue,
                               struct k_work_delayable *dwork,
                               k_timeout_t delay);
int  k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
int  k_work_cancel_delayable(struct k_work_delayable *dwork);
bool k_work_delayable_is_pending(struct k_work_delayable *dwork);
int64_t k_work_delayable_remaining_get(struct k_work_delayable *dwork);

/* ----------------------------------------------------------------
 * Thread
 * ---------------------------------------------------------------- */

typedef void (*k_thread_entry_t)(void *p1, void *p2, void *p3);

struct k_thread {
    TaskHandle_t  handle;
    StaticTask_t  tcb;
    StackType_t  *stack;
    uint32_t      stack_size;
    const char   *name;
};

/* Priority helpers -- map to FreeRTOS priority scheme */
#define K_PRIO_PREEMPT(p) (configMAX_PRIORITIES - 1 - (p))
#define K_PRIO_COOP(p)    (configMAX_PRIORITIES - 1 - (p))

#define K_THREAD_STACK_DEFINE(name, size) \
    static StackType_t name[size / sizeof(StackType_t)]

#define K_THREAD_STACK_SIZEOF(stack) (sizeof(stack))

void k_thread_create(struct k_thread *thread, StackType_t *stack,
                     size_t stack_size, k_thread_entry_t entry,
                     void *p1, void *p2, void *p3,
                     int prio, uint32_t options, k_timeout_t delay);
void k_thread_name_set(struct k_thread *thread, const char *name);
void k_thread_abort(struct k_thread *thread);
int  k_thread_join(struct k_thread *thread, k_timeout_t timeout);
int  k_thread_stack_space_get(struct k_thread *thread, size_t *unused);

#ifdef __cplusplus
}
#endif
