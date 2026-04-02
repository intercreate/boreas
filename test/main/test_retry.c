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

    TEST_ASSERT_EQUAL(d1.ticks, d2.ticks);
    TEST_ASSERT_EQUAL(d2.ticks, d3.ticks);
    TEST_ASSERT_TRUE(retry_exhausted(&ctx));
}

static void test_retry_exponential(void)
{
    struct retry_ctx ctx;
    retry_ctx_init(&ctx, 5, 100, 10000, RETRY_EXPONENTIAL);

    k_timeout_t d1 = retry_next_delay(&ctx); /* 100ms */
    k_timeout_t d2 = retry_next_delay(&ctx); /* 200ms */
    k_timeout_t d3 = retry_next_delay(&ctx); /* 400ms */

    TEST_ASSERT_TRUE(d2.ticks > d1.ticks);
    TEST_ASSERT_TRUE(d3.ticks > d2.ticks);
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

void test_retry_group(void)
{
    RUN_TEST(test_retry_fixed);
    RUN_TEST(test_retry_exponential);
    RUN_TEST(test_retry_exhausted);
    RUN_TEST(test_retry_reset);
}
