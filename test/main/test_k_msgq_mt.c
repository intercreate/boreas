/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Multi-thread msgq tests (require real scheduler, not linux target).
 */

#include <errno.h>
#include <string.h>

#include "unity.h"
#include "zephyr/kernel.h"

/* -----------------------------------------------------------
 * Test 1: put on full queue times out
 *
 * Fill a 3-slot queue, then attempt k_msgq_put with a timeout.
 * Verify -EAGAIN and that elapsed time is approximately the
 * timeout duration.
 * --------------------------------------------------------- */

#define MSGQ_DEPTH   3
#define MSG_SIZE     sizeof(uint32_t)
#define TIMEOUT_MS   100
#define TOLERANCE_MS 50

static uint8_t __attribute__((aligned(4))) msgq_buf[MSG_SIZE * MSGQ_DEPTH];
static struct k_msgq test_msgq;

static void test_msgq_put_timeout_on_full(void)
{
	TEST_ASSERT_EQUAL(0, k_msgq_init(&test_msgq, (char *)msgq_buf, MSG_SIZE, MSGQ_DEPTH));

	/* Fill the queue */
	for (uint32_t i = 0; i < MSGQ_DEPTH; i++) {
		TEST_ASSERT_EQUAL(0, k_msgq_put(&test_msgq, &i, K_NO_WAIT));
	}
	TEST_ASSERT_EQUAL(0, k_msgq_num_free_get(&test_msgq));

	/* Attempt put on full queue with timeout */
	uint32_t val = 99;
	int64_t start = k_uptime_get();
	int ret = k_msgq_put(&test_msgq, &val, K_MSEC(TIMEOUT_MS));
	int64_t elapsed = k_uptime_get() - start;

	TEST_ASSERT_EQUAL(-EAGAIN, ret);
	TEST_ASSERT_GREATER_OR_EQUAL(TIMEOUT_MS - TOLERANCE_MS, elapsed);
	TEST_ASSERT_LESS_OR_EQUAL(TIMEOUT_MS + TOLERANCE_MS, elapsed);
}

/* -----------------------------------------------------------
 * Test 2: put unblocks when consumer drains
 *
 * Fill queue. Spawn a consumer thread that drains one slot
 * after 50ms. Producer's put with 500ms timeout should
 * succeed well before the timeout expires.
 * --------------------------------------------------------- */

K_THREAD_STACK_DEFINE(drainer_stack, 2048);
static struct k_thread drainer_thread;

static void drainer_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	struct k_msgq *q = (struct k_msgq *)p1;

	k_msleep(50);

	uint32_t tmp;
	TEST_ASSERT_EQUAL(0, k_msgq_get(q, &tmp, K_NO_WAIT));

	vTaskSuspend(NULL);
}

static void test_msgq_put_unblocks_on_drain(void)
{
	TEST_ASSERT_EQUAL(0, k_msgq_init(&test_msgq, (char *)msgq_buf, MSG_SIZE, MSGQ_DEPTH));

	/* Fill the queue */
	for (uint32_t i = 0; i < MSGQ_DEPTH; i++) {
		TEST_ASSERT_EQUAL(0, k_msgq_put(&test_msgq, &i, K_NO_WAIT));
	}
	TEST_ASSERT_EQUAL(0, k_msgq_num_free_get(&test_msgq));

	/* Spawn consumer that drains one slot after 50ms */
	memset(&drainer_thread, 0, sizeof(drainer_thread));
	k_thread_create(&drainer_thread, drainer_stack, K_THREAD_STACK_SIZEOF(drainer_stack),
			drainer_entry, &test_msgq, NULL, NULL, 5, 0, K_NO_WAIT);

	/* Producer put with generous timeout — should unblock when drainer frees a slot */
	uint32_t val = 42;
	int64_t start = k_uptime_get();
	int ret = k_msgq_put(&test_msgq, &val, K_MSEC(500));
	int64_t elapsed = k_uptime_get() - start;

	TEST_ASSERT_EQUAL(0, ret);
	/* Should have unblocked around 50ms, well before the 500ms timeout */
	TEST_ASSERT_LESS_THAN(200, elapsed);

	k_thread_abort(&drainer_thread);
}

void test_k_msgq_mt_group(void)
{
	RUN_TEST(test_msgq_put_timeout_on_full);
	RUN_TEST(test_msgq_put_unblocks_on_drain);
}
