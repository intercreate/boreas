/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible toolchain shims (upstream <zephyr/toolchain.h>
 * spellings), kept thin so near-verbatim ports can keep their upstream
 * #include shape. All definitions are #ifndef-guarded: a TU that
 * already got a definition elsewhere keeps the first one it saw.
 */

#pragma once

#include "esp_attr.h"

/* Branch-prediction hints.
 * Divergence from ESP-IDF: esp_compiler.h defines likely/unlikely as
 * plain (x) unless CONFIG_COMPILER_OPTIMIZATION_PERF is set; Boreas
 * matches upstream Zephyr's unconditional __builtin_expect instead.
 * Both definitions are #ifndef-guarded, so include order picks the
 * winner per TU -- the difference is codegen-only (identical
 * semantics either way). */
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

/* Weak symbol */
#ifndef __weak
#define __weak __attribute__((__weak__))
#endif

/* Always inline -- undef any prior definition to guarantee `inline` is
 * present (RISC-V GCC errors without it under -Werror=attributes). */
#undef ALWAYS_INLINE
#define ALWAYS_INLINE __attribute__((always_inline)) inline

/* Upstream places __noinit data in a section the loader does not zero
 * (so it also survives a warm reset). Mapped to ESP-IDF's
 * __NOINIT_ATTR: a genuine .noinit section on hardware targets, and a
 * no-op on the linux/host test target (esp_attr.h's _SECTION_ATTR_IMPL
 * expands to nothing under CONFIG_IDF_TARGET_LINUX), where __noinit
 * data is ordinary zero-initialized BSS instead. Code that must retain
 * data across a warm reset therefore cannot rely on this shim on the
 * host target; on hardware, prefer RTC_NOINIT_ATTR when the data must
 * also survive deep sleep. */
#ifndef __noinit
#define __noinit __NOINIT_ATTR
#endif
