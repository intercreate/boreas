/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

#define EVT_A BIT(0)
#define EVT_B BIT(1)
#define EVT_C BIT(2)

static void test_event_set_and_wait(void)
{
    struct k_event evt;
    k_event_init(&evt);

    k_event_set(&evt, EVT_A | EVT_B);

    uint32_t got = k_event_wait(&evt, EVT_A, false, K_NO_WAIT);
    TEST_ASSERT_EQUAL(EVT_A, got & EVT_A);
}

static void test_event_wait_all(void)
{
    struct k_event evt;
    k_event_init(&evt);

    k_event_set(&evt, EVT_A | EVT_B);

    /* Should succeed — both bits set */
    uint32_t got = k_event_wait_all(&evt, EVT_A | EVT_B, false, K_NO_WAIT);
    TEST_ASSERT_EQUAL(EVT_A | EVT_B, got);
}

static void test_event_wait_all_missing(void)
{
    struct k_event evt;
    k_event_init(&evt);

    k_event_set(&evt, EVT_A); /* Only A, not B */

    /* Should timeout — waiting for both A and B */
    uint32_t got = k_event_wait_all(&evt, EVT_A | EVT_B, false, K_NO_WAIT);
    TEST_ASSERT_NOT_EQUAL(EVT_A | EVT_B, got);
}

static void test_event_clear(void)
{
    struct k_event evt;
    k_event_init(&evt);

    k_event_set(&evt, EVT_A | EVT_B | EVT_C);
    k_event_clear(&evt, EVT_B);

    uint32_t got = k_event_wait(&evt, EVT_B, false, K_NO_WAIT);
    TEST_ASSERT_EQUAL(0, got & EVT_B);
}

void test_k_event_group(void)
{
    RUN_TEST(test_event_set_and_wait);
    RUN_TEST(test_event_wait_all);
    RUN_TEST(test_event_wait_all_missing);
    RUN_TEST(test_event_clear);
}
