/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

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

void test_k_sem_group(void)
{
    RUN_TEST(test_sem_init_and_count);
    RUN_TEST(test_sem_give_and_take);
    RUN_TEST(test_sem_take_timeout);
    RUN_TEST(test_sem_reset);
}
