/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/sys/atomic.h"

static void test_atomic_init_get(void)
{
	atomic_t a = ATOMIC_INIT(42);
	TEST_ASSERT_EQUAL(42, atomic_get(&a));
}

static void test_atomic_set_returns_previous(void)
{
	/* atomic_set must return the previous value (Zephyr-compat).
	 * This is the regression test for the divergence fix. */
	atomic_t a = ATOMIC_INIT(7);
	atomic_val_t prev = atomic_set(&a, 99);
	TEST_ASSERT_EQUAL(7, prev);
	TEST_ASSERT_EQUAL(99, atomic_get(&a));
}

static void test_atomic_or(void)
{
	atomic_t a = ATOMIC_INIT(0x0F);
	atomic_val_t prev = atomic_or(&a, 0xF0);
	TEST_ASSERT_EQUAL(0x0F, prev);
	TEST_ASSERT_EQUAL(0xFF, atomic_get(&a));
}

static void test_atomic_and(void)
{
	atomic_t a = ATOMIC_INIT(0xFF);
	atomic_val_t prev = atomic_and(&a, 0x0F);
	TEST_ASSERT_EQUAL(0xFF, prev);
	TEST_ASSERT_EQUAL(0x0F, atomic_get(&a));
}

static void test_atomic_add(void)
{
	atomic_t a = ATOMIC_INIT(10);
	atomic_val_t prev = atomic_add(&a, 5);
	TEST_ASSERT_EQUAL(10, prev);
	TEST_ASSERT_EQUAL(15, atomic_get(&a));
}

static void test_atomic_sub(void)
{
	atomic_t a = ATOMIC_INIT(20);
	atomic_val_t prev = atomic_sub(&a, 7);
	TEST_ASSERT_EQUAL(20, prev);
	TEST_ASSERT_EQUAL(13, atomic_get(&a));
}

static void test_atomic_test_bit(void)
{
	atomic_t a = ATOMIC_INIT(0);
	TEST_ASSERT_FALSE(atomic_test_bit(&a, 3));
	atomic_set(&a, (atomic_val_t)(1UL << 3));
	TEST_ASSERT_TRUE(atomic_test_bit(&a, 3));
	TEST_ASSERT_FALSE(atomic_test_bit(&a, 4));
}

static void test_atomic_set_bit(void)
{
	atomic_t a = ATOMIC_INIT(0);
	atomic_set_bit(&a, 5);
	atomic_set_bit(&a, 10);
	TEST_ASSERT_TRUE(atomic_test_bit(&a, 5));
	TEST_ASSERT_TRUE(atomic_test_bit(&a, 10));
	TEST_ASSERT_FALSE(atomic_test_bit(&a, 6));
}

static void test_atomic_clear_bit(void)
{
	atomic_t a = ATOMIC_INIT(0);
	atomic_set_bit(&a, 7);
	TEST_ASSERT_TRUE(atomic_test_bit(&a, 7));
	atomic_clear_bit(&a, 7);
	TEST_ASSERT_FALSE(atomic_test_bit(&a, 7));
}

static void test_atomic_test_and_set_bit(void)
{
	atomic_t a = ATOMIC_INIT(0);

	/* First call: bit was 0, returns false, leaves bit set */
	TEST_ASSERT_FALSE(atomic_test_and_set_bit(&a, 4));
	TEST_ASSERT_TRUE(atomic_test_bit(&a, 4));

	/* Second call: bit was 1, returns true, still set */
	TEST_ASSERT_TRUE(atomic_test_and_set_bit(&a, 4));
	TEST_ASSERT_TRUE(atomic_test_bit(&a, 4));
}

static void test_atomic_test_and_clear_bit(void)
{
	atomic_t a = ATOMIC_INIT(0);
	atomic_set_bit(&a, 8);

	/* First call: bit was 1, returns true, leaves bit cleared */
	TEST_ASSERT_TRUE(atomic_test_and_clear_bit(&a, 8));
	TEST_ASSERT_FALSE(atomic_test_bit(&a, 8));

	/* Second call: bit was 0, returns false, still cleared */
	TEST_ASSERT_FALSE(atomic_test_and_clear_bit(&a, 8));
	TEST_ASSERT_FALSE(atomic_test_bit(&a, 8));
}

static void test_atomic_negative_values(void)
{
	/* atomic_t is signed; verify negative values round-trip */
	atomic_t a = ATOMIC_INIT(-1);
	TEST_ASSERT_EQUAL(-1, atomic_get(&a));
	atomic_val_t prev = atomic_set(&a, -100);
	TEST_ASSERT_EQUAL(-1, prev);
	TEST_ASSERT_EQUAL(-100, atomic_get(&a));
}

void test_atomic_group(void)
{
	RUN_TEST(test_atomic_init_get);
	RUN_TEST(test_atomic_set_returns_previous);
	RUN_TEST(test_atomic_or);
	RUN_TEST(test_atomic_and);
	RUN_TEST(test_atomic_add);
	RUN_TEST(test_atomic_sub);
	RUN_TEST(test_atomic_test_bit);
	RUN_TEST(test_atomic_set_bit);
	RUN_TEST(test_atomic_clear_bit);
	RUN_TEST(test_atomic_test_and_set_bit);
	RUN_TEST(test_atomic_test_and_clear_bit);
	RUN_TEST(test_atomic_negative_values);
}
