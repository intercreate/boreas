/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include "esp_log.h"

static const char *TAG = "k_thread";

/* Wrapper to adapt Zephyr's 3-arg entry to FreeRTOS's 1-arg entry */
struct thread_args {
    k_thread_entry_t entry;
    void *p1;
    void *p2;
    void *p3;
};

static void k_thread_entry_wrapper(void *arg)
{
    struct k_thread *thread = (struct k_thread *)arg;
    /* Arguments are stashed after the TCB — we use a simple convention:
     * p1 is passed via FreeRTOS task arg, p2 and p3 are NULL for now.
     * Full 3-arg support requires storing args in the k_thread struct. */
    (void)thread;
    /* This is a simplified wrapper — full implementation will store
     * entry + args in k_thread struct */
    vTaskDelete(NULL);
}

void k_thread_create(struct k_thread *thread, StackType_t *stack,
                     size_t stack_size, k_thread_entry_t entry,
                     void *p1, void *p2, void *p3,
                     int prio, uint32_t options, k_timeout_t delay)
{
    (void)options;
    (void)delay; /* TODO: deferred start via k_timer */
    (void)p2;
    (void)p3;

    thread->stack = stack;
    thread->stack_size = stack_size;

    thread->handle = xTaskCreateStatic(
        (TaskFunction_t)entry,
        "k_thread",
        stack_size / sizeof(StackType_t),
        p1,
        prio,
        stack,
        &thread->tcb);

    if (thread->handle == NULL) {
        ESP_LOGE(TAG, "Failed to create thread");
    }
}

void k_thread_name_set(struct k_thread *thread, const char *name)
{
    thread->name = name;
    /* FreeRTOS doesn't support renaming after creation,
     * but we store it for diagnostics */
}

void k_thread_abort(struct k_thread *thread)
{
    if (thread->handle != NULL) {
        vTaskDelete(thread->handle);
        thread->handle = NULL;
    }
}

int k_thread_join(struct k_thread *thread, k_timeout_t timeout)
{
    /* FreeRTOS doesn't have native join. Poll eTaskGetState. */
    TickType_t deadline = xTaskGetTickCount() + k_timeout_to_ticks(timeout);
    bool forever = k_timeout_is_forever(timeout);

    while (thread->handle != NULL) {
        eTaskState state = eTaskGetState(thread->handle);
        if (state == eDeleted || state == eInvalid) {
            thread->handle = NULL;
            return 0;
        }
        if (!forever && xTaskGetTickCount() >= deadline) {
            return -1; /* timeout */
        }
        k_msleep(10);
    }
    return 0;
}

int k_thread_stack_space_get(struct k_thread *thread, size_t *unused)
{
    if (thread->handle == NULL) {
        return -1;
    }
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(thread->handle);
    *unused = (size_t)(hwm * sizeof(StackType_t));
    return 0;
}
