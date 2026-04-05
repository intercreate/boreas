/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/sys/time_units.h"

static void test_k_msec(void)
{
    k_timeout_t t = K_MSEC(100);
    TEST_ASSERT_EQUAL(100, t.ms);
}

static void test_k_seconds(void)
{
    k_timeout_t t = K_SECONDS(2);
    TEST_ASSERT_EQUAL(2000, t.ms);
}

static void test_k_minutes(void)
{
    k_timeout_t t = K_MINUTES(1);
    TEST_ASSERT_EQUAL(60000, t.ms);
}

static void test_k_usec(void)
{
    /* K_USEC rounds up to nearest millisecond */
    k_timeout_t t = K_USEC(5000);
    TEST_ASSERT_EQUAL(5, t.ms);

    /* Sub-ms rounds up to 1ms, not 0 (K_NO_WAIT) */
    k_timeout_t t2 = K_USEC(500);
    TEST_ASSERT_EQUAL(1, t2.ms);
    TEST_ASSERT_FALSE(k_timeout_is_no_wait(t2));
}

static void test_k_ticks(void)
{
    /* K_TICKS converts FreeRTOS ticks to ms */
    k_timeout_t t = K_TICKS(42);
    TEST_ASSERT_EQUAL(42 * portTICK_PERIOD_MS, t.ms);
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

static void test_k_timeout_to_us(void)
{
    /* Lossless ms -> us conversion for esp_timer */
    TEST_ASSERT_EQUAL(15000, k_timeout_to_us(K_MSEC(15)));
    TEST_ASSERT_EQUAL(500000, k_timeout_to_us(K_MSEC(500)));
    TEST_ASSERT_EQUAL(0, k_timeout_to_us(K_NO_WAIT));
    TEST_ASSERT_EQUAL(0, k_timeout_to_us(K_FOREVER));
}

static void test_k_timeout_eq(void)
{
    TEST_ASSERT_TRUE(K_TIMEOUT_EQ(K_MSEC(100), K_MSEC(100)));
    TEST_ASSERT_FALSE(K_TIMEOUT_EQ(K_MSEC(100), K_MSEC(200)));
    TEST_ASSERT_TRUE(K_TIMEOUT_EQ(K_FOREVER, K_FOREVER));
    TEST_ASSERT_TRUE(K_TIMEOUT_EQ(K_NO_WAIT, K_NO_WAIT));
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
    RUN_TEST(test_k_timeout_to_us);
    RUN_TEST(test_k_timeout_eq);
}
