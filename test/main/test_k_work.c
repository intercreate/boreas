/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

static volatile int work_executed;

static void test_work_handler(struct k_work *work)
{
    (void)work;
    work_executed++;
}

static void test_work_submit(void)
{
    /* Requires system work queue to be started — skip if not available */
    struct k_work work;
    k_work_init(&work, test_work_handler);
    work_executed = 0;

    int ret = k_work_submit(&work);
    if (ret == -1) {
        TEST_IGNORE_MESSAGE("System work queue not initialized");
        return;
    }

    k_msleep(100);
    TEST_ASSERT_EQUAL(1, work_executed);
}

static void test_work_is_pending(void)
{
    struct k_work work;
    k_work_init(&work, test_work_handler);

    TEST_ASSERT_FALSE(k_work_is_pending(&work));
}

void test_k_work_group(void)
{
    RUN_TEST(test_work_submit);
    RUN_TEST(test_work_is_pending);
}
