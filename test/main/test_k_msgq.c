/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

static void test_msgq_put_get(void)
{
    K_MSGQ_DEFINE(test_q, sizeof(int), 4, 4);
    k_msgq_init(&test_q, (char *)test_q.storage, sizeof(int), 4);

    int send_val = 42;
    TEST_ASSERT_EQUAL(0, k_msgq_put(&test_q, &send_val, K_NO_WAIT));
    TEST_ASSERT_EQUAL(1, k_msgq_num_used_get(&test_q));
    TEST_ASSERT_EQUAL(3, k_msgq_num_free_get(&test_q));

    int recv_val = 0;
    TEST_ASSERT_EQUAL(0, k_msgq_get(&test_q, &recv_val, K_NO_WAIT));
    TEST_ASSERT_EQUAL(42, recv_val);
    TEST_ASSERT_EQUAL(0, k_msgq_num_used_get(&test_q));
}

static void test_msgq_peek(void)
{
    K_MSGQ_DEFINE(test_q, sizeof(int), 4, 4);
    k_msgq_init(&test_q, (char *)test_q.storage, sizeof(int), 4);

    int val = 99;
    k_msgq_put(&test_q, &val, K_NO_WAIT);

    int peeked = 0;
    TEST_ASSERT_EQUAL(0, k_msgq_peek(&test_q, &peeked));
    TEST_ASSERT_EQUAL(99, peeked);
    /* Still in queue */
    TEST_ASSERT_EQUAL(1, k_msgq_num_used_get(&test_q));
}

static void test_msgq_purge(void)
{
    K_MSGQ_DEFINE(test_q, sizeof(int), 4, 4);
    k_msgq_init(&test_q, (char *)test_q.storage, sizeof(int), 4);

    int val = 1;
    k_msgq_put(&test_q, &val, K_NO_WAIT);
    k_msgq_put(&test_q, &val, K_NO_WAIT);
    k_msgq_put(&test_q, &val, K_NO_WAIT);

    k_msgq_purge(&test_q);
    TEST_ASSERT_EQUAL(0, k_msgq_num_used_get(&test_q));
}

static void test_msgq_full(void)
{
    K_MSGQ_DEFINE(test_q, sizeof(int), 2, 4);
    k_msgq_init(&test_q, (char *)test_q.storage, sizeof(int), 2);

    int val = 1;
    TEST_ASSERT_EQUAL(0, k_msgq_put(&test_q, &val, K_NO_WAIT));
    val = 2;
    TEST_ASSERT_EQUAL(0, k_msgq_put(&test_q, &val, K_NO_WAIT));
    TEST_ASSERT_EQUAL(0, k_msgq_num_free_get(&test_q));

    /* Queue full -- should fail */
    val = 3;
    TEST_ASSERT_NOT_EQUAL(0, k_msgq_put(&test_q, &val, K_NO_WAIT));
    TEST_ASSERT_EQUAL(2, k_msgq_num_used_get(&test_q));

    /* Drain one, then put should succeed */
    int out;
    TEST_ASSERT_EQUAL(0, k_msgq_get(&test_q, &out, K_NO_WAIT));
    TEST_ASSERT_EQUAL(1, out);
    TEST_ASSERT_EQUAL(0, k_msgq_put(&test_q, &val, K_NO_WAIT));
}

static void test_msgq_get_empty(void)
{
    K_MSGQ_DEFINE(test_q, sizeof(int), 4, 4);
    k_msgq_init(&test_q, (char *)test_q.storage, sizeof(int), 4);

    int val;
    TEST_ASSERT_NOT_EQUAL(0, k_msgq_get(&test_q, &val, K_NO_WAIT));
}

static void test_msgq_peek_empty(void)
{
    K_MSGQ_DEFINE(test_q, sizeof(int), 4, 4);
    k_msgq_init(&test_q, (char *)test_q.storage, sizeof(int), 4);

    int val;
    TEST_ASSERT_NOT_EQUAL(0, k_msgq_peek(&test_q, &val));
}

void test_k_msgq_group(void)
{
    RUN_TEST(test_msgq_put_get);
    RUN_TEST(test_msgq_peek);
    RUN_TEST(test_msgq_purge);
    RUN_TEST(test_msgq_full);
    RUN_TEST(test_msgq_get_empty);
    RUN_TEST(test_msgq_peek_empty);
}
