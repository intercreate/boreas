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

static void test_sleep_returns_zero(void)
{
	/* Zephyr-compat: k_sleep / k_usleep return int32_t (remaining
	 * time when interrupted). Boreas doesn't support interruption
	 * yet, so they always return 0. Verify the type is callable in
	 * the chained-return form. */
	int32_t rem = k_sleep(K_MSEC(1));
	TEST_ASSERT_EQUAL(0, rem);
	rem = k_usleep(100);
	TEST_ASSERT_EQUAL(0, rem);
}

static void test_usleep_sub_ms(void)
{
	/* Sub-millisecond path uses esp_rom_delay_us busy-wait. Verify
	 * it actually delays and does not return early. No upper bound
	 * because scheduler/ISR preemption can legitimately inflate the
	 * measured elapsed time. */
	int64_t before = esp_timer_get_time();
	k_usleep(500);
	int64_t elapsed_us = esp_timer_get_time() - before;

	TEST_ASSERT_GREATER_OR_EQUAL(400, elapsed_us);
}

static void test_usleep_ms_path(void)
{
	/* >=1000us takes the vTaskDelay path; allow tick-granularity slack. */
	int64_t before = k_uptime_get();
	k_usleep(20000); /* 20ms */
	int64_t elapsed = k_uptime_get() - before;

	TEST_ASSERT_GREATER_OR_EQUAL(15, elapsed);
	TEST_ASSERT_LESS_OR_EQUAL(60, elapsed);
}

static void test_usleep_zero(void)
{
	/* Upstream Zephyr no-ops on us<=0 and returns 0. Verify Boreas
	 * matches: must return promptly with no busy-wait. The 1ms
	 * upper bound tolerates normal interrupt/scheduler latency
	 * while still cleanly distinguishing the no-op path from any
	 * accidental fall-through to esp_rom_delay_us. */
	int64_t before = esp_timer_get_time();
	int32_t rem = k_usleep(0);
	int64_t elapsed_us = esp_timer_get_time() - before;

	TEST_ASSERT_EQUAL(0, rem);
	TEST_ASSERT_LESS_OR_EQUAL(1000, elapsed_us);
}

static void test_usleep_negative(void)
{
	/* us<0: same no-op semantics as us==0. Notably must NOT pass a
	 * negative value to esp_rom_delay_us (which takes uint32_t and
	 * would treat -1 as a ~71-minute delay). 1ms upper bound for
	 * the same reason as test_usleep_zero. */
	int64_t before = esp_timer_get_time();
	int32_t rem = k_usleep(-1);
	int64_t elapsed_us = esp_timer_get_time() - before;

	TEST_ASSERT_EQUAL(0, rem);
	TEST_ASSERT_LESS_OR_EQUAL(1000, elapsed_us);
}

void test_sleep_group(void)
{
	RUN_TEST(test_msleep);
	RUN_TEST(test_sleep_with_timeout);
	RUN_TEST(test_sleep_no_wait);
	RUN_TEST(test_yield);
	RUN_TEST(test_sleep_returns_zero);
	RUN_TEST(test_usleep_sub_ms);
	RUN_TEST(test_usleep_ms_path);
	RUN_TEST(test_usleep_zero);
	RUN_TEST(test_usleep_negative);
}
