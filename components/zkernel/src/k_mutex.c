/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include "esp_log.h"

static const char *TAG = "k_mutex";

int k_mutex_init(struct k_mutex *mutex)
{
    /* Zephyr k_mutex is reentrant -- same thread can re-lock */
    mutex->handle = xSemaphoreCreateRecursiveMutexStatic(&mutex->buffer);
    if (mutex->handle == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return -1;
    }
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
    BaseType_t ret = xSemaphoreTakeRecursive(mutex->handle,
                                             k_timeout_to_ticks(timeout));
    if (ret != pdTRUE) {
        return -1;
    }

#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
    mutex->lock_time = xTaskGetTickCount();
#endif

    return 0;
}

int k_mutex_unlock(struct k_mutex *mutex)
{
#if defined(CONFIG_ZKERNEL_MUTEX_DEBUG)
#if CONFIG_ZKERNEL_MUTEX_HOLD_WARNING_MS > 0
    uint32_t held_ms = (xTaskGetTickCount() - mutex->lock_time) * portTICK_PERIOD_MS;
    if (held_ms > CONFIG_ZKERNEL_MUTEX_HOLD_WARNING_MS) {
        ESP_LOGW(TAG, "Mutex held for %lu ms (threshold: %d ms)",
                 (unsigned long)held_ms, CONFIG_ZKERNEL_MUTEX_HOLD_WARNING_MS);
    }
#endif
#endif

    BaseType_t ret = xSemaphoreGiveRecursive(mutex->handle);
    return (ret == pdTRUE) ? 0 : -1;
}
