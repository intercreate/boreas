/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr k_mutex: re-entrant mutex with priority inheritance.
 *
 * Uses a non-recursive FreeRTOS mutex (which has PI) with manual
 * re-entrancy tracking via owner + count. This matches Zephyr
 * semantics: same thread can lock multiple times, and waiters
 * boost the holder's priority.
 */

#include "zephyr/kernel.h"

#include <errno.h>

#include "esp_log.h"

static const char *TAG = "k_mutex";

int k_mutex_init(struct k_mutex *mutex)
{
    mutex->handle = xSemaphoreCreateMutexStatic(&mutex->buffer);
    if (mutex->handle == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return -ENOMEM;
    }
    mutex->owner = NULL;
    mutex->count = 0;
#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
    mutex->order = 0;
    mutex->lock_time = 0;
#endif
    return 0;
}

#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
int k_mutex_init_ordered(struct k_mutex *mutex, uint8_t order)
{
    int ret = k_mutex_init(mutex);
    if (ret == 0) {
        mutex->order = order;
    }
    return ret;
}
#endif

int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout)
{
    if (xPortInIsrContext()) {
        return -EWOULDBLOCK;
    }

    TaskHandle_t current = xTaskGetCurrentTaskHandle();

    /* Re-entrant: if we already own it, just bump the count */
    if (mutex->owner == current) {
        mutex->count++;
        return 0;
    }

    /* First acquisition: take the PI-enabled mutex */
    BaseType_t ret = xSemaphoreTake(mutex->handle,
                                    k_timeout_to_ticks(timeout));
    if (ret != pdTRUE) {
        return k_timeout_is_no_wait(timeout) ? -EBUSY : -EAGAIN;
    }

    mutex->owner = current;
    mutex->count = 1;

#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
    mutex->lock_time = xTaskGetTickCount();
#endif

    return 0;
}

int k_mutex_unlock(struct k_mutex *mutex)
{
    if (xPortInIsrContext()) {
        return -EWOULDBLOCK;
    }

    TaskHandle_t current = xTaskGetCurrentTaskHandle();

    if (mutex->owner != current) {
        return -EPERM;
    }

#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
#if CONFIG_ZKERNEL_MUTEX_HOLD_WARNING_MS > 0
    if (mutex->count == 1) {
        uint32_t held_ms = (xTaskGetTickCount() - mutex->lock_time) * portTICK_PERIOD_MS;
        if (held_ms > CONFIG_ZKERNEL_MUTEX_HOLD_WARNING_MS) {
            ESP_LOGW(TAG, "Mutex held for %lu ms (threshold: %d ms)",
                     (unsigned long)held_ms, CONFIG_ZKERNEL_MUTEX_HOLD_WARNING_MS);
        }
    }
#endif
#endif

    mutex->count--;
    if (mutex->count == 0) {
        mutex->owner = NULL;
        xSemaphoreGive(mutex->handle);
    }
    return 0;
}
