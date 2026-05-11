/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Priority inheritance tests for k_mutex.
 *
 * These tests verify that k_mutex correctly inherits priority from
 * waiting threads, preventing priority inversion. Requires real
 * FreeRTOS scheduling (HW-only, not linux target).
 *
 * Thread priority mapping (FreeRTOS: higher number = higher priority):
 *   HIGH   = configMAX_PRIORITIES - 2
 *   MEDIUM = configMAX_PRIORITIES - 4
 *   LOW    = configMAX_PRIORITIES - 6
 *   (test runner is at priority 1 by default)
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "zephyr/kernel.h"

#define PRIO_HIGH   (configMAX_PRIORITIES - 2)
#define PRIO_MEDIUM (configMAX_PRIORITIES - 4)
#define PRIO_LOW    (configMAX_PRIORITIES - 6)

#define LOCK_TIMEOUT_MS   100
#define LOCK_TOLERANCE_MS 50

/* -------------------------------------------------------------------
 * Test 1: Priority boost -- verify uxTaskPriorityGet reflects PI
 *
 * Low-priority thread holds mutex. High-priority thread waits.
 * FreeRTOS should boost low's effective priority to match high's.
 * ---------------------------------------------------------------- */

static struct k_mutex pi_mutex;
static volatile UBaseType_t boosted_priority;
static volatile int low_done;

static void low_prio_holder(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	struct k_mutex *mtx = (struct k_mutex *)p1;

	k_mutex_lock(mtx, K_FOREVER);

	/* Signal that we hold the lock, then wait for high thread to block on it */
	k_msleep(50);

	/* By now the high-priority thread should be waiting on our mutex.
	 * FreeRTOS PI should have boosted our effective priority. */
	boosted_priority = uxTaskPriorityGet(NULL);

	k_mutex_unlock(mtx);
	low_done = 1;
	vTaskSuspend(NULL);
}

static volatile int high_got_mutex;

static void high_prio_waiter(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	struct k_mutex *mtx = (struct k_mutex *)p1;

	/* This should block until low releases */
	k_mutex_lock(mtx, K_FOREVER);
	high_got_mutex = 1;
	k_mutex_unlock(mtx);
	vTaskSuspend(NULL);
}

static void test_mutex_priority_boost(void)
{
	K_THREAD_STACK_DEFINE(low_stack, 4096);
	K_THREAD_STACK_DEFINE(high_stack, 4096);
	struct k_thread low_thread = {0};
	struct k_thread high_thread = {0};

	k_mutex_init(&pi_mutex);
	boosted_priority = 0;
	low_done = 0;
	high_got_mutex = 0;

	/* Start low-priority thread -- it will grab the mutex */
	k_thread_create(&low_thread, low_stack, K_THREAD_STACK_SIZEOF(low_stack), low_prio_holder,
			&pi_mutex, NULL, NULL, PRIO_LOW, 0, K_NO_WAIT);

	/* Let low thread run and grab mutex */
	k_msleep(10);

	/* Start high-priority thread -- it will block on the mutex */
	k_thread_create(&high_thread, high_stack, K_THREAD_STACK_SIZEOF(high_stack),
			high_prio_waiter, &pi_mutex, NULL, NULL, PRIO_HIGH, 0, K_NO_WAIT);

	/* Wait for everything to complete */
	k_msleep(200);

	/* Low thread should have been boosted to high's priority while holding mutex */
	TEST_ASSERT_EQUAL(PRIO_HIGH, boosted_priority);

	/* Both threads should have completed */
	TEST_ASSERT_EQUAL(1, low_done);
	TEST_ASSERT_EQUAL(1, high_got_mutex);

	k_thread_abort(&low_thread);
	k_thread_abort(&high_thread);
}

/* -------------------------------------------------------------------
 * Test 2: Priority restored after unlock
 *
 * After the high-priority waiter gets the mutex and the low thread
 * unlocks, the low thread's priority should return to its original.
 * ---------------------------------------------------------------- */

static volatile UBaseType_t priority_after_unlock;

static void low_prio_check_restore(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	struct k_mutex *mtx = (struct k_mutex *)p1;

	k_mutex_lock(mtx, K_FOREVER);
	k_msleep(50); /* let high thread block on us */
	k_mutex_unlock(mtx);

	/* After unlock, our priority should be back to PRIO_LOW */
	priority_after_unlock = uxTaskPriorityGet(NULL);

	vTaskSuspend(NULL);
}

static void test_mutex_priority_restore(void)
{
	K_THREAD_STACK_DEFINE(low_stack, 4096);
	K_THREAD_STACK_DEFINE(high_stack, 4096);
	struct k_thread low_thread = {0};
	struct k_thread high_thread = {0};

	k_mutex_init(&pi_mutex);
	priority_after_unlock = 0;
	high_got_mutex = 0;

	k_thread_create(&low_thread, low_stack, K_THREAD_STACK_SIZEOF(low_stack),
			low_prio_check_restore, &pi_mutex, NULL, NULL, PRIO_LOW, 0, K_NO_WAIT);

	k_msleep(10);

	k_thread_create(&high_thread, high_stack, K_THREAD_STACK_SIZEOF(high_stack),
			high_prio_waiter, &pi_mutex, NULL, NULL, PRIO_HIGH, 0, K_NO_WAIT);

	k_msleep(200);

	/* Low thread's priority should be restored to original after unlock */
	TEST_ASSERT_EQUAL(PRIO_LOW, priority_after_unlock);
	TEST_ASSERT_EQUAL(1, high_got_mutex);

	k_thread_abort(&low_thread);
	k_thread_abort(&high_thread);
}

/* -------------------------------------------------------------------
 * Test 3: Classic 3-thread inversion scenario
 *
 * Low holds mutex. High blocks on mutex. Medium becomes runnable.
 *
 * WITH PI: Low is boosted to High's priority, so Low runs before
 * Medium, releases mutex quickly, and High unblocks fast.
 *
 * WITHOUT PI: Medium would preempt Low (medium > low), delaying
 * the mutex release and causing High to wait much longer.
 *
 * We verify by measuring how long High is blocked. With PI it
 * should be approximately low_hold_time. Without PI it would be
 * low_hold_time + medium_work_time.
 * ---------------------------------------------------------------- */

#define LOW_HOLD_MS    50  /* how long low holds the mutex */
#define MEDIUM_WORK_MS 200 /* how long medium burns CPU */

static volatile int64_t high_blocked_ms;
static volatile int medium_finished;

/* Static allocation for 3-thread test (too large for main task stack) */
K_THREAD_STACK_DEFINE(pi3_low_stack, 4096);
K_THREAD_STACK_DEFINE(pi3_med_stack, 4096);
K_THREAD_STACK_DEFINE(pi3_high_stack, 4096);
static struct k_thread pi3_low_thread;
static struct k_thread pi3_med_thread;
static struct k_thread pi3_high_thread;

static void low_prio_timed(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	struct k_mutex *mtx = (struct k_mutex *)p1;

	k_mutex_lock(mtx, K_FOREVER);
	k_msleep(LOW_HOLD_MS);
	k_mutex_unlock(mtx);
	vTaskSuspend(NULL);
}

static void medium_prio_worker(void *p1, void *p2, void *p3)
{
	(void)p1;
	(void)p2;
	(void)p3;

	/* Busy work -- without PI this would delay the low thread */
	k_msleep(MEDIUM_WORK_MS);
	medium_finished = 1;
	vTaskSuspend(NULL);
}

static void high_prio_timed(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	struct k_mutex *mtx = (struct k_mutex *)p1;

	int64_t start = k_uptime_get();
	k_mutex_lock(mtx, K_FOREVER);
	high_blocked_ms = k_uptime_get() - start;
	k_mutex_unlock(mtx);
	vTaskSuspend(NULL);
}

static void test_mutex_pi_prevents_inversion(void)
{
	memset(&pi3_low_thread, 0, sizeof(pi3_low_thread));
	memset(&pi3_med_thread, 0, sizeof(pi3_med_thread));
	memset(&pi3_high_thread, 0, sizeof(pi3_high_thread));

	k_mutex_init(&pi_mutex);
	high_blocked_ms = 0;
	medium_finished = 0;

	/* 1. Low grabs mutex */
	k_thread_create(&pi3_low_thread, pi3_low_stack, K_THREAD_STACK_SIZEOF(pi3_low_stack),
			low_prio_timed, &pi_mutex, NULL, NULL, PRIO_LOW, 0, K_NO_WAIT);

	k_msleep(10); /* let low grab the mutex */

	/* 2. High tries to take mutex -- blocks, triggers PI boost on low */
	k_thread_create(&pi3_high_thread, pi3_high_stack, K_THREAD_STACK_SIZEOF(pi3_high_stack),
			high_prio_timed, &pi_mutex, NULL, NULL, PRIO_HIGH, 0, K_NO_WAIT);

	/* 3. Medium becomes runnable -- without PI, it would preempt low */
	k_thread_create(&pi3_med_thread, pi3_med_stack, K_THREAD_STACK_SIZEOF(pi3_med_stack),
			medium_prio_worker, NULL, NULL, NULL, PRIO_MEDIUM, 0, K_NO_WAIT);

	/* Wait for everything to finish */
	k_msleep(500);

	/* WITH PI: high should have been blocked ~LOW_HOLD_MS (50ms)
	 * because low was boosted and ran before medium.
	 *
	 * WITHOUT PI: high would be blocked ~LOW_HOLD_MS + MEDIUM_WORK_MS (250ms)
	 * because medium would preempt low.
	 *
	 * Use generous bounds: PI means < 100ms, no PI would be > 200ms */
	TEST_ASSERT_LESS_THAN(LOW_HOLD_MS + 50, high_blocked_ms);

	k_thread_abort(&pi3_low_thread);
	k_thread_abort(&pi3_med_thread);
	k_thread_abort(&pi3_high_thread);
}

/* -------------------------------------------------------------------
 * Test 4: Lock with timeout under contention
 *
 * One thread holds the mutex indefinitely. Main thread attempts
 * k_mutex_lock with a timeout and verifies -EAGAIN after the
 * expected duration.
 * ---------------------------------------------------------------- */

static volatile int holder_ready;

static void mutex_holder_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	struct k_mutex *mtx = (struct k_mutex *)p1;

	k_mutex_lock(mtx, K_FOREVER);
	holder_ready = 1;

	/* Hold until aborted */
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

static void test_mutex_lock_timeout_under_contention(void)
{
	K_THREAD_STACK_DEFINE(holder_stack, 4096);
	struct k_thread holder_thread = {0};
	struct k_mutex mtx;

	k_mutex_init(&mtx);
	holder_ready = 0;

	k_thread_create(&holder_thread, holder_stack, K_THREAD_STACK_SIZEOF(holder_stack),
			mutex_holder_entry, &mtx, NULL, NULL, 5, 0, K_NO_WAIT);

	/* Wait for holder to grab the mutex */
	while (!holder_ready) {
		k_msleep(5);
	}

	int64_t start = k_uptime_get();
	int ret = k_mutex_lock(&mtx, K_MSEC(LOCK_TIMEOUT_MS));
	int64_t elapsed = k_uptime_get() - start;

	TEST_ASSERT_EQUAL(-EAGAIN, ret);
	TEST_ASSERT_GREATER_OR_EQUAL(LOCK_TIMEOUT_MS - LOCK_TOLERANCE_MS, elapsed);
	TEST_ASSERT_LESS_OR_EQUAL(LOCK_TIMEOUT_MS + LOCK_TOLERANCE_MS, elapsed);

	k_thread_abort(&holder_thread);
}

void test_k_mutex_pi_group(void)
{
	RUN_TEST(test_mutex_priority_boost);
	RUN_TEST(test_mutex_priority_restore);
	RUN_TEST(test_mutex_pi_prevents_inversion);
	RUN_TEST(test_mutex_lock_timeout_under_contention);
}
