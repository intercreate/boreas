/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

/*
 * Note: All threads use xTaskCreateStatic (static TCB + stack).
 * Static tasks must NOT call vTaskDelete(NULL) — FreeRTOS will try
 * to free() the TCB which isn't heap-allocated. Instead, threads
 * signal completion and suspend; the test cleans up via k_thread_abort.
 */

static volatile int thread_ran = 0;

static void thread_entry(void *p1)
{
    volatile int *flag = (volatile int *)p1;
    *flag = 42;
    vTaskSuspend(NULL); /* Suspend self — test will abort us */
}

static void test_thread_create_and_run(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread;
    thread_ran = 0;

    thread.handle = xTaskCreateStatic(
        thread_entry, "test_thr",
        K_THREAD_STACK_SIZEOF(stack) / sizeof(StackType_t),
        (void *)&thread_ran, 5, stack, &thread.tcb);

    TEST_ASSERT_NOT_NULL(thread.handle);

    k_msleep(100);
    TEST_ASSERT_EQUAL(42, thread_ran);

    k_thread_abort(&thread);
}

static void test_thread_stack_space(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread;
    thread_ran = 0;

    thread.handle = xTaskCreateStatic(
        thread_entry, "test_stk",
        K_THREAD_STACK_SIZEOF(stack) / sizeof(StackType_t),
        (void *)&thread_ran, 5, stack, &thread.tcb);

    k_msleep(100);

    size_t unused = 0;
    int ret = k_thread_stack_space_get(&thread, &unused);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_GREATER_THAN(0, unused);
    TEST_ASSERT_LESS_THAN(4096, unused);

    k_thread_abort(&thread);
}

static void test_thread_name_set(void)
{
    struct k_thread thread = {0};
    k_thread_name_set(&thread, "my_thread");
    TEST_ASSERT_EQUAL_STRING("my_thread", thread.name);
}

static volatile int abort_thread_running = 0;

static void long_running_entry(void *p1)
{
    abort_thread_running = 1;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void test_thread_abort(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread;
    abort_thread_running = 0;

    thread.handle = xTaskCreateStatic(
        long_running_entry, "abort_thr",
        K_THREAD_STACK_SIZEOF(stack) / sizeof(StackType_t),
        NULL, 5, stack, &thread.tcb);

    k_msleep(50);
    TEST_ASSERT_EQUAL(1, abort_thread_running);

    k_thread_abort(&thread);
    TEST_ASSERT_NULL(thread.handle);

    k_msleep(50);
}

static volatile int join_flag = 0;

static void short_entry(void *p1)
{
    k_msleep(50);
    join_flag = 1;
    vTaskSuspend(NULL); /* Signal done, suspend — join detects suspended state */
}

static void test_thread_join(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread;
    join_flag = 0;

    thread.handle = xTaskCreateStatic(
        short_entry, "join_thr",
        K_THREAD_STACK_SIZEOF(stack) / sizeof(StackType_t),
        NULL, 5, stack, &thread.tcb);

    /* Join should block until thread finishes */
    int ret = k_thread_join(&thread, K_SECONDS(2));
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, join_flag);

    k_thread_abort(&thread);
}

void test_k_thread_group(void)
{
    RUN_TEST(test_thread_create_and_run);
    RUN_TEST(test_thread_stack_space);
    RUN_TEST(test_thread_name_set);
    RUN_TEST(test_thread_abort);
    RUN_TEST(test_thread_join);
}
