/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas test runner.
 */

#include "unity.h"

/* Test group declarations */
void test_timeout_group(void);
void test_slist_group(void);
void test_dlist_group(void);
void test_k_sem_group(void);
void test_k_mutex_group(void);
void test_k_msgq_group(void);
void test_k_event_group(void);
void test_retry_group(void);

/* Timer/work tests require esp_timer — not available on linux target */
#if !CONFIG_IDF_TARGET_LINUX
void test_k_timer_group(void);
void test_k_work_group(void);
#endif

void app_main(void)
{
    UNITY_BEGIN();

    test_timeout_group();
    test_slist_group();
    test_dlist_group();
    test_k_sem_group();
    test_k_mutex_group();
    test_k_msgq_group();
    test_k_event_group();
    test_retry_group();

#if !CONFIG_IDF_TARGET_LINUX
    test_k_timer_group();
    test_k_work_group();
#endif

    UNITY_END();
}
