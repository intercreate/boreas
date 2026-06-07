/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Multi-thread event tests (require real scheduler, not linux target).
 */

#include <string.h>

#include "unity.h"
#include "zephyr/kernel.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_K_TIMER_DISPATCH_ISR
#include "esp_attr.h"
#endif

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
	k_thread_create(&setter_thread, setter_stack, K_THREAD_STACK_SIZEOF(setter_stack),
			event_setter_entry, (void *)(uintptr_t)EVT_DONE, NULL, NULL, 5, 0,
			K_NO_WAIT);

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

	k_thread_create(&setter2_thread, setter2_stack, K_THREAD_STACK_SIZEOF(setter2_stack),
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

	k_thread_create(&setter_thread, setter_stack, K_THREAD_STACK_SIZEOF(setter_stack),
			event_setter_entry, (void *)(uintptr_t)EVT_DONE, NULL, NULL, 5, 0,
			K_NO_WAIT);

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

/* ----------------------------------------------------------------
 * #43: timeout-vs-post race stress (mirror of the k_sem test --
 * k_event shares the architecture, so the consume-the-in-flight-
 * notification branch of z_event_wait_internal needs the same edge
 * coverage). Invariants per outcome of a wait_safe vs a racing post:
 *
 *   got == EVT_GO -> the bits were consumed     (test() == 0)
 *   got == 0      -> the post landed after the timeout, with no
 *                    waiter to target           (test() == EVT_GO)
 *
 * and after draining, the reserved notification index must be clean.
 * The poster is a RAW FreeRTOS task pinned to the other core on
 * multicore targets (k_thread_create pins to core 0).
 * ---------------------------------------------------------------- */

#define EVT_STRESS_ITERS 100

/* The 18..22 ms sweep below only separates "reliably wake" from
 * "reliably miss" with 1 ms ticks (see the k_sem stress test). */
BUILD_ASSERT(CONFIG_FREERTOS_HZ >= 1000, "stress sweep requires 1 ms (or finer) ticks");

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define EVT_POSTER_CORE 1
#else
#define EVT_POSTER_CORE 0
#endif

static struct k_sem evt_stress_go;
static struct k_sem evt_stress_ack;

static void evt_stress_poster_task(void *arg)
{
	(void)arg;
	for (int i = 0; i < EVT_STRESS_ITERS; i++) {
		k_sem_take(&evt_stress_go, K_FOREVER);
		/* Sweep across the waiter's 20 ms timeout: 18/19 wake it,
		 * 20 lands on the timeout tick (the race), 21/22 post
		 * into an empty waiter list after the timeout. */
		k_msleep(18 + (i % 5));
		k_event_post(&mt_evt, EVT_GO);
		k_sem_give(&evt_stress_ack);
	}
	vTaskDelete(NULL);
}

static void test_event_timeout_vs_post_stress(void)
{
	k_event_init(&mt_evt);
	TEST_ASSERT_EQUAL(0, k_sem_init(&evt_stress_go, 0, 1));
	TEST_ASSERT_EQUAL(0, k_sem_init(&evt_stress_ack, 0, 1));

	TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(evt_stress_poster_task, "evt_stress",
							  4096, NULL, 6, NULL, EVT_POSTER_CORE));

	int timeouts = 0;

	for (int i = 0; i < EVT_STRESS_ITERS; i++) {
		k_sem_give(&evt_stress_go);

		uint32_t got = k_event_wait_safe(&mt_evt, EVT_GO, false, K_MSEC(20));

		/* The ack orders this iteration's post before the checks. */
		TEST_ASSERT_EQUAL(0, k_sem_take(&evt_stress_ack, K_SECONDS(2)));

		if (got != 0) {
			TEST_ASSERT_EQUAL(EVT_GO, got);
			TEST_ASSERT_EQUAL(0, k_event_test(&mt_evt, EVT_GO));
		} else {
			/* Timed out; the post latched into the word. */
			TEST_ASSERT_EQUAL(EVT_GO, k_event_test(&mt_evt, EVT_GO));
			TEST_ASSERT_EQUAL(EVT_GO, k_event_wait_safe(&mt_evt, EVT_GO, false,
								    K_NO_WAIT)); /* drain */
			timeouts++;
		}

		/* No stranded notification on the reserved index: an empty
		 * wait must miss both immediately and after blocking. */
		TEST_ASSERT_EQUAL(0, k_event_wait(&mt_evt, EVT_GO, false, K_NO_WAIT));
		TEST_ASSERT_EQUAL(0, k_event_wait(&mt_evt, EVT_GO, false, K_MSEC(2)));
	}

#if !CONFIG_IDF_TARGET_LINUX
	/* The sweep must produce both outcomes or the race was never
	 * exercised (18/19 ms reliably wake, 21/22 ms reliably miss).
	 * HW only: on the linux target a loaded CI host can stall the
	 * tick clock and collapse the whole sweep onto one outcome --
	 * the per-iteration invariants above hold regardless. */
	TEST_ASSERT_NOT_EQUAL(0, timeouts);
	TEST_ASSERT_NOT_EQUAL(EVT_STRESS_ITERS, timeouts);
#else
	(void)timeouts;
#endif

	k_msleep(20); /* let the self-deleting poster be reaped */
}

/* ----------------------------------------------------------------
 * #43: a single post wakes 3+ waiters (the chain-wake walk beyond
 * the two-waiter regression above).
 * ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(trio_stack0, 2048);
K_THREAD_STACK_DEFINE(trio_stack1, 2048);
K_THREAD_STACK_DEFINE(trio_stack2, 2048);
static struct k_thread trio_threads[3];
static volatile uint32_t trio_got[3];

static void trio_waiter_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	int idx = (int)(intptr_t)p1;

	trio_got[idx] = k_event_wait(&mt_evt, EVT_GO, false, K_SECONDS(2));
}

static void trio_spawn(int idx, k_thread_stack_t *stack, size_t stack_size)
{
	k_thread_create(&trio_threads[idx], stack, stack_size, trio_waiter_entry,
			(void *)(intptr_t)idx, NULL, NULL, 5, 0, K_NO_WAIT);
}

static void trio_park_all(void)
{
	trio_got[0] = 0xdead;
	trio_got[1] = 0xdead;
	trio_got[2] = 0xdead;
	trio_spawn(0, trio_stack0, K_THREAD_STACK_SIZEOF(trio_stack0));
	trio_spawn(1, trio_stack1, K_THREAD_STACK_SIZEOF(trio_stack1));
	trio_spawn(2, trio_stack2, K_THREAD_STACK_SIZEOF(trio_stack2));
	k_msleep(20);
}

static void trio_join_and_check(void)
{
	for (int i = 0; i < 3; i++) {
		TEST_ASSERT_EQUAL(0, k_thread_join(&trio_threads[i], K_SECONDS(2)));
		TEST_ASSERT_EQUAL(EVT_GO, trio_got[i]);
	}
}

static void test_event_post_wakes_three_waiters(void)
{
	k_event_init(&mt_evt);
	trio_park_all();

	k_event_post(&mt_evt, EVT_GO); /* ONE post must wake all three */

	trio_join_and_check();
}

/* ----------------------------------------------------------------
 * #43: FromISR post -- the multi-waiter accumulate-then-yield-once
 * path of z_event_post_internal in real ISR context (HW only; prior
 * art: the gated tests in test_k_timer.c).
 * ---------------------------------------------------------------- */

#ifdef CONFIG_K_TIMER_DISPATCH_ISR

static volatile bool evt_isr_was_isr;

static void IRAM_ATTR event_isr_post_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	evt_isr_was_isr = xPortInIsrContext();
	k_event_post(&mt_evt, EVT_GO);
}

static void test_event_isr_post_wakes_all(void)
{
	struct k_timer timer;

	k_event_init(&mt_evt);
	evt_isr_was_isr = false;
	trio_park_all();

	k_timer_init(&timer, event_isr_post_cb, NULL);
	k_timer_start(&timer, K_MSEC(10), K_NO_WAIT);

	trio_join_and_check();
	k_timer_stop(&timer);
	TEST_ASSERT_TRUE_MESSAGE(evt_isr_was_isr, "poster did not run in ISR context");
}

extern int _iram_text_start;
extern int _iram_text_end;

static void test_k_event_wait_iram_attr(void)
{
	/* The isr-ok-with-K_NO_WAIT contract (k_event_test rides this)
	 * requires IRAM residency -- see test_k_sem_take_iram_attr. */
	uintptr_t fn = (uintptr_t)k_event_wait;
	TEST_ASSERT_TRUE_MESSAGE(
		(fn >= (uintptr_t)&_iram_text_start && fn < (uintptr_t)&_iram_text_end),
		"k_event_wait is not in IRAM address range");
}

#endif /* CONFIG_K_TIMER_DISPATCH_ISR */

void test_k_event_mt_group(void)
{
	RUN_TEST(test_event_wait_from_thread);
	RUN_TEST(test_event_wait_all_from_thread);
	RUN_TEST(test_event_wait_safe_from_thread);
	RUN_TEST(test_event_post_wakes_all_with_safe_waiter);
	RUN_TEST(test_event_timeout_vs_post_stress);
	RUN_TEST(test_event_post_wakes_three_waiters);
#ifdef CONFIG_K_TIMER_DISPATCH_ISR
	RUN_TEST(test_event_isr_post_wakes_all);
	RUN_TEST(test_k_event_wait_iram_attr);
#endif
}
