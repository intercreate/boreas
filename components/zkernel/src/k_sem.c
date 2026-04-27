/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include <errno.h>

#include "esp_log.h"

static const char *TAG = "k_sem";

int k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit)
{
	sem->handle = xSemaphoreCreateCountingStatic(limit, initial_count, &sem->buffer);
	if (sem->handle == NULL) {
		ESP_LOGE(TAG, "Failed to create semaphore");
		return -ENOMEM;
	}
	return 0;
}

int k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	BaseType_t ret = xSemaphoreTake(sem->handle, k_timeout_to_ticks(timeout));
	if (ret == pdTRUE) {
		return 0;
	}
	return k_timeout_is_no_wait(timeout) ? -EBUSY : -EAGAIN;
}

void k_sem_give(struct k_sem *sem)
{
	if (xPortInIsrContext()) {
		BaseType_t wake = pdFALSE;
		xSemaphoreGiveFromISR(sem->handle, &wake);
		if (wake) {
			portYIELD_FROM_ISR(wake);
		}
	} else {
		xSemaphoreGive(sem->handle);
	}
}

void k_sem_reset(struct k_sem *sem)
{
	/* Drain all tokens */
	while (xSemaphoreTake(sem->handle, 0) == pdTRUE) {
	}
}

unsigned int k_sem_count_get(struct k_sem *sem)
{
	return (unsigned int)uxSemaphoreGetCount(sem->handle);
}
