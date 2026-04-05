/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "zephyr/kernel.h"

/*
 * Note: All threads use xTaskCreateStatic (static TCB + stack).
 * Static tasks must NOT call vTaskDelete(NULL) -- FreeRTOS will try
 * to free() the TCB which isn't heap-allocated. Instead, threads
 * signal completion and suspend; the test cleans up via k_thread_abort.
 */

static volatile int thread_ran = 0;

static void thread_entry(void *p1, void *p2, void *p3)
{
    volatile int *flag = (volatile int *)p1;
    (void)p2;
    (void)p3;
    *flag = 42;
    vTaskSuspend(NULL); /* Suspend self -- test will abort us */
}

static void test_thread_create_and_run(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread = {0};
    thread_ran = 0;

    k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack),
                    thread_entry, (void *)&thread_ran, NULL, NULL,
                    5, 0, K_NO_WAIT);

    TEST_ASSERT_NOT_NULL(thread.handle);

    k_msleep(100);
    TEST_ASSERT_EQUAL(42, thread_ran);

    k_thread_abort(&thread);
}

static void test_thread_stack_space(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread = {0};
    thread_ran = 0;

    k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack),
                    thread_entry, (void *)&thread_ran, NULL, NULL,
                    5, 0, K_NO_WAIT);

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

static void long_running_entry(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;
    abort_thread_running = 1;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void test_thread_abort(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread = {0};
    abort_thread_running = 0;

    k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack),
                    long_running_entry, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    k_msleep(50);
    TEST_ASSERT_EQUAL(1, abort_thread_running);

    k_thread_abort(&thread);
    TEST_ASSERT_NULL(thread.handle);

    k_msleep(50);
}

static volatile int join_flag = 0;

static void short_entry(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;
    k_msleep(50);
    join_flag = 1;
    vTaskSuspend(NULL); /* Signal done, suspend -- join detects suspended state */
}

static void test_thread_join(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread = {0};
    join_flag = 0;

    k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack),
                    short_entry, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    /* Join should block until thread finishes */
    int ret = k_thread_join(&thread, K_SECONDS(2));
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, join_flag);

    k_thread_abort(&thread);
}

/* Test that all 3 parameters (p1, p2, p3) are passed correctly */
static volatile int p1_val, p2_val, p3_val;

static void three_arg_entry(void *p1, void *p2, void *p3)
{
    p1_val = (int)(intptr_t)p1;
    p2_val = (int)(intptr_t)p2;
    p3_val = (int)(intptr_t)p3;
    vTaskSuspend(NULL);
}

static void test_thread_three_params(void)
{
    K_THREAD_STACK_DEFINE(stack, 4096);
    struct k_thread thread = {0};
    p1_val = p2_val = p3_val = 0;

    k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack),
                    three_arg_entry,
                    (void *)(intptr_t)10,
                    (void *)(intptr_t)20,
                    (void *)(intptr_t)30,
                    5, 0, K_NO_WAIT);

    k_msleep(100);
    TEST_ASSERT_EQUAL(10, p1_val);
    TEST_ASSERT_EQUAL(20, p2_val);
    TEST_ASSERT_EQUAL(30, p3_val);

    k_thread_abort(&thread);
}

static volatile int deferred_ran;

static void deferred_entry(void *p1, void *p2, void *p3)
{
    (void)p2; (void)p3;
    volatile int *flag = (volatile int *)p1;
    *flag = 1;
    /* Return — k_thread_entry_wrapper suspends the task for us */
}

/* Static allocation for deferred tests -- k_thread with embedded k_timer
 * must outlive the test function to avoid corrupting the esp_timer handle */
K_THREAD_STACK_DEFINE(deferred_stack, 4096);
static struct k_thread deferred_thread;

static void test_thread_deferred_forever(void)
{
    memset(&deferred_thread, 0, sizeof(deferred_thread));
    deferred_ran = 0;

    /* K_FOREVER: thread should NOT run until resumed */
    k_thread_create(&deferred_thread, deferred_stack,
                    K_THREAD_STACK_SIZEOF(deferred_stack),
                    deferred_entry, (void *)&deferred_ran, NULL, NULL,
                    5, 0, K_FOREVER);

    k_msleep(100);
    TEST_ASSERT_EQUAL(0, deferred_ran);

    /* Resume it */
    k_thread_resume(&deferred_thread);
    k_msleep(50);
    TEST_ASSERT_EQUAL(1, deferred_ran);

    k_thread_abort(&deferred_thread);
}

static void test_thread_deferred_delay(void)
{
    memset(&deferred_thread, 0, sizeof(deferred_thread));
    deferred_ran = 0;

    /* K_MSEC(200): thread should start after ~200ms */
    k_thread_create(&deferred_thread, deferred_stack,
                    K_THREAD_STACK_SIZEOF(deferred_stack),
                    deferred_entry, (void *)&deferred_ran, NULL, NULL,
                    5, 0, K_MSEC(200));

    k_msleep(50);
    TEST_ASSERT_EQUAL(0, deferred_ran); /* too early */

    k_msleep(250);
    TEST_ASSERT_EQUAL(1, deferred_ran); /* should have started */

    k_thread_abort(&deferred_thread);
}

void test_k_thread_group(void)
{
    RUN_TEST(test_thread_create_and_run);
    RUN_TEST(test_thread_stack_space);
    RUN_TEST(test_thread_name_set);
    RUN_TEST(test_thread_abort);
    RUN_TEST(test_thread_join);
    RUN_TEST(test_thread_three_params);
    RUN_TEST(test_thread_deferred_forever);
    RUN_TEST(test_thread_deferred_delay);
}
