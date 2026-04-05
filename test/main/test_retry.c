/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zsys/retry.h"

static void test_retry_fixed(void)
{
    struct retry_ctx ctx;
    retry_ctx_init(&ctx, 3, 1000, 10000, RETRY_FIXED);

    k_timeout_t d1 = retry_next_delay(&ctx);
    k_timeout_t d2 = retry_next_delay(&ctx);
    k_timeout_t d3 = retry_next_delay(&ctx);

    TEST_ASSERT_EQUAL(d1.ms, d2.ms);
    TEST_ASSERT_EQUAL(d2.ms, d3.ms);
    TEST_ASSERT_TRUE(retry_exhausted(&ctx));
}

static void test_retry_exponential(void)
{
    struct retry_ctx ctx;
    retry_ctx_init(&ctx, 5, 100, 10000, RETRY_EXPONENTIAL);

    k_timeout_t d1 = retry_next_delay(&ctx); /* 100ms */
    k_timeout_t d2 = retry_next_delay(&ctx); /* 200ms */
    k_timeout_t d3 = retry_next_delay(&ctx); /* 400ms */

    TEST_ASSERT_TRUE(d2.ms > d1.ms);
    TEST_ASSERT_TRUE(d3.ms > d2.ms);
}

static void test_retry_exhausted(void)
{
    struct retry_ctx ctx;
    retry_ctx_init(&ctx, 2, 100, 1000, RETRY_FIXED);

    retry_next_delay(&ctx);
    retry_next_delay(&ctx);
    TEST_ASSERT_TRUE(retry_exhausted(&ctx));

    k_timeout_t d = retry_next_delay(&ctx);
    TEST_ASSERT_TRUE(k_timeout_is_no_wait(d));
}

static void test_retry_reset(void)
{
    struct retry_ctx ctx;
    retry_ctx_init(&ctx, 2, 100, 1000, RETRY_FIXED);

    retry_next_delay(&ctx);
    retry_next_delay(&ctx);
    TEST_ASSERT_TRUE(retry_exhausted(&ctx));

    retry_reset(&ctx);
    TEST_ASSERT_FALSE(retry_exhausted(&ctx));
    TEST_ASSERT_EQUAL(0, retry_attempt_get(&ctx));
}

static void test_retry_exp_jitter(void)
{
    struct retry_ctx ctx;
    retry_ctx_init(&ctx, 5, 100, 10000, RETRY_EXP_JITTER);

    k_timeout_t d1 = retry_next_delay(&ctx);
    k_timeout_t d2 = retry_next_delay(&ctx);

    /* Jitter adds 0-25%, so d1 should be 100-125 ticks-equivalent */
    TEST_ASSERT_GREATER_OR_EQUAL(K_MSEC(100).ms, d1.ms);
    TEST_ASSERT_LESS_OR_EQUAL(K_MSEC(126).ms, d1.ms);

    /* d2 base is 200ms + jitter, should be > d1 base of 100 */
    TEST_ASSERT_GREATER_OR_EQUAL(K_MSEC(200).ms, d2.ms);
}

static void test_retry_max_delay_cap(void)
{
    struct retry_ctx ctx;
    retry_ctx_init(&ctx, 10, 1000, 5000, RETRY_EXPONENTIAL);

    /* 1000, 2000, 4000, 5000(capped), 5000, ... */
    retry_next_delay(&ctx);
    retry_next_delay(&ctx);
    retry_next_delay(&ctx);
    k_timeout_t d4 = retry_next_delay(&ctx);
    k_timeout_t d5 = retry_next_delay(&ctx);

    /* Should be capped at max_delay */
    TEST_ASSERT_EQUAL(K_MSEC(5000).ms, d4.ms);
    TEST_ASSERT_EQUAL(K_MSEC(5000).ms, d5.ms);
}

void test_retry_group(void)
{
    RUN_TEST(test_retry_fixed);
    RUN_TEST(test_retry_exponential);
    RUN_TEST(test_retry_exhausted);
    RUN_TEST(test_retry_reset);
    RUN_TEST(test_retry_exp_jitter);
    RUN_TEST(test_retry_max_delay_cap);
}
