/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible timeout API.
 *
 * Timeouts store microseconds internally. Conversion to FreeRTOS ticks
 * happens at the point of use (k_timeout_to_ticks), and direct
 * microsecond access for esp_timer is lossless (k_timeout_to_us).
 */

#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef struct {
	int64_t us; /* microseconds; -1 = forever; 0 = no wait */
} k_timeout_t;

/** Tick-count type, mirrors upstream Zephyr's <zephyr/sys/time_units.h>.
 *  Used for absolute and relative tick-resolution timer queries. */
typedef int64_t k_ticks_t;

#define K_USEC(usec) ((k_timeout_t){.us = (usec)})
#define K_MSEC(msec) ((k_timeout_t){.us = (int64_t)(msec) * 1000})
#define K_SECONDS(s) ((k_timeout_t){.us = (int64_t)(s) * 1000000})
#define K_MINUTES(m) ((k_timeout_t){.us = (int64_t)(m) * 60000000})
#define K_TICKS(t)   ((k_timeout_t){.us = (int64_t)(t) * portTICK_PERIOD_MS * 1000})
#define K_NO_WAIT    ((k_timeout_t){.us = 0})
#define K_FOREVER    ((k_timeout_t){.us = -1})

/** Convert timeout to FreeRTOS ticks (for sem, mutex, msgq, etc.).
 *  Resolution limited by CONFIG_FREERTOS_HZ.
 *  Non-zero sub-tick values round up to 1 tick to avoid becoming K_NO_WAIT. */
static inline TickType_t k_timeout_to_ticks(k_timeout_t timeout)
{
	if (timeout.us < 0) {
		return portMAX_DELAY;
	}
	if (timeout.us == 0) {
		return 0;
	}
	TickType_t ticks = pdMS_TO_TICKS(timeout.us / 1000);
	return (ticks > 0) ? ticks : 1; /* at least 1 tick for non-zero timeout */
}

/** Convert timeout to microseconds (for esp_timer — lossless for finite values).
 *  K_FOREVER and K_NO_WAIT both return 0 (esp_timer cannot express "forever"). */
static inline uint64_t k_timeout_to_us(k_timeout_t timeout)
{
	if (timeout.us <= 0) {
		return 0;
	}
	return (uint64_t)timeout.us;
}

/** Convert timeout to milliseconds. */
static inline int64_t k_timeout_to_ms(k_timeout_t timeout)
{
	if (timeout.us < 0) {
		return -1;
	}
	return timeout.us / 1000;
}

static inline bool k_timeout_is_forever(k_timeout_t timeout)
{
	return timeout.us < 0;
}

static inline bool k_timeout_is_no_wait(k_timeout_t timeout)
{
	return timeout.us == 0;
}

#define K_TIMEOUT_EQ(a, b) ((a).us == (b).us)
