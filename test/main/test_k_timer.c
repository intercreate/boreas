/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

static volatile int timer_count;

static void test_timer_cb(struct k_timer *timer)
{
    timer_count++;
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

static void test_timer_user_data(void)
{
    struct k_timer timer;
    k_timer_init(&timer, NULL, NULL);

    int ctx = 42;
    k_timer_user_data_set(&timer, &ctx);
    TEST_ASSERT_EQUAL_PTR(&ctx, k_timer_user_data_get(&timer));
}

void test_k_timer_group(void)
{
    RUN_TEST(test_timer_one_shot);
    RUN_TEST(test_timer_periodic);
    RUN_TEST(test_timer_user_data);
}
