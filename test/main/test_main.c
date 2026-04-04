/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas test runner.
 */

#include "unity.h"

/* Test group declarations -- always available */
void test_timeout_group(void);
void test_slist_group(void);
void test_dlist_group(void);
void test_k_sem_group(void);
void test_k_mutex_group(void);
void test_k_msgq_group(void);
void test_k_event_group(void);
void test_retry_group(void);
void test_log_group(void);

/* Shell tests -- available on all targets when ZSHELL is enabled */
#if defined(CONFIG_ZSHELL)
void test_shell_group(void);
#endif

/* These require esp_timer / real HW -- not available on linux target */
#if !CONFIG_IDF_TARGET_LINUX
void test_uptime_group(void);
void test_sleep_group(void);
void test_k_timer_group(void);
void test_k_work_group(void);
void test_k_thread_group(void);
#endif

void app_main(void)
{
    UNITY_BEGIN();

    /* Layer 0: Foundation */
    test_timeout_group();
    test_slist_group();
    test_dlist_group();

    /* Layer 1: Kernel primitives */
    test_k_sem_group();
    test_k_mutex_group();
    test_k_msgq_group();
    test_k_event_group();

    /* System services */
    test_retry_group();
    test_log_group();

#if defined(CONFIG_ZSHELL)
    /* Shell */
    test_shell_group();
#endif

#if !CONFIG_IDF_TARGET_LINUX
    /* Layer 0: Uptime & Sleep (need esp_timer) */
    test_uptime_group();
    test_sleep_group();

    /* Layer 1: Thread */
    test_k_thread_group();

    /* Layer 2: Timer & Work Queue */
    test_k_timer_group();
    test_k_work_group();
#endif

    UNITY_END();
}
