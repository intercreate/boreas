/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

static void test_msleep(void)
{
	int64_t before = k_uptime_get();
	k_msleep(100);
	int64_t elapsed = k_uptime_get() - before;

	/* Should sleep ~100ms (allow 80-300ms for tick granularity) */
	TEST_ASSERT_GREATER_OR_EQUAL(80, elapsed);
	TEST_ASSERT_LESS_OR_EQUAL(300, elapsed);
}

static void test_sleep_with_timeout(void)
{
	int64_t before = k_uptime_get();
	k_sleep(K_MSEC(100));
	int64_t elapsed = k_uptime_get() - before;

	TEST_ASSERT_GREATER_OR_EQUAL(80, elapsed);
	TEST_ASSERT_LESS_OR_EQUAL(300, elapsed);
}

static void test_sleep_no_wait(void)
{
	/* K_NO_WAIT should return immediately */
	int64_t before = k_uptime_get();
	k_sleep(K_NO_WAIT);
	int64_t elapsed = k_uptime_get() - before;

	TEST_ASSERT_LESS_OR_EQUAL(10, elapsed);
}

static void test_yield(void)
{
	/* k_yield should not crash or hang -- just context switch */
	int64_t before = k_uptime_get();
	k_yield();
	int64_t elapsed = k_uptime_get() - before;

	TEST_ASSERT_LESS_OR_EQUAL(10, elapsed);
}

void test_sleep_group(void)
{
	RUN_TEST(test_msleep);
	RUN_TEST(test_sleep_with_timeout);
	RUN_TEST(test_sleep_no_wait);
	RUN_TEST(test_yield);
}
