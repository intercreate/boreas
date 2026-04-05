/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible timeout API.
 *
 * Timeouts store milliseconds internally. Conversion to FreeRTOS ticks
 * happens at the point of use (k_timeout_to_ticks), and conversion to
 * microseconds for esp_timer is lossless (k_timeout_to_us).
 */

#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef struct {
    int64_t ms; /* milliseconds; -1 = forever; 0 = no wait */
} k_timeout_t;

#define K_MSEC(msec)  ((k_timeout_t){.ms = (msec)})
#define K_SECONDS(s)  ((k_timeout_t){.ms = (int64_t)(s) * 1000})
#define K_MINUTES(m)  ((k_timeout_t){.ms = (int64_t)(m) * 60000})
#define K_USEC(us)    ((k_timeout_t){.ms = ((us) + 999) / 1000})
#define K_TICKS(t)    ((k_timeout_t){.ms = (int64_t)(t) * portTICK_PERIOD_MS})
#define K_NO_WAIT     ((k_timeout_t){.ms = 0})
#define K_FOREVER     ((k_timeout_t){.ms = -1})

/** Convert timeout to FreeRTOS ticks (for sem, mutex, msgq, etc.) */
static inline TickType_t k_timeout_to_ticks(k_timeout_t timeout)
{
    if (timeout.ms < 0) {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(timeout.ms);
}

/** Convert timeout to microseconds (for esp_timer -- lossless) */
static inline uint64_t k_timeout_to_us(k_timeout_t timeout)
{
    if (timeout.ms <= 0) {
        return 0;
    }
    return (uint64_t)timeout.ms * 1000;
}

/** Get the raw millisecond value from a timeout */
static inline int64_t k_timeout_to_ms(k_timeout_t timeout)
{
    return timeout.ms;
}

static inline bool k_timeout_is_forever(k_timeout_t timeout)
{
    return timeout.ms < 0;
}

static inline bool k_timeout_is_no_wait(k_timeout_t timeout)
{
    return timeout.ms == 0;
}

#define K_TIMEOUT_EQ(a, b) ((a).ms == (b).ms)
