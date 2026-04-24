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

#define ATOMIC_DEFINE(name, num_bits)                                          \
    atomic_t name[((num_bits) + (sizeof(atomic_t) * 8) - 1) /                  \
                  (sizeof(atomic_t) * 8)]

static inline atomic_val_t atomic_get(const atomic_t *target)
{
    return __atomic_load_n(target, __ATOMIC_SEQ_CST);
}

static inline void atomic_set(atomic_t *target, atomic_val_t value)
{
    __atomic_store_n(target, value, __ATOMIC_SEQ_CST);
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

/* Bit-addressed helpers. `bit` is a 0-based index across the atomic_t word;
 * callers that pack multiple flags into one word are responsible for
 * staying within sizeof(atomic_t) * 8 bits.
 */
static inline bool atomic_test_bit(const atomic_t *target, int bit)
{
    return (atomic_get(target) & ((atomic_val_t)1 << bit)) != 0;
}

static inline void atomic_set_bit(atomic_t *target, int bit)
{
    (void)atomic_or(target, (atomic_val_t)1 << bit);
}

static inline void atomic_clear_bit(atomic_t *target, int bit)
{
    (void)atomic_and(target, ~((atomic_val_t)1 << bit));
}

static inline bool atomic_test_and_set_bit(atomic_t *target, int bit)
{
    atomic_val_t mask = (atomic_val_t)1 << bit;
    return (atomic_or(target, mask) & mask) != 0;
}

static inline bool atomic_test_and_clear_bit(atomic_t *target, int bit)
{
    atomic_val_t mask = (atomic_val_t)1 << bit;
    return (atomic_and(target, ~mask) & mask) != 0;
}

#ifdef __cplusplus
}
#endif
