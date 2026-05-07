/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <stdint.h>

#include "unity.h"
#include "zephyr/kernel.h"
#include "sdkconfig.h"

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static volatile int timer_count;
static volatile int stop_called;

static void IRAM_ATTR test_timer_cb(struct k_timer *timer)
{
	timer_count++;
}

static void test_stop_cb(struct k_timer *timer)
{
	stop_called++;
}

static void test_timer_one_shot(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	k_timer_start(&timer, K_MSEC(50), K_NO_WAIT);
	k_msleep(200);
	k_timer_stop(&timer);

	TEST_ASSERT_GREATER_OR_EQUAL(1, timer_count);
}

static void test_timer_periodic(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	k_timer_start(&timer, K_MSEC(50), K_MSEC(50));
	k_msleep(300);
	k_timer_stop(&timer);

	TEST_ASSERT_GREATER_OR_EQUAL(3, timer_count);
}

static void test_timer_stop_callback(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, test_stop_cb);
	stop_called = 0;

	k_timer_start(&timer, K_MSEC(50), K_MSEC(50));
	k_msleep(100);
	k_timer_stop(&timer);

	TEST_ASSERT_EQUAL(1, stop_called);
}

static void test_timer_status_get(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	k_timer_start(&timer, K_MSEC(30), K_MSEC(30));
	k_msleep(200);

	uint32_t status = k_timer_status_get(&timer);
	TEST_ASSERT_GREATER_OR_EQUAL(3, status);

	/* Second call should return 0 or small number (resets on read) */
	k_msleep(10);
	uint32_t status2 = k_timer_status_get(&timer);
	TEST_ASSERT_LESS_OR_EQUAL(2, status2);

	k_timer_stop(&timer);
}

static void test_timer_remaining_get(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);

	k_timer_start(&timer, K_MSEC(500), K_NO_WAIT);
	k_msleep(50);

	uint32_t remaining = k_timer_remaining_get(&timer);
	/* Should have ~450ms remaining (allow wide margin) */
	TEST_ASSERT_GREATER_THAN(100, remaining);
	TEST_ASSERT_LESS_OR_EQUAL(500, remaining);

	k_timer_stop(&timer);

	/* After stop, remaining should be 0 */
	remaining = k_timer_remaining_get(&timer);
	TEST_ASSERT_EQUAL(0, remaining);
}

static void test_timer_status_sync_blocking(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	/* status_sync should block until the next expiry, NOT busy-poll. */
	uint32_t t0 = (uint32_t)k_uptime_get();
	k_timer_start(&timer, K_MSEC(100), K_NO_WAIT);
	uint32_t status = k_timer_status_sync(&timer);
	uint32_t elapsed = (uint32_t)k_uptime_get() - t0;

	/* Got the single expiry. */
	TEST_ASSERT_EQUAL(1, status);
	/* Blocked for at least ~the timeout (allow <= drift below). */
	TEST_ASSERT_GREATER_OR_EQUAL(95, elapsed);
	TEST_ASSERT_LESS_THAN(200, elapsed);

	k_timer_stop(&timer);
}

static void test_timer_status_sync_already_fired(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);

	/* Periodic so expiries accumulate before status_sync is called. */
	k_timer_start(&timer, K_MSEC(20), K_MSEC(20));
	k_msleep(100); /* let several expiries land */

	/* status_sync must return immediately when count is already > 0,
	 * and atomically reset the count to zero. */
	uint32_t status = k_timer_status_sync(&timer);
	TEST_ASSERT_GREATER_OR_EQUAL(3, status);

	/* Count was just reset; a back-to-back call goes through the
	 * blocking path and waits for the next expiry. */
	uint32_t status2 = k_timer_status_sync(&timer);
	TEST_ASSERT_GREATER_OR_EQUAL(1, status2);

	k_timer_stop(&timer);
}

/* Regression: status_sync must wake when the timer is stopped, not just
 * on expiry. Otherwise blocking on a one-shot that gets cancelled would
 * hang. Mirrors upstream Zephyr semantics. */
struct stop_waker_ctx {
	struct k_timer *timer;
};

static void stop_waker_thread(void *arg, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	struct stop_waker_ctx *ctx = arg;
	k_msleep(50);
	k_timer_stop(ctx->timer);
}

K_THREAD_STACK_DEFINE(stop_waker_stack, 2048);

static void test_timer_status_sync_wakes_on_stop(void)
{
	struct k_timer timer;
	struct k_thread waker_thread;
	struct stop_waker_ctx ctx;

	k_timer_init(&timer, test_timer_cb, NULL);
	k_timer_start(&timer, K_SECONDS(60), K_NO_WAIT); /* very long */
	ctx.timer = &timer;

	k_thread_create(&waker_thread, stop_waker_stack, K_THREAD_STACK_SIZEOF(stop_waker_stack),
			stop_waker_thread, &ctx, NULL, NULL, 5, 0, K_NO_WAIT);

	uint32_t t0 = (uint32_t)k_uptime_get();
	uint32_t status = k_timer_status_sync(&timer); /* should wake on stop */
	uint32_t elapsed = (uint32_t)k_uptime_get() - t0;

	TEST_ASSERT_EQUAL(0, status); /* never expired */
	TEST_ASSERT_GREATER_OR_EQUAL(40, elapsed);
	TEST_ASSERT_LESS_THAN(200, elapsed);

	k_thread_join(&waker_thread, K_FOREVER);
}

static void test_timer_remaining_ticks(void)
{
	struct k_timer timer;
	k_timer_init(&timer, NULL, NULL);

	k_timer_start(&timer, K_MSEC(500), K_NO_WAIT);
	k_msleep(50);

	k_ticks_t remaining = k_timer_remaining_ticks(&timer);
	/* Roughly 450ms remaining; bounds derived from K_MSEC are tick-rate-agnostic. */
	k_ticks_t expected_min = k_timeout_to_ticks(K_MSEC(100));
	k_ticks_t expected_max = k_timeout_to_ticks(K_MSEC(500));
	TEST_ASSERT_GREATER_THAN(expected_min, remaining);
	TEST_ASSERT_LESS_OR_EQUAL(expected_max, remaining);

	k_timer_stop(&timer);

	/* After stop, remaining_ticks must be zero (matches upstream). */
	TEST_ASSERT_EQUAL(0, k_timer_remaining_ticks(&timer));
}

static void test_timer_expires_ticks(void)
{
	struct k_timer timer;
	k_timer_init(&timer, NULL, NULL);

	k_ticks_t now_before = k_uptime_ticks();
	k_timer_start(&timer, K_MSEC(500), K_NO_WAIT);
	k_ticks_t expires = k_timer_expires_ticks(&timer);

	/* Expiry tick is in the future. */
	TEST_ASSERT_GREATER_THAN(now_before, expires);

	k_ticks_t remaining = k_timer_remaining_ticks(&timer);
	k_ticks_t now_after = k_uptime_ticks();
	/* expires - now ~ remaining. Allow a few-tick slop for jitter. */
	k_ticks_t computed = expires - now_after;
	k_ticks_t delta = (computed > remaining) ? (computed - remaining) : (remaining - computed);
	TEST_ASSERT_LESS_OR_EQUAL(5, delta);

	k_timer_stop(&timer);

	/* Upstream surprising bit: when stopped, expires_ticks returns
	 * CURRENT uptime, not zero. */
	k_ticks_t expires_stopped = k_timer_expires_ticks(&timer);
	k_ticks_t uptime_now = k_uptime_ticks();
	k_ticks_t stopped_delta = (expires_stopped > uptime_now) ? (expires_stopped - uptime_now)
								 : (uptime_now - expires_stopped);
	TEST_ASSERT_LESS_OR_EQUAL(5, stopped_delta);
}

/* Regression: after a one-shot expires, the timer must report itself
 * as not-running. Previously running stayed true and status_sync would
 * hang on a re-call after the expiry was already consumed. */
static void test_timer_one_shot_clears_running(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	k_timer_start(&timer, K_MSEC(50), K_NO_WAIT);
	k_msleep(150);

	/* First sync drains the one expiry. */
	TEST_ASSERT_EQUAL(1, k_timer_status_sync(&timer));

	/* Second sync must return immediately with 0 (timer no longer
	 * running), not block indefinitely. Capture elapsed to prove
	 * non-blocking. */
	uint32_t t0 = (uint32_t)k_uptime_get();
	uint32_t status = k_timer_status_sync(&timer);
	uint32_t elapsed = (uint32_t)k_uptime_get() - t0;
	TEST_ASSERT_EQUAL(0, status);
	TEST_ASSERT_LESS_THAN(20, elapsed);

	/* remaining_get must also report 0 once the one-shot is done. */
	TEST_ASSERT_EQUAL(0, k_timer_remaining_get(&timer));
}

static void test_timer_user_data(void)
{
	struct k_timer timer;
	k_timer_init(&timer, NULL, NULL);

	int ctx = 42;
	k_timer_user_data_set(&timer, &ctx);
	TEST_ASSERT_EQUAL_PTR(&ctx, k_timer_user_data_get(&timer));
}

static void test_timer_first_interval(void)
{
	struct k_timer timer;
	k_timer_init(&timer, test_timer_cb, NULL);
	timer_count = 0;

	/* First expiry at 100ms, then repeat every 50ms */
	k_timer_start(&timer, K_MSEC(100), K_MSEC(50));

	/* At 80ms: should not have fired yet */
	k_msleep(80);
	TEST_ASSERT_EQUAL(0, timer_count);

	/* At 130ms: first expiry should have fired (~100ms) */
	k_msleep(50);
	TEST_ASSERT_EQUAL(1, timer_count);

	/* At 330ms: periodic should have added ~4 more (at ~150, 200, 250, 300ms) */
	k_msleep(200);
	TEST_ASSERT_GREATER_OR_EQUAL(4, timer_count);

	k_timer_stop(&timer);
}

/* ----------------------------------------------------------------
 * ISR dispatch tests (CONFIG_K_TIMER_DISPATCH_ISR)
 * ---------------------------------------------------------------- */

#ifdef CONFIG_K_TIMER_DISPATCH_ISR

static volatile bool isr_context_result;

static void IRAM_ATTR isr_context_check_cb(struct k_timer *timer)
{
	isr_context_result = xPortInIsrContext();
}

static void test_timer_callback_in_isr_context(void)
{
	struct k_timer timer;
	k_timer_init(&timer, isr_context_check_cb, NULL);
	isr_context_result = false;

	k_timer_start(&timer, K_MSEC(10), K_NO_WAIT);
	k_msleep(100);
	k_timer_stop(&timer);

	TEST_ASSERT_TRUE_MESSAGE(isr_context_result, "Expiry callback did not run in ISR context");
}

extern int _iram_text_start;
extern int _iram_text_end;

static void test_timer_callback_iram_attr(void)
{
	uintptr_t fn = (uintptr_t)isr_context_check_cb;
	TEST_ASSERT_TRUE_MESSAGE(
		(fn >= (uintptr_t)&_iram_text_start && fn < (uintptr_t)&_iram_text_end),
		"IRAM_ATTR callback is not in IRAM address range");
}

static struct k_sem isr_test_sem;
static struct k_work isr_test_work;
static volatile bool isr_work_ran;

static void IRAM_ATTR isr_safe_apis_cb(struct k_timer *timer)
{
	k_sem_give(&isr_test_sem);
	k_work_submit(&isr_test_work);
}

static void isr_test_work_handler(struct k_work *work)
{
	isr_work_ran = true;
}

static void test_timer_isr_safe_apis(void)
{
	struct k_timer timer;

	k_sem_init(&isr_test_sem, 0, 1);
	k_work_init(&isr_test_work, isr_test_work_handler);
	isr_work_ran = false;

	k_timer_init(&timer, isr_safe_apis_cb, NULL);
	k_timer_start(&timer, K_MSEC(10), K_NO_WAIT);

	/* k_sem_take blocks until the ISR callback gives the sem. */
	int ret = k_sem_take(&isr_test_sem, K_MSEC(500));
	TEST_ASSERT_EQUAL_MESSAGE(0, ret, "k_sem_give from ISR did not wake waiter");

	/* Give the work queue a moment to drain. */
	k_msleep(50);
	TEST_ASSERT_TRUE_MESSAGE(isr_work_ran, "k_work_submit from ISR did not run");

	k_timer_stop(&timer);
}

static void test_k_work_submit_iram_attr(void)
{
	uintptr_t fn = (uintptr_t)k_work_submit;
	TEST_ASSERT_TRUE_MESSAGE(
		(fn >= (uintptr_t)&_iram_text_start && fn < (uintptr_t)&_iram_text_end),
		"k_work_submit is not in IRAM address range");
}

static void test_k_sem_give_iram_attr(void)
{
	uintptr_t fn = (uintptr_t)k_sem_give;
	TEST_ASSERT_TRUE_MESSAGE(
		(fn >= (uintptr_t)&_iram_text_start && fn < (uintptr_t)&_iram_text_end),
		"k_sem_give is not in IRAM address range");
}

#endif /* CONFIG_K_TIMER_DISPATCH_ISR */

void test_k_timer_group(void)
{
	RUN_TEST(test_timer_one_shot);
	RUN_TEST(test_timer_periodic);
	RUN_TEST(test_timer_stop_callback);
	RUN_TEST(test_timer_status_get);
	RUN_TEST(test_timer_remaining_get);
	RUN_TEST(test_timer_user_data);
	RUN_TEST(test_timer_first_interval);
	RUN_TEST(test_timer_status_sync_blocking);
	RUN_TEST(test_timer_status_sync_already_fired);
	RUN_TEST(test_timer_status_sync_wakes_on_stop);
	RUN_TEST(test_timer_remaining_ticks);
	RUN_TEST(test_timer_expires_ticks);
	RUN_TEST(test_timer_one_shot_clears_running);
#ifdef CONFIG_K_TIMER_DISPATCH_ISR
	RUN_TEST(test_timer_callback_in_isr_context);
	RUN_TEST(test_timer_callback_iram_attr);
	RUN_TEST(test_timer_isr_safe_apis);
	RUN_TEST(test_k_work_submit_iram_attr);
	RUN_TEST(test_k_sem_give_iram_attr);
#endif
}
