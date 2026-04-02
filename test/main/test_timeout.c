/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/sys/time_units.h"

static void test_k_msec(void)
{
    k_timeout_t t = K_MSEC(100);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(100), t.ticks);
}

static void test_k_seconds(void)
{
    k_timeout_t t = K_SECONDS(2);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(2000), t.ticks);
}

static void test_k_minutes(void)
{
    k_timeout_t t = K_MINUTES(1);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(60000), t.ticks);
}

static void test_k_usec(void)
{
    /* K_USEC converts to ticks via ms -- sub-ms rounds to 0 ticks */
    k_timeout_t t = K_USEC(5000);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(5), t.ticks);
}

static void test_k_ticks(void)
{
    k_timeout_t t = K_TICKS(42);
    TEST_ASSERT_EQUAL(42, t.ticks);
}

static void test_k_forever(void)
{
    k_timeout_t t = K_FOREVER;
    TEST_ASSERT_TRUE(k_timeout_is_forever(t));
    TEST_ASSERT_EQUAL(portMAX_DELAY, k_timeout_to_ticks(t));
}

static void test_k_no_wait(void)
{
    k_timeout_t t = K_NO_WAIT;
    TEST_ASSERT_TRUE(k_timeout_is_no_wait(t));
    TEST_ASSERT_EQUAL(0, k_timeout_to_ticks(t));
}

static void test_k_no_wait_is_not_forever(void)
{
    TEST_ASSERT_FALSE(k_timeout_is_forever(K_NO_WAIT));
    TEST_ASSERT_FALSE(k_timeout_is_no_wait(K_FOREVER));
}

void test_timeout_group(void)
{
    RUN_TEST(test_k_msec);
    RUN_TEST(test_k_seconds);
    RUN_TEST(test_k_minutes);
    RUN_TEST(test_k_usec);
    RUN_TEST(test_k_ticks);
    RUN_TEST(test_k_forever);
    RUN_TEST(test_k_no_wait);
    RUN_TEST(test_k_no_wait_is_not_forever);
}
