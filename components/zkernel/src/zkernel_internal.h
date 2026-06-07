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

#if configTASK_NOTIFICATION_ARRAY_ENTRIES < 2
#error "Boreas zkernel reserves task-notification index 1 for blocking primitives (index 0 stays free for ESP-IDF internals such as esp_ipc/pthread/eth). Add sdkconfig.boreas to SDKCONFIG_DEFAULTS (see the README) or set CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES=2 in sdkconfig."
#endif

/* Reserved for zkernel blocking primitives (k_sem, k_event): index 0
 * is used by ESP-IDF internals. Sharing one index across primitives is
 * safe -- a task blocks on at most one primitive at a time, and every
 * blocking call drains in-flight notifications before returning (the
 * consume-the-in-flight-give protocol). */
#define Z_KERNEL_NOTIFY_INDEX 1

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
