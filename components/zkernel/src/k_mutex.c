/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr k_mutex: re-entrant mutex with priority inheritance.
 *
 * Thin wrapper over a FreeRTOS recursive mutex, which already provides
 * both re-entrancy (recursive take/give) and priority inheritance --
 * matching Zephyr's k_mutex semantics. The blocking take routes through
 * xQueueSemaphoreTake, which priority-inherits for any mutex-type queue,
 * so no manual owner/count tracking is needed.
 */

#include "zephyr/kernel.h"

#include <errno.h>

#include "esp_log.h"

static const char *TAG = "k_mutex";

int k_mutex_init(struct k_mutex *mutex)
{
	mutex->handle = xSemaphoreCreateRecursiveMutexStatic(&mutex->buffer);
	if (mutex->handle == NULL) {
		ESP_LOGE(TAG, "Failed to create mutex");
		return -ENOMEM;
	}
	return 0;
}

int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout)
{
	if (xPortInIsrContext()) {
		return -EWOULDBLOCK;
	}

	if (xSemaphoreTakeRecursive(mutex->handle, k_timeout_to_ticks(timeout)) != pdTRUE) {
		return k_timeout_is_no_wait(timeout) ? -EBUSY : -EAGAIN;
	}
	return 0;
}

int k_mutex_unlock(struct k_mutex *mutex)
{
	if (xPortInIsrContext()) {
		return -EWOULDBLOCK;
	}

	/* Recursive give returns pdFALSE when the caller isn't the owner. */
	if (xSemaphoreGiveRecursive(mutex->handle) != pdTRUE) {
		return -EPERM;
	}
	return 0;
}
