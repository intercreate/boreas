/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

static volatile int work_executed;

static void test_work_handler(struct k_work *work)
{
	(void)work;
	work_executed++;
}

static void test_work_submit(void)
{

	struct k_work work;
	k_work_init(&work, test_work_handler);
	work_executed = 0;

	TEST_ASSERT_EQUAL(0, k_work_submit(&work));
	k_msleep(100);
	TEST_ASSERT_EQUAL(1, work_executed);
}

static void test_work_submit_idempotent(void)
{

	struct k_work work;
	k_work_init(&work, test_work_handler);
	work_executed = 0;

	TEST_ASSERT_EQUAL(0, k_work_submit(&work));
	k_work_submit(&work);
	k_msleep(100);
	TEST_ASSERT_GREATER_OR_EQUAL(1, work_executed);
}

static void test_work_cancel(void)
{
	struct k_work work;
	k_work_init(&work, test_work_handler);

	/* Cancel on non-queued work should succeed */
	TEST_ASSERT_TRUE(k_work_cancel(&work));
}

static void test_work_is_pending(void)
{
	struct k_work work;
	k_work_init(&work, test_work_handler);

	TEST_ASSERT_FALSE(k_work_is_pending(&work));
}

static void test_work_delayable(void)
{

	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	k_work_schedule(&dwork, K_MSEC(50));
	TEST_ASSERT_TRUE(k_work_delayable_is_pending(&dwork));

	k_msleep(10);
	TEST_ASSERT_EQUAL(0, work_executed);

	k_msleep(200);
	TEST_ASSERT_EQUAL(1, work_executed);
}

static void test_work_cancel_delayable(void)
{

	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	k_work_schedule(&dwork, K_MSEC(200));
	k_msleep(10);
	k_work_cancel_delayable(&dwork);

	k_msleep(300);
	TEST_ASSERT_EQUAL(0, work_executed);
}

static void test_work_reschedule(void)
{

	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	/* Schedule for 500ms, then reschedule to 50ms */
	k_work_schedule(&dwork, K_MSEC(500));
	k_msleep(10);
	k_work_reschedule(&dwork, K_MSEC(50));

	/* Should fire at ~60ms total, not 500ms */
	k_msleep(200);
	TEST_ASSERT_EQUAL(1, work_executed);
}

static void test_work_delayable_remaining(void)
{

	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	k_work_schedule(&dwork, K_MSEC(500));
	k_msleep(50);

	int64_t remaining = k_work_delayable_remaining_get(&dwork);
	TEST_ASSERT_GREATER_THAN(100, remaining);
	TEST_ASSERT_LESS_THAN(500, remaining);

	k_work_cancel_delayable(&dwork);
}

static volatile int custom_wq_executed;

static void custom_wq_handler(struct k_work *work)
{
	(void)work;
	custom_wq_executed++;
}

static void test_work_submit_to_queue(void)
{

	struct k_work work;
	k_work_init(&work, custom_wq_handler);
	custom_wq_executed = 0;

	/* submit_to_queue with explicit queue (vs submit which uses sys queue) */
	TEST_ASSERT_EQUAL(0, k_work_submit_to_queue(&k_sys_work_q, &work));
	k_msleep(100);
	TEST_ASSERT_EQUAL(1, custom_wq_executed);
}

static volatile int slow_work_done;

static void slow_work_handler(struct k_work *work)
{
	(void)work;
	k_msleep(200);
	slow_work_done = 1;
}

static void test_work_flush(void)
{
	struct k_work work;
	k_work_init(&work, slow_work_handler);
	slow_work_done = 0;

	k_work_submit(&work);

	/* Flush should block until handler finishes (~200ms) */
	struct k_work_sync sync;
	int64_t start = k_uptime_get();
	k_work_flush(&work, &sync);
	int64_t elapsed = k_uptime_get() - start;

	TEST_ASSERT_EQUAL(1, slow_work_done);
	TEST_ASSERT_GREATER_OR_EQUAL(100, elapsed);
}

static void test_work_flush_not_pending(void)
{
	struct k_work work;
	k_work_init(&work, test_work_handler);

	/* Flush on non-pending work should return immediately */
	struct k_work_sync sync;
	int ret = k_work_flush(&work, &sync);
	TEST_ASSERT_EQUAL(0, ret);
}

void test_k_work_group(void)
{
	RUN_TEST(test_work_submit);
	RUN_TEST(test_work_submit_idempotent);
	RUN_TEST(test_work_cancel);
	RUN_TEST(test_work_is_pending);
	RUN_TEST(test_work_delayable);
	RUN_TEST(test_work_cancel_delayable);
	RUN_TEST(test_work_reschedule);
	RUN_TEST(test_work_delayable_remaining);
	RUN_TEST(test_work_submit_to_queue);
	RUN_TEST(test_work_flush);
	RUN_TEST(test_work_flush_not_pending);
}
