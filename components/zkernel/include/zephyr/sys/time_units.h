/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible timeout API over FreeRTOS tick types.
 */

#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef struct {
    int64_t ticks; /* FreeRTOS ticks; -1 = forever; 0 = no wait */
} k_timeout_t;

#define K_MSEC(ms)    ((k_timeout_t){.ticks = pdMS_TO_TICKS(ms)})
#define K_SECONDS(s)  ((k_timeout_t){.ticks = pdMS_TO_TICKS((s) * 1000)})
#define K_MINUTES(m)  ((k_timeout_t){.ticks = pdMS_TO_TICKS((m) * 60000)})
#define K_USEC(us)    ((k_timeout_t){.ticks = pdMS_TO_TICKS((us) / 1000)})
#define K_TICKS(t)    ((k_timeout_t){.ticks = (t)})
#define K_NO_WAIT     ((k_timeout_t){.ticks = 0})
#define K_FOREVER     ((k_timeout_t){.ticks = -1})

static inline TickType_t k_timeout_to_ticks(k_timeout_t timeout)
{
    if (timeout.ticks < 0) {
        return portMAX_DELAY;
    }
    return (TickType_t)timeout.ticks;
}

static inline bool k_timeout_is_forever(k_timeout_t timeout)
{
    return timeout.ticks < 0;
}

static inline bool k_timeout_is_no_wait(k_timeout_t timeout)
{
    return timeout.ticks == 0;
}
