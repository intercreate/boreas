/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Multi-thread event tests (require real scheduler, not linux target).
 */

#include <string.h>

#include "unity.h"
#include "zephyr/kernel.h"

#define EVT_DONE BIT(0)
#define EVT_GO   BIT(1)

/* ----------------------------------------------------------------
 * Thread sets an event after a short delay
 * ---------------------------------------------------------------- */

static struct k_event mt_evt;
K_THREAD_STACK_DEFINE(setter_stack, 2048);
static struct k_thread setter_thread;

static void event_setter_entry(void *p1, void *p2, void *p3)
{
    (void)p2;
    (void)p3;
    uint32_t bits = (uint32_t)(uintptr_t)p1;

    k_msleep(50);
    k_event_set(&mt_evt, bits);
}

static void test_event_wait_from_thread(void)
{
    k_event_init(&mt_evt);
    memset(&setter_thread, 0, sizeof(setter_thread));

    /* Spawn thread that will set EVT_DONE after 50ms */
    k_thread_create(&setter_thread, setter_stack, sizeof(setter_stack),
                    event_setter_entry,
                    (void *)(uintptr_t)EVT_DONE, NULL, NULL,
                    5, 0, K_NO_WAIT);

    /* Block waiting for the event -- should wake when thread sets it */
    uint32_t got = k_event_wait(&mt_evt, EVT_DONE, false, K_MSEC(500));
    TEST_ASSERT_EQUAL(EVT_DONE, got & EVT_DONE);

    k_thread_abort(&setter_thread);
}

/* ----------------------------------------------------------------
 * Thread sets multiple events, waiter uses wait_all
 * ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(setter2_stack, 2048);
static struct k_thread setter2_thread;

static void event_multi_setter_entry(void *p1, void *p2, void *p3)
{
    (void)p2;
    (void)p3;
    struct k_event *evt = (struct k_event *)p1;

    k_msleep(30);
    k_event_set(evt, EVT_DONE);

    k_msleep(30);
    k_event_set(evt, EVT_GO);
}

static void test_event_wait_all_from_thread(void)
{
    k_event_init(&mt_evt);
    memset(&setter2_thread, 0, sizeof(setter2_thread));

    k_thread_create(&setter2_thread, setter2_stack, sizeof(setter2_stack),
                    event_multi_setter_entry,
                    &mt_evt, NULL, NULL,
                    5, 0, K_NO_WAIT);

    /* Wait for BOTH bits -- thread sets them ~30ms apart */
    uint32_t got = k_event_wait_all(&mt_evt, EVT_DONE | EVT_GO, false,
                                    K_MSEC(500));
    TEST_ASSERT_EQUAL(EVT_DONE | EVT_GO, got);

    k_thread_abort(&setter2_thread);
}

/* ----------------------------------------------------------------
 * Wait with reset -- verify bits are cleared after wake
 * ---------------------------------------------------------------- */

static void test_event_wait_reset_from_thread(void)
{
    k_event_init(&mt_evt);
    memset(&setter_thread, 0, sizeof(setter_thread));

    k_thread_create(&setter_thread, setter_stack, sizeof(setter_stack),
                    event_setter_entry,
                    (void *)(uintptr_t)EVT_DONE, NULL, NULL,
                    5, 0, K_NO_WAIT);

    /* Wait with reset=true */
    uint32_t got = k_event_wait(&mt_evt, EVT_DONE, true, K_MSEC(500));
    TEST_ASSERT_EQUAL(EVT_DONE, got & EVT_DONE);

    /* EVT_DONE should be cleared now */
    got = k_event_wait(&mt_evt, EVT_DONE, false, K_NO_WAIT);
    TEST_ASSERT_EQUAL(0, got);

    k_thread_abort(&setter_thread);
}

void test_k_event_mt_group(void)
{
    RUN_TEST(test_event_wait_from_thread);
    RUN_TEST(test_event_wait_all_from_thread);
    RUN_TEST(test_event_wait_reset_from_thread);
}
