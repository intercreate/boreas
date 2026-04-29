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
 * K_SEM_DEFINE must produce a fully-initialized semaphore by the
 * time app_main() runs. We verify the post-startup count matches
 * the macro's `initial` argument and that take/give work without
 * an explicit k_sem_init() call.
 *
 * Note: ESP-IDF Xtensa iterates .init_array in descending order, so
 * we cannot reliably consume the sem from another constructor in
 * the same TU (see K_SEM_DEFINE doxygen). The "ready by app_main"
 * contract is what's actually deliverable; that's what we test.
 * ---------------------------------------------------------------- */

K_SEM_DEFINE(auto_sem, 2, 5);

static void test_sem_auto_init_pre_main(void)
{
	/* Initial count from the macro must be visible by app_main. */
	TEST_ASSERT_EQUAL(2, k_sem_count_get(&auto_sem));

	/* And the sem must be usable without an explicit k_sem_init(). */
	TEST_ASSERT_EQUAL(0, k_sem_take(&auto_sem, K_NO_WAIT));
	TEST_ASSERT_EQUAL(1, k_sem_count_get(&auto_sem));

	k_sem_give(&auto_sem);
	TEST_ASSERT_EQUAL(2, k_sem_count_get(&auto_sem));
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
