/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

static volatile int timer_count;
static volatile int stop_called;

static void test_timer_cb(struct k_timer *timer)
{
	timer_count++;
}

static void test_stop_cb(struct k_timer *timer)
{
	stop_called++;
}

static void test_timer_one_shot(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	k_timer_start(&timer, K_MSEC(50), K_NO_WAIT);
	k_msleep(200);
	k_timer_stop(&timer);

	TEST_ASSERT_GREATER_OR_EQUAL(1, timer_count);
}

static void test_timer_periodic(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	k_timer_start(&timer, K_MSEC(50), K_MSEC(50));
	k_msleep(300);
	k_timer_stop(&timer);

	TEST_ASSERT_GREATER_OR_EQUAL(3, timer_count);
}

static void test_timer_stop_callback(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, test_stop_cb);
	stop_called = 0;

	k_timer_start(&timer, K_MSEC(50), K_MSEC(50));
	k_msleep(100);
	k_timer_stop(&timer);

	TEST_ASSERT_EQUAL(1, stop_called);
}

static void test_timer_status_get(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	k_timer_start(&timer, K_MSEC(30), K_MSEC(30));
	k_msleep(200);

	uint32_t status = k_timer_status_get(&timer);
	TEST_ASSERT_GREATER_OR_EQUAL(3, status);

	/* Second call should return 0 or small number (resets on read) */
	k_msleep(10);
	uint32_t status2 = k_timer_status_get(&timer);
	TEST_ASSERT_LESS_OR_EQUAL(2, status2);

	k_timer_stop(&timer);
}

static void test_timer_remaining_get(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);

	k_timer_start(&timer, K_MSEC(500), K_NO_WAIT);
	k_msleep(50);

	int64_t remaining = k_timer_remaining_get(&timer);
	/* Should have ~450ms remaining (allow wide margin) */
	TEST_ASSERT_GREATER_THAN(100, remaining);
	TEST_ASSERT_LESS_THAN(500, remaining);

	k_timer_stop(&timer);

	/* After stop, remaining should be 0 */
	remaining = k_timer_remaining_get(&timer);
	TEST_ASSERT_EQUAL(0, remaining);
}

static void test_timer_user_data(void)
{
	struct k_timer timer;
	k_timer_init(&timer, NULL, NULL);

	int ctx = 42;
	k_timer_user_data_set(&timer, &ctx);
	TEST_ASSERT_EQUAL_PTR(&ctx, k_timer_user_data_get(&timer));
}

static void test_timer_first_interval(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	/* First expiry at 100ms, then repeat every 50ms */
	k_timer_start(&timer, K_MSEC(100), K_MSEC(50));

	/* At 80ms: should not have fired yet */
	k_msleep(80);
	TEST_ASSERT_EQUAL(0, timer_count);

	/* At 130ms: first expiry should have fired (~100ms) */
	k_msleep(50);
	TEST_ASSERT_EQUAL(1, timer_count);

	/* At 330ms: periodic should have added ~4 more (at ~150, 200, 250, 300ms) */
	k_msleep(200);
	TEST_ASSERT_GREATER_OR_EQUAL(4, timer_count);

	k_timer_stop(&timer);
}

void test_k_timer_group(void)
{
	RUN_TEST(test_timer_one_shot);
	RUN_TEST(test_timer_periodic);
	RUN_TEST(test_timer_stop_callback);
	RUN_TEST(test_timer_status_get);
	RUN_TEST(test_timer_remaining_get);
	RUN_TEST(test_timer_user_data);
	RUN_TEST(test_timer_first_interval);
}
