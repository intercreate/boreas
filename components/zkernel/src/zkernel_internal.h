/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Private helpers shared by zkernel sources. Not installed; not part
 * of the public API.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_attr.h"

/* ISR-aware critical section: ESP-IDF needs different macros for ISR
 * vs task context. Inlined to avoid runtime overhead in the common
 * (task) case. */
static ALWAYS_INLINE void z_kernel_lock(portMUX_TYPE *lock)
{
	if (xPortInIsrContext()) {
		portENTER_CRITICAL_ISR(lock);
	} else {
		portENTER_CRITICAL(lock);
	}
}

static ALWAYS_INLINE void z_kernel_unlock(portMUX_TYPE *lock)
{
	if (xPortInIsrContext()) {
		portEXIT_CRITICAL_ISR(lock);
	} else {
		portEXIT_CRITICAL(lock);
	}
}
