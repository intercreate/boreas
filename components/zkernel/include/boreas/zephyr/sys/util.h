/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible utility macros.
 */

#pragma once

#include "esp_attr.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#ifndef BIT_MASK
#define BIT_MASK(n) (BIT(n) - 1UL)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef CLAMP
#define CLAMP(val, lo, hi) MIN(MAX(val, lo), hi)
#endif

#ifndef ABS
#define ABS(a) (((a) < 0) ? -(a) : (a))
#endif

#ifndef IS_POWER_OF_TWO
#define IS_POWER_OF_TWO(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))
#endif

#ifndef ROUND_UP
#define ROUND_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#endif

#ifndef ROUND_DOWN
#define ROUND_DOWN(x, align) ((x) & ~((align) - 1))
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, field) ((type *)(((char *)(ptr)) - offsetof(type, field)))
#endif

#ifndef IS_ENABLED
#define IS_ENABLED(config_macro)       _IS_ENABLED1(config_macro)
#define _IS_ENABLED1(config_macro)     _IS_ENABLED2(_XXXX##config_macro)
#define _IS_ENABLED2(one_or_two_args)  _IS_ENABLED3(one_or_two_args true, false)
#define _IS_ENABLED3(ignore, val, ...) val
#define _XXXX1                         _YYYY,
#endif

#ifndef POPCOUNT
#define POPCOUNT(x) __builtin_popcount(x)
#endif

/* Compile-time assertion */
#ifndef BUILD_ASSERT
#define BUILD_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

/* Mark a variable as intentionally unused */
#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

/* Packed struct attribute */
#ifndef PACKED
#define PACKED __attribute__((__packed__))
#endif

/* Aligned attribute */
#ifndef ALIGNED
#define ALIGNED(x) __attribute__((__aligned__(x)))
#endif

/* Weak symbol */
#ifndef __weak
#define __weak __attribute__((__weak__))
#endif

/* Always inline */
#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

/* Runtime assertion -- logs and aborts.
 * NOT safe from IRAM ISR context (ESP_LOGE + abort are flash-resident).
 * Use k_panic() for unrecoverable errors in ISR/IRAM context. */
#ifndef __ASSERT
#include "esp_log.h"
#define __ASSERT(cond, msg)                                                                        \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			ESP_LOGE("ASSERT", "%s at %s:%d", (msg), __FILE__, __LINE__);              \
			abort();                                                                   \
		}                                                                                  \
	} while (0)
#endif

/* IRAM-safe panic -- triggers an illegal-instruction exception caught by
 * the ESP-IDF panic handler (which is IRAM-resident). Produces a full
 * backtrace on UART and reboots. Safe to call from IRAM_ATTR ISR context.
 * Mirrors upstream Zephyr's k_panic() contract. */
#define k_panic() __builtin_trap()

/* Attribute for kernel functions callable from ISR context.
 * Always IRAM-resident: upstream Zephyr marks these isr-ok
 * unconditionally, and ESP-IDF ISRs registered with
 * ESP_INTR_FLAG_IRAM fire during cache-disabled windows. */
#define K_ISR_SAFE IRAM_ATTR

#ifdef __cplusplus
}
#endif
