/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible atomic operations over GCC __atomic builtins.
 *
 * Ordering: all RMW operations use __ATOMIC_SEQ_CST to match Zephyr's
 * atomic.h contract, which documents that each atomic op acts as a full
 * barrier (see Zephyr include/zephyr/sys/atomic_builtin.h). Seq-cst is
 * the conservative default and is cheap on RISC-V (one fence); no measured
 * benefit to weaker orderings for typical flag-coordination use cases.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long atomic_t;
typedef atomic_t atomic_val_t;

#define ATOMIC_INIT(i) (i)

static inline atomic_val_t atomic_get(const atomic_t *target)
{
	return __atomic_load_n(target, __ATOMIC_SEQ_CST);
}

/**
 * Atomically set @p target to @p value and return the previous value.
 * Matches upstream Zephyr's atomic_set(): the return value enables
 * atomic-swap idioms (e.g. `old = atomic_set(&x, new);`).
 */
static inline atomic_val_t atomic_set(atomic_t *target, atomic_val_t value)
{
	return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_or(atomic_t *target, atomic_val_t value)
{
	return __atomic_fetch_or(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_and(atomic_t *target, atomic_val_t value)
{
	return __atomic_fetch_and(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_add(atomic_t *target, atomic_val_t value)
{
	return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_sub(atomic_t *target, atomic_val_t value)
{
	return __atomic_fetch_sub(target, value, __ATOMIC_SEQ_CST);
}

/* Bit-addressed helpers. These operate on a single atomic_t word;
 * bit must be in [0, sizeof(atomic_t) * 8). Multi-word atomic bitmaps
 * (ATOMIC_DEFINE in upstream Zephyr) are not supported -- add them only
 * if a caller actually needs more than a word's worth of flags.
 *
 * The mask is built as `1UL << bit` (unsigned) and then cast to the
 * signed atomic_val_t. Shifting a 1 directly into the sign bit of a
 * signed type is undefined behavior in C; doing the shift in unsigned
 * first avoids that while preserving Zephyr's signed atomic_t typedef.
 */
#define _ATOMIC_BIT_MASK(bit) ((atomic_val_t)(1UL << (bit)))

static inline bool atomic_test_bit(const atomic_t *target, int bit)
{
	return (atomic_get(target) & _ATOMIC_BIT_MASK(bit)) != 0;
}

static inline void atomic_set_bit(atomic_t *target, int bit)
{
	(void)atomic_or(target, _ATOMIC_BIT_MASK(bit));
}

static inline void atomic_clear_bit(atomic_t *target, int bit)
{
	(void)atomic_and(target, ~_ATOMIC_BIT_MASK(bit));
}

static inline bool atomic_test_and_set_bit(atomic_t *target, int bit)
{
	atomic_val_t mask = _ATOMIC_BIT_MASK(bit);
	return (atomic_or(target, mask) & mask) != 0;
}

static inline bool atomic_test_and_clear_bit(atomic_t *target, int bit)
{
	atomic_val_t mask = _ATOMIC_BIT_MASK(bit);
	return (atomic_and(target, ~mask) & mask) != 0;
}

#ifdef __cplusplus
}
#endif
