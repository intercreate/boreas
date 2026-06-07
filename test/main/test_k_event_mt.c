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
	k_thread_create(&setter_thread, setter_stack, sizeof(setter_stack), event_setter_entry,
			(void *)(uintptr_t)EVT_DONE, NULL, NULL, 5, 0, K_NO_WAIT);

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
	k_event_post(evt, EVT_DONE); /* post MERGES; set would replace */

	k_msleep(30);
	k_event_post(evt, EVT_GO);
}

static void test_event_wait_all_from_thread(void)
{
	k_event_init(&mt_evt);
	memset(&setter2_thread, 0, sizeof(setter2_thread));

	k_thread_create(&setter2_thread, setter2_stack, sizeof(setter2_stack),
			event_multi_setter_entry, &mt_evt, NULL, NULL, 5, 0, K_NO_WAIT);

	/* Wait for BOTH bits -- thread sets them ~30ms apart */
	uint32_t got = k_event_wait_all(&mt_evt, EVT_DONE | EVT_GO, false, K_MSEC(500));
	TEST_ASSERT_EQUAL(EVT_DONE | EVT_GO, got);

	k_thread_abort(&setter2_thread);
}

/* ----------------------------------------------------------------
 * Blocking wait_safe -- matched bits are consumed on wake
 * (upstream parity: plain wait leaves them set; reset=true zeroes the
 * whole tracked set BEFORE waiting, it does not clear after)
 * ---------------------------------------------------------------- */

static void test_event_wait_safe_from_thread(void)
{
	k_event_init(&mt_evt);
	memset(&setter_thread, 0, sizeof(setter_thread));

	k_thread_create(&setter_thread, setter_stack, sizeof(setter_stack), event_setter_entry,
			(void *)(uintptr_t)EVT_DONE, NULL, NULL, 5, 0, K_NO_WAIT);

	/* Block until the setter posts, consuming the matched bits. */
	uint32_t got = k_event_wait_safe(&mt_evt, EVT_DONE, false, K_MSEC(500));
	TEST_ASSERT_EQUAL(EVT_DONE, got);

	/* Consumed atomically on wake. */
	TEST_ASSERT_EQUAL(0, k_event_test(&mt_evt, EVT_DONE));

	k_thread_abort(&setter_thread);
}

/* ----------------------------------------------------------------
 * One post wakes ALL satisfied waiters -- including when an earlier
 * _safe waiter consumes the matched bits. Regression for the
 * clear-mid-walk starvation found in review: clears must be
 * accumulated and applied after the waiter walk (upstream order), or
 * the second waiter here never wakes.
 * ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(safe_waiter_stack, 2048);
K_THREAD_STACK_DEFINE(plain_waiter_stack, 2048);
static struct k_thread safe_waiter_thread;
static struct k_thread plain_waiter_thread;
static volatile uint32_t safe_waiter_got;
static volatile uint32_t plain_waiter_got;

static void safe_waiter_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	safe_waiter_got = k_event_wait_safe((struct k_event *)p1, EVT_GO, false, K_MSEC(500));
}

static void plain_waiter_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	plain_waiter_got = k_event_wait((struct k_event *)p1, EVT_GO, false, K_MSEC(500));
}

static void test_event_post_wakes_all_with_safe_waiter(void)
{
	k_event_init(&mt_evt);
	memset(&safe_waiter_thread, 0, sizeof(safe_waiter_thread));
	memset(&plain_waiter_thread, 0, sizeof(plain_waiter_thread));
	safe_waiter_got = 0xdead;
	plain_waiter_got = 0xdead;

	/* Safe waiter enqueues FIRST so its consume would starve the
	 * plain waiter under the buggy ordering. */
	k_thread_create(&safe_waiter_thread, safe_waiter_stack,
			K_THREAD_STACK_SIZEOF(safe_waiter_stack), safe_waiter_entry, &mt_evt, NULL,
			NULL, 5, 0, K_NO_WAIT);
	k_msleep(20);
	k_thread_create(&plain_waiter_thread, plain_waiter_stack,
			K_THREAD_STACK_SIZEOF(plain_waiter_stack), plain_waiter_entry, &mt_evt,
			NULL, NULL, 5, 0, K_NO_WAIT);
	k_msleep(20);

	k_event_post(&mt_evt, EVT_GO); /* ONE post must wake BOTH */

	TEST_ASSERT_EQUAL(0, k_thread_join(&safe_waiter_thread, K_SECONDS(2)));
	TEST_ASSERT_EQUAL(0, k_thread_join(&plain_waiter_thread, K_SECONDS(2)));
	TEST_ASSERT_EQUAL(EVT_GO, safe_waiter_got);
	TEST_ASSERT_EQUAL(EVT_GO, plain_waiter_got);
	/* The safe waiter consumed the bits after the walk. */
	TEST_ASSERT_EQUAL(0, k_event_test(&mt_evt, EVT_GO));
}

void test_k_event_mt_group(void)
{
	RUN_TEST(test_event_wait_from_thread);
	RUN_TEST(test_event_wait_all_from_thread);
	RUN_TEST(test_event_wait_safe_from_thread);
	RUN_TEST(test_event_post_wakes_all_with_safe_waiter);
}
