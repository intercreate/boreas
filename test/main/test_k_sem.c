/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"
#include <errno.h>

static void test_sem_init_and_count(void)
{
	struct k_sem sem;
	TEST_ASSERT_EQUAL(0, k_sem_init(&sem, 3, 5));
	TEST_ASSERT_EQUAL(3, k_sem_count_get(&sem));
}

static void test_sem_give_and_take(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 0, 1);
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&sem));

	k_sem_give(&sem);
	TEST_ASSERT_EQUAL(1, k_sem_count_get(&sem));

	TEST_ASSERT_EQUAL(0, k_sem_take(&sem, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&sem));
}

static void test_sem_take_timeout(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 0, 1);

	/* Should fail immediately with K_NO_WAIT */
	TEST_ASSERT_NOT_EQUAL(0, k_sem_take(&sem, K_NO_WAIT));
}

static void test_sem_reset(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 5, 10);
	TEST_ASSERT_EQUAL(5, k_sem_count_get(&sem));

	k_sem_reset(&sem);
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&sem));
}

static void test_sem_give_at_limit(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 0, 3);

	k_sem_give(&sem);
	k_sem_give(&sem);
	k_sem_give(&sem);
	TEST_ASSERT_EQUAL(3, k_sem_count_get(&sem));

	/* Give beyond limit -- FreeRTOS counting semaphore silently ignores */
	k_sem_give(&sem);
	TEST_ASSERT_EQUAL(3, k_sem_count_get(&sem));
}

static void test_sem_init_at_limit(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 5, 5);
	TEST_ASSERT_EQUAL(5, k_sem_count_get(&sem));

	/* Already at limit, give should not increase */
	k_sem_give(&sem);
	TEST_ASSERT_EQUAL(5, k_sem_count_get(&sem));
}

static void test_sem_take_no_wait_empty(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 0, 1);

	/* K_NO_WAIT on empty should return -EBUSY */
	int ret = k_sem_take(&sem, K_NO_WAIT);
	TEST_ASSERT_EQUAL(-EBUSY, ret);
}

/* ----------------------------------------------------------------
 * K_SEM_DEFINE auto-init regression test.
 *
 * K_SEM_DEFINE must produce a fully-initialized semaphore before
 * main() runs (per Zephyr-compat contract). We verify this two ways:
 *   1. A separate constructor consumes the sem (k_sem_take/give); if
 *      K_SEM_DEFINE's auto-init didn't fire first, this would crash
 *      on a NULL handle. The flag captures success.
 *   2. The test reads the post-startup count to confirm it matches
 *      the macro's `initial` argument.
 * ---------------------------------------------------------------- */

K_SEM_DEFINE(_auto_sem, 2, 5);
static volatile bool _auto_sem_used_pre_main = false;

__attribute__((constructor)) static void _consume_auto_sem(void)
{
	/* Take 2 (drains it), give 1, leaves count=1. Will SIGSEGV /
	 * abort if K_SEM_DEFINE didn't auto-init the sem first. */
	if (k_sem_take(&_auto_sem, K_NO_WAIT) == 0 && k_sem_take(&_auto_sem, K_NO_WAIT) == 0) {
		k_sem_give(&_auto_sem);
		_auto_sem_used_pre_main = true;
	}
}

static void test_sem_auto_init_pre_main(void)
{
	TEST_ASSERT_TRUE_MESSAGE(_auto_sem_used_pre_main,
				 "K_SEM_DEFINE constructor did not run before main()");
	/* Constructor left count=1 after take/take/give */
	TEST_ASSERT_EQUAL(1, k_sem_count_get(&_auto_sem));
}

void test_k_sem_group(void)
{
	RUN_TEST(test_sem_init_and_count);
	RUN_TEST(test_sem_give_and_take);
	RUN_TEST(test_sem_take_timeout);
	RUN_TEST(test_sem_reset);
	RUN_TEST(test_sem_give_at_limit);
	RUN_TEST(test_sem_init_at_limit);
	RUN_TEST(test_sem_take_no_wait_empty);
	RUN_TEST(test_sem_auto_init_pre_main);
}
