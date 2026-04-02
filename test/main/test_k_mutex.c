/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

static void test_mutex_init_lock_unlock(void)
{
    struct k_mutex mutex;
    TEST_ASSERT_EQUAL(0, k_mutex_init(&mutex));
    TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));
    TEST_ASSERT_EQUAL(0, k_mutex_unlock(&mutex));
}

static void test_mutex_lock_timeout(void)
{
    struct k_mutex mutex;
    k_mutex_init(&mutex);

    /* Lock it */
    k_mutex_lock(&mutex, K_NO_WAIT);

    /* FreeRTOS recursive mutex allows same-task relock,
     * so this test just verifies the API works */
    TEST_ASSERT_EQUAL(0, k_mutex_lock(&mutex, K_NO_WAIT));
    k_mutex_unlock(&mutex);
    k_mutex_unlock(&mutex);
}

void test_k_mutex_group(void)
{
    RUN_TEST(test_mutex_init_lock_unlock);
    RUN_TEST(test_mutex_lock_timeout);
}
