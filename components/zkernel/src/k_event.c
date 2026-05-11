/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include <errno.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "k_event";

int k_event_init(struct k_event *event)
{
	event->handle = xEventGroupCreateStatic(&event->buffer);
	if (event->handle == NULL) {
		ESP_LOGE(TAG, "Failed to create event group");
		return -ENOMEM;
	}
	return 0;
}

uint32_t K_ISR_SAFE k_event_post(struct k_event *event, uint32_t events)
{
	return k_event_set(event, events);
}

uint32_t K_ISR_SAFE k_event_set(struct k_event *event, uint32_t events)
{
	if (xPortInIsrContext()) {
		BaseType_t wake = pdFALSE;
		BaseType_t ret =
			xEventGroupSetBitsFromISR(event->handle, (EventBits_t)events, &wake);
		if (wake) {
			portYIELD_FROM_ISR(wake);
		}
		return (ret == pdPASS) ? events : 0;
	}
	return (uint32_t)xEventGroupSetBits(event->handle, (EventBits_t)events);
}

uint32_t K_ISR_SAFE k_event_clear(struct k_event *event, uint32_t events)
{
	if (xPortInIsrContext()) {
		return (uint32_t)xEventGroupClearBitsFromISR(event->handle, (EventBits_t)events);
	}
	return (uint32_t)xEventGroupClearBits(event->handle, (EventBits_t)events);
}

uint32_t k_event_wait(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout)
{
	EventBits_t bits = xEventGroupWaitBits(event->handle, (EventBits_t)events,
					       reset ? pdTRUE : pdFALSE, pdFALSE, /* wait for ANY */
					       k_timeout_to_ticks(timeout));
	return (uint32_t)(bits & events);
}

uint32_t k_event_wait_all(struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout)
{
	EventBits_t bits = xEventGroupWaitBits(event->handle, (EventBits_t)events,
					       reset ? pdTRUE : pdFALSE, pdTRUE, /* wait for ALL */
					       k_timeout_to_ticks(timeout));
	return (uint32_t)(bits & events);
}
