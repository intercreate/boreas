/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include "esp_log.h"

static const char *TAG = "k_work";

/* Work item flags */
#define K_WORK_QUEUED  BIT(0)
#define K_WORK_RUNNING BIT(1)

/* Default system work queue */
#define SYS_WQ_DEPTH 16
#define SYS_WQ_STACK_SIZE 4096
static uint8_t sys_wq_storage[SYS_WQ_DEPTH * sizeof(struct k_work *)];
static StackType_t sys_wq_stack[SYS_WQ_STACK_SIZE / sizeof(StackType_t)];

struct k_work_queue k_sys_work_q = {
    .storage = sys_wq_storage,
    .stack = sys_wq_stack,
    .depth = SYS_WQ_DEPTH,
    .stack_size = SYS_WQ_STACK_SIZE,
};
static bool sys_wq_initialized = false;

/* ----------------------------------------------------------------
 * Work Queue Thread
 * ---------------------------------------------------------------- */

static void k_work_queue_thread(void *p1)
{
    struct k_work_queue *queue = (struct k_work_queue *)p1;
    struct k_work *work;

    for (;;) {
        if (xQueueReceive(queue->queue, &work, portMAX_DELAY) == pdTRUE) {
            /* A cancelled item may still be in the queue (QUEUED flag cleared
             * but not removable from FreeRTOS queue). Skip it silently. */
            if (work && work->handler && (work->flags & K_WORK_QUEUED)) {
                work->flags |= K_WORK_RUNNING;
                work->flags &= ~K_WORK_QUEUED;
                work->handler(work);
                work->flags &= ~K_WORK_RUNNING;
            }
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
    work->node.next = NULL;
    work->node.prev = NULL;
}

static int k_work_submit_internal(struct k_work_queue *queue,
                                  struct k_work *work)
{
    if (work->flags & K_WORK_QUEUED) {
        /* Already queued -- idempotent */
        return 1;
    }

    work->flags |= K_WORK_QUEUED;

    BaseType_t ret;
    if (xPortInIsrContext()) {
        BaseType_t wake = pdFALSE;
        ret = xQueueSendToBackFromISR(queue->queue, &work, &wake);
        if (wake) {
            portYIELD_FROM_ISR(wake);
        }
    } else {
        ret = xQueueSendToBack(queue->queue, &work, pdMS_TO_TICKS(20));
    }

    if (ret != pdTRUE) {
        work->flags &= ~K_WORK_QUEUED;
        ESP_LOGW(TAG, "Work queue full");
        return -1;
    }
    return 0;
}

int k_work_submit(struct k_work *work)
{
    if (!sys_wq_initialized) {
        ESP_LOGE(TAG, "System work queue not initialized");
        return -1;
    }
    return k_work_submit_internal(&k_sys_work_q, work);
}

int k_work_submit_to_queue(struct k_work_queue *queue, struct k_work *work)
{
    return k_work_submit_internal(queue, work);
}

bool k_work_cancel(struct k_work *work)
{
    if (work->flags & K_WORK_RUNNING) {
        return false; /* Can't cancel while running */
    }
    work->flags &= ~K_WORK_QUEUED;
    return true;
}

bool k_work_is_pending(struct k_work *work)
{
    return (work->flags & (K_WORK_QUEUED | K_WORK_RUNNING)) != 0;
}

/* ----------------------------------------------------------------
 * Work Queue Lifecycle
 * ---------------------------------------------------------------- */

void k_work_queue_init(struct k_work_queue *queue)
{
    queue->queue = NULL;
    queue->thread = NULL;
    queue->name = NULL;
    /* storage, stack, depth, stack_size, tcb are set by K_WORK_QUEUE_DEFINE
     * or by the caller before k_work_queue_start */
}

void k_work_queue_start(struct k_work_queue *queue, const char *name,
                        uint32_t stack_size, int prio)
{
    queue->name = name;

    /* Create queue for work item pointers */
    queue->queue = xQueueCreateStatic(queue->depth,
                                      sizeof(struct k_work *),
                                      queue->storage,
                                      &queue->queue_buffer);

    /* Create worker thread using the queue's own stack and TCB */
    queue->thread = xTaskCreateStatic(k_work_queue_thread,
                                      name,
                                      stack_size / sizeof(StackType_t),
                                      queue, prio, queue->stack,
                                      &queue->tcb);

    if (queue == &k_sys_work_q) {
        sys_wq_initialized = true;
    }

    ESP_LOGI(TAG, "Work queue '%s' started (prio=%d, stack=%lu)",
             name, prio, (unsigned long)stack_size);
}

/* ----------------------------------------------------------------
 * Delayable Work
 * ---------------------------------------------------------------- */

static void k_work_delayable_timer_expiry(struct k_timer *timer)
{
    struct k_work_delayable *dwork =
        CONTAINER_OF(timer, struct k_work_delayable, timer);
    k_work_submit(&dwork->work);
}

void k_work_init_delayable(struct k_work_delayable *dwork,
                           k_work_handler_t handler)
{
    k_work_init(&dwork->work, handler);
    k_timer_init(&dwork->timer, k_work_delayable_timer_expiry, NULL);
}

int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
    return k_work_schedule_for_queue(&k_sys_work_q, dwork, delay);
}

int k_work_schedule_for_queue(struct k_work_queue *queue,
                              struct k_work_delayable *dwork,
                              k_timeout_t delay)
{
    if (k_work_is_pending(&dwork->work)) {
        return 0; /* Already scheduled */
    }

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
