/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/sys/time_units.h"

static void test_k_msec(void)
{
	k_timeout_t t = K_MSEC(100);
	TEST_ASSERT_EQUAL(100000, t.us); /* 100ms = 100000us */
}

static void test_k_seconds(void)
{
	k_timeout_t t = K_SECONDS(2);
	TEST_ASSERT_EQUAL(2000000, t.us);
}

static void test_k_minutes(void)
{
	k_timeout_t t = K_MINUTES(1);
	TEST_ASSERT_EQUAL(60000000, t.us);
}

static void test_k_usec(void)
{
	/* K_USEC stores microseconds directly — no rounding */
	k_timeout_t t = K_USEC(5000);
	TEST_ASSERT_EQUAL(5000, t.us);

	/* Sub-ms precision preserved */
	k_timeout_t t2 = K_USEC(500);
	TEST_ASSERT_EQUAL(500, t2.us);
	TEST_ASSERT_FALSE(k_timeout_is_no_wait(t2));
}

static void test_k_ticks(void)
{
	/* K_TICKS converts FreeRTOS ticks to us */
	k_timeout_t t = K_TICKS(42);
	TEST_ASSERT_EQUAL(42 * portTICK_PERIOD_MS * 1000, t.us);
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
	/* Lossless us access for esp_timer */
	TEST_ASSERT_EQUAL(15000, k_timeout_to_us(K_MSEC(15)));
	TEST_ASSERT_EQUAL(500, k_timeout_to_us(K_USEC(500)));
	TEST_ASSERT_EQUAL(500000, k_timeout_to_us(K_MSEC(500)));
	TEST_ASSERT_EQUAL(0, k_timeout_to_us(K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_timeout_to_us(K_FOREVER));
}

static void test_k_timeout_to_ms(void)
{
	TEST_ASSERT_EQUAL(100, k_timeout_to_ms(K_MSEC(100)));
	TEST_ASSERT_EQUAL(0, k_timeout_to_ms(K_USEC(500)));  /* 500us < 1ms */
	TEST_ASSERT_EQUAL(1, k_timeout_to_ms(K_USEC(1500))); /* 1500us = 1ms */
	TEST_ASSERT_EQUAL(-1, k_timeout_to_ms(K_FOREVER));
}

static void test_k_timeout_eq(void)
{
	TEST_ASSERT_TRUE(K_TIMEOUT_EQ(K_MSEC(100), K_MSEC(100)));
	TEST_ASSERT_FALSE(K_TIMEOUT_EQ(K_MSEC(100), K_MSEC(200)));
	TEST_ASSERT_TRUE(K_TIMEOUT_EQ(K_FOREVER, K_FOREVER));
	TEST_ASSERT_TRUE(K_TIMEOUT_EQ(K_NO_WAIT, K_NO_WAIT));
	TEST_ASSERT_TRUE(K_TIMEOUT_EQ(K_USEC(500), K_USEC(500)));
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
	RUN_TEST(test_k_timeout_to_ms);
	RUN_TEST(test_k_timeout_eq);
}
