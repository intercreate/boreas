/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

static void test_uptime_get_positive(void)
{
	int64_t ms = k_uptime_get();
	TEST_ASSERT_GREATER_THAN(0, ms);
}

static void test_uptime_get_32(void)
{
	uint32_t ms = k_uptime_get_32();
	TEST_ASSERT_GREATER_THAN(0, ms);
}

static void test_uptime_monotonic(void)
{
	int64_t a = k_uptime_get();
	k_msleep(10);
	int64_t b = k_uptime_get();
	TEST_ASSERT_GREATER_THAN(a, b);
}

static void test_uptime_delta(void)
{
	int64_t ref = k_uptime_get();
	k_msleep(50);
	int64_t delta = k_uptime_delta(&ref);

	/* Delta should be ~50ms (allow 20-200ms for scheduler jitter) */
	TEST_ASSERT_GREATER_OR_EQUAL(20, delta);
	TEST_ASSERT_LESS_OR_EQUAL(200, delta);

	/* ref should have been updated to ~now */
	int64_t now = k_uptime_get();
	TEST_ASSERT_LESS_OR_EQUAL(50, now - ref);
}

void test_uptime_group(void)
{
	RUN_TEST(test_uptime_get_positive);
	RUN_TEST(test_uptime_get_32);
	RUN_TEST(test_uptime_monotonic);
	RUN_TEST(test_uptime_delta);
}
