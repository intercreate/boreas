/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible runtime assertions (upstream <zephyr/sys/__assert.h>
 * spellings).
 *
 * Divergence: upstream compiles these out unless CONFIG_ASSERT=y
 * (default n); Boreas keeps them always on -- they log and abort.
 * NOT safe from IRAM ISR context (ESP_LOGE + abort are flash-resident).
 * Use k_panic() (sys/util.h) for unrecoverable errors in ISR/IRAM
 * context.
 */

#pragma once

#ifndef __ASSERT
#include <stdlib.h> /* abort() */

#include "esp_log.h"
#define __ASSERT(cond, msg)                                                                        \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			ESP_LOGE("ASSERT", "%s at %s:%d", (msg), __FILE__, __LINE__);              \
			abort();                                                                   \
		}                                                                                  \
	} while (0)
#endif

/* Assertion with no message (upstream spelling). */
#ifndef __ASSERT_NO_MSG
#define __ASSERT_NO_MSG(cond) __ASSERT((cond), "")
#endif
