/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"
#include <errno.h>

static void test_mutex_init_lock_unlock(void)
{
	struct k_mutex mutex;
	TEST_ASSERT_EQUAL(0, k_mutex_init(&mutex));
	TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_mutex_unlock(&mutex));
}

static void test_mutex_reentrant(void)
{
	struct k_mutex mutex;
	k_mutex_init(&mutex);

	/* Zephyr k_mutex is reentrant -- same thread can lock multiple times */
	TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));

	/* Must unlock the same number of times */
	TEST_ASSERT_EQUAL(0, k_mutex_unlock(&mutex));
	TEST_ASSERT_EQUAL(0, k_mutex_unlock(&mutex));
	TEST_ASSERT_EQUAL(0, k_mutex_unlock(&mutex));

	/* Now fully released -- can lock again */
	TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_mutex_unlock(&mutex));
}

static void test_mutex_unlock_not_owner(void)
{
	struct k_mutex mutex;
	k_mutex_init(&mutex);

	/* Unlock without owning -- should return -EPERM */
	int ret = k_mutex_unlock(&mutex);
	TEST_ASSERT_EQUAL(-EPERM, ret);
}

static void test_mutex_lock_unlock_lock(void)
{
	struct k_mutex mutex;
	k_mutex_init(&mutex);

	/* Lock, unlock, then lock again to verify full release */
	TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_mutex_unlock(&mutex));
	TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_mutex_unlock(&mutex));
}

void test_k_mutex_group(void)
{
	RUN_TEST(test_mutex_init_lock_unlock);
	RUN_TEST(test_mutex_reentrant);
	RUN_TEST(test_mutex_unlock_not_owner);
	RUN_TEST(test_mutex_lock_unlock_lock);
}
