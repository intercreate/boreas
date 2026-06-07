/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <errno.h>

#include "unity.h"
#include "zephyr/kernel.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_K_TIMER_DISPATCH_ISR
#include "esp_attr.h"
#include "esp_timer.h"
#endif

static void test_sem_init_and_count(void)
{
	struct k_sem sem;
	TEST_ASSERT_EQUAL(0, k_sem_init(&sem, 3, 5));
	TEST_ASSERT_EQUAL(3, k_sem_count_get(&sem));
}

static void test_sem_give_and_take(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 0, 1);
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&sem));

	k_sem_give(&sem);
	TEST_ASSERT_EQUAL(1, k_sem_count_get(&sem));

	TEST_ASSERT_EQUAL(0, k_sem_take(&sem, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&sem));
}

static void test_sem_take_timeout(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 0, 1);

	/* Should fail immediately with K_NO_WAIT */
	TEST_ASSERT_NOT_EQUAL(0, k_sem_take(&sem, K_NO_WAIT));
}

static void test_sem_reset(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 5, 10);
	TEST_ASSERT_EQUAL(5, k_sem_count_get(&sem));

	k_sem_reset(&sem);
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&sem));
}

static void test_sem_give_at_limit(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 0, 3);

	k_sem_give(&sem);
	k_sem_give(&sem);
	k_sem_give(&sem);
	TEST_ASSERT_EQUAL(3, k_sem_count_get(&sem));

	/* Give beyond limit -- FreeRTOS counting semaphore silently ignores */
	k_sem_give(&sem);
	TEST_ASSERT_EQUAL(3, k_sem_count_get(&sem));
}

static void test_sem_init_at_limit(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 5, 5);
	TEST_ASSERT_EQUAL(5, k_sem_count_get(&sem));

	/* Already at limit, give should not increase */
	k_sem_give(&sem);
	TEST_ASSERT_EQUAL(5, k_sem_count_get(&sem));
}

static void test_sem_take_no_wait_empty(void)
{
	struct k_sem sem;
	k_sem_init(&sem, 0, 1);

	/* K_NO_WAIT on empty should return -EBUSY */
	int ret = k_sem_take(&sem, K_NO_WAIT);
	TEST_ASSERT_EQUAL(-EBUSY, ret);
}

/* ----------------------------------------------------------------
 * K_SEM_DEFINE static-init test.
 *
 * K_SEM_DEFINE is a true compile-time initializer (upstream parity,
 * #26): the sem is usable from ANY constructor -- the old
 * constructor-emitting macro could not guarantee that on ESP-IDF
 * Xtensa (descending .init_array iteration). ctor_take_result proves
 * a constructor in this TU can consume the sem before main().
 * ---------------------------------------------------------------- */

K_SEM_DEFINE(auto_sem, 2, 5);

static int ctor_take_result = -1;

__attribute__((constructor)) static void sem_ctor_consumer(void)
{
	ctor_take_result = k_sem_take(&auto_sem, K_NO_WAIT);
	if (ctor_take_result == 0) {
		k_sem_give(&auto_sem); /* restore for the app_main test */
	}
}

static void test_sem_define_usable_in_constructor(void)
{
	TEST_ASSERT_EQUAL(0, ctor_take_result);
}

static void test_sem_init_invalid_args(void)
{
	struct k_sem sem;

	TEST_ASSERT_EQUAL(-EINVAL, k_sem_init(&sem, 0, 0));
	TEST_ASSERT_EQUAL(-EINVAL, k_sem_init(&sem, 6, 5));
}

static void test_sem_auto_init_pre_main(void)
{
	/* Initial count from the macro must be visible by app_main. */
	TEST_ASSERT_EQUAL(2, k_sem_count_get(&auto_sem));

	/* And the sem must be usable without an explicit k_sem_init(). */
	TEST_ASSERT_EQUAL(0, k_sem_take(&auto_sem, K_NO_WAIT));
	TEST_ASSERT_EQUAL(1, k_sem_count_get(&auto_sem));

	k_sem_give(&auto_sem);
	TEST_ASSERT_EQUAL(2, k_sem_count_get(&auto_sem));
}

/* ----------------------------------------------------------------
 * Stack-local sem + K_FOREVER + cross-priority give
 *
 * Regression harness for the corruption surfaced 2026-04-29 on
 * ESP32-S3 (root-caused to the pre-#18 k_thread zombie defect, issue
 * #21 -- see the April-shapes block below). Run EARLY so the rest of
 * the suite acts as the delayed-corruption detector: the original
 * bug panicked unrelated later tests, not the trigger itself.
 * ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(sem_giver_stack, 4096);
static struct k_thread sem_giver_thread;

static void sem_giver_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	k_msleep(30);
	k_sem_give((struct k_sem *)p1);
}

/* Each cycle uses a fresh stack frame: take on a STACK-LOCAL sem,
 * given from a thread at @p giver_prio, then let the frame die and
 * scribble over it via the next call. */
static void stack_sem_forever_cycle(int giver_prio)
{
	struct k_sem sem; /* stack-local: the trigger */

	TEST_ASSERT_EQUAL(0, k_sem_init(&sem, 0, 1));

	k_thread_create(&sem_giver_thread, sem_giver_stack, K_THREAD_STACK_SIZEOF(sem_giver_stack),
			sem_giver_entry, &sem, NULL, NULL, giver_prio, 0, K_NO_WAIT);

	TEST_ASSERT_EQUAL(0, k_sem_take(&sem, K_FOREVER));
	TEST_ASSERT_EQUAL(0, k_thread_join(&sem_giver_thread, K_SECONDS(2)));
}

/* Burn the just-freed frame region so a dangling kernel reference into
 * the dead sem shows up as corruption instead of silent luck. */
static void scribble_stack(void)
{
	volatile uint8_t burn[sizeof(struct k_sem) * 4];

	for (size_t i = 0; i < sizeof(burn); i++) {
		burn[i] = 0xA5;
	}
}

static void test_sem_stack_forever_same_prio(void)
{
	for (int i = 0; i < 5; i++) {
		stack_sem_forever_cycle(5); /* same prio as test task pool */
		scribble_stack();
	}
}

static void test_sem_stack_forever_high_prio(void)
{
	for (int i = 0; i < 5; i++) {
		stack_sem_forever_cycle(22); /* ESP_TIMER_TASK-class giver */
		scribble_stack();
	}
}

/* Timer-expiry giver -- the original PR 4 shape: expiry runs on
 * ESP_TIMER_TASK (HW) / the k_timer dispatcher (linux), both higher
 * priority than the test task. */
static void sem_give_expiry(struct k_timer *timer)
{
	k_sem_give((struct k_sem *)k_timer_user_data_get(timer));
}

static void test_sem_stack_forever_timer_giver(void)
{
	for (int i = 0; i < 5; i++) {
		struct k_sem sem; /* stack-local */
		struct k_timer timer;

		TEST_ASSERT_EQUAL(0, k_sem_init(&sem, 0, 1));
		k_timer_init(&timer, sem_give_expiry, NULL);
		k_timer_user_data_set(&timer, &sem);
		k_timer_start(&timer, K_MSEC(30), K_NO_WAIT);

		TEST_ASSERT_EQUAL(0, k_sem_take(&sem, K_FOREVER));
		k_timer_stop(&timer);
		scribble_stack();
	}
}

/* ----------------------------------------------------------------
 * Regression: the April 2026 corruption shapes.
 *
 * Stack-local timer+sem structs blocking on K_FOREVER takes, given
 * from the timer expiry context. Root cause was NOT k_sem: parked
 * tasks from the pre-#18 k_thread lifecycle left dangling
 * xStateListItem nodes in dead stack frames; frame reuse poisoned
 * them and later kernel list operations corrupted memory (proven by
 * injection on the linux target, issue #21). Fixed by #18. These
 * shapes reproduce the original trigger and must stay green under
 * either k_timer dispatch mode.
 * ---------------------------------------------------------------- */

/* PR-4's attempted k_timer shape: sem embedded next to the timer in
 * ONE stack-local struct. */
struct april_sync_timer {
	struct k_timer timer;
	struct k_sem sem;
};

static void april_sync_expiry(struct k_timer *timer)
{
	struct april_sync_timer *st = CONTAINER_OF(timer, struct april_sync_timer, timer);

	k_sem_give(&st->sem);
}

/* Extra frame to match the original call depth
 * (test -> k_timer_status_sync -> k_sem_take -> xQueueSemaphoreTake);
 * window-spill behavior on Xtensa depends on it. */
static __attribute__((noinline)) uint32_t april_fake_status_sync(struct april_sync_timer *st)
{
	TEST_ASSERT_EQUAL(0, k_sem_take(&st->sem, K_FOREVER));
	return 1;
}

/* April crash #1: one-shot 100 ms, block until first expiry. */
static void test_sem_april_oneshot_status_sync(void)
{
	struct april_sync_timer st; /* STACK -- the trigger */

	k_timer_init(&st.timer, april_sync_expiry, NULL);
	TEST_ASSERT_EQUAL(0, k_sem_init(&st.sem, 0, 10));

	k_timer_start(&st.timer, K_MSEC(100), K_NO_WAIT);
	TEST_ASSERT_EQUAL(1, april_fake_status_sync(&st));

	k_timer_stop(&st.timer);
	scribble_stack();
}

/* April crash #2 (already_fired shape): periodic 20 ms; first take is
 * the fast path (count accumulated); second take BLOCKS with the next
 * high-prio give landing milliseconds after the park -- the tightest
 * timing window. */
static void test_sem_april_periodic_status_sync(void)
{
	for (int i = 0; i < 5; i++) {
		struct april_sync_timer st; /* STACK */

		k_timer_init(&st.timer, april_sync_expiry, NULL);
		TEST_ASSERT_EQUAL(0, k_sem_init(&st.sem, 0, 10));

		k_timer_start(&st.timer, K_MSEC(20), K_MSEC(20));
		k_msleep(100); /* several expiries accumulate */

		TEST_ASSERT_EQUAL(1, april_fake_status_sync(&st)); /* fast path */
		TEST_ASSERT_EQUAL(1, april_fake_status_sync(&st)); /* blocks ~20ms */

		k_timer_stop(&st.timer);
		scribble_stack();
	}
}

/* ----------------------------------------------------------------
 * Notification-backed semantics: reset wakes waiters; priority wake
 * ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(sem_reset_stack, 4096);
static struct k_thread sem_resetter_thread;

static void sem_resetter_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	k_msleep(30);
	k_sem_reset((struct k_sem *)p1);
}

static void test_sem_reset_wakes_waiter_eagain(void)
{
	struct k_sem sem; /* stack-local: also exercises severance */

	TEST_ASSERT_EQUAL(0, k_sem_init(&sem, 0, 1));

	k_thread_create(&sem_resetter_thread, sem_reset_stack,
			K_THREAD_STACK_SIZEOF(sem_reset_stack), sem_resetter_entry, &sem, NULL,
			NULL, 5, 0, K_NO_WAIT);

	/* Blocked take must be woken by the reset with -EAGAIN
	 * (upstream parity; the FreeRTOS-backed sem could not do this). */
	TEST_ASSERT_EQUAL(-EAGAIN, k_sem_take(&sem, K_SECONDS(2)));
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&sem));
	TEST_ASSERT_EQUAL(0, k_thread_join(&sem_resetter_thread, K_SECONDS(2)));
}

K_THREAD_STACK_DEFINE(sem_lo_stack, 4096);
K_THREAD_STACK_DEFINE(sem_hi_stack, 4096);
static struct k_thread sem_lo_thread;
static struct k_thread sem_hi_thread;
static struct k_sem prio_sem;
/* 4 = low, 6 = high: k_thread_create takes FreeRTOS priorities (higher
 * number = higher priority), NOT upstream Zephyr's inverted scheme. */
static volatile int first_woken;

static void prio_waiter_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	if (k_sem_take(&prio_sem, K_SECONDS(2)) == 0 && first_woken == 0) {
		first_woken = (int)(intptr_t)p1;
	}
}

static void test_sem_give_wakes_highest_priority_waiter(void)
{
	TEST_ASSERT_EQUAL(0, k_sem_init(&prio_sem, 0, 2));
	first_woken = 0;

	/* Low-priority waiter enqueues FIRST -- FIFO would wake it. */
	k_thread_create(&sem_lo_thread, sem_lo_stack, K_THREAD_STACK_SIZEOF(sem_lo_stack),
			prio_waiter_entry, (void *)(intptr_t)4, NULL, NULL, 4, 0, K_NO_WAIT);
	k_msleep(20);
	k_thread_create(&sem_hi_thread, sem_hi_stack, K_THREAD_STACK_SIZEOF(sem_hi_stack),
			prio_waiter_entry, (void *)(intptr_t)6, NULL, NULL, 6, 0, K_NO_WAIT);
	k_msleep(20);

	k_sem_give(&prio_sem); /* upstream: highest-priority waiter wins */
	k_msleep(20);
	TEST_ASSERT_EQUAL(6, first_woken);

	k_sem_give(&prio_sem); /* release the second waiter */
	TEST_ASSERT_EQUAL(0, k_thread_join(&sem_lo_thread, K_SECONDS(2)));
	TEST_ASSERT_EQUAL(0, k_thread_join(&sem_hi_thread, K_SECONDS(2)));
}

/* ----------------------------------------------------------------
 * #43: timeout-vs-give race stress.
 *
 * The consume-the-in-flight-give branch of k_sem_take (woken with
 * got == 0) and the give-after-timeout latch only execute when a give
 * collides with the take's timeout edge -- no other test in the suite
 * ever times out a take while a give is in flight. Sweep the giver's
 * firing time across the taker's 20 ms timeout (early / same tick /
 * late) and check the conservation invariants on every outcome:
 *
 *   ret == 0       -> the unit was consumed   (count == 0)
 *   ret == -EAGAIN -> the give landed after the timeout (count == 1)
 *
 * and after draining, the reserved notification index must be clean:
 * an immediate take fails -EBUSY and a short blocking take times out
 * (a stranded notification would satisfy it instantly).
 *
 * The giver is a RAW FreeRTOS task: k_thread_create pins to core 0,
 * and on multicore targets the giver must run on the other core for
 * true concurrency in the race windows.
 * ---------------------------------------------------------------- */

#define STRESS_ITERS 100

/* The 18..22 ms sweep below only separates "reliably wake" from
 * "reliably latch" with 1 ms ticks; at e.g. 100 Hz the whole sweep
 * quantizes onto the timeout boundary. */
BUILD_ASSERT(CONFIG_FREERTOS_HZ >= 1000, "stress sweep requires 1 ms (or finer) ticks");

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
#define STRESS_GIVER_CORE 1
#else
#define STRESS_GIVER_CORE 0
#endif

static struct k_sem stress_sem;
static struct k_sem stress_go;
static struct k_sem stress_ack;

static void stress_giver_task(void *arg)
{
	(void)arg;
	for (int i = 0; i < STRESS_ITERS; i++) {
		k_sem_take(&stress_go, K_FOREVER);
		/* Sweep across the taker's 20 ms timeout: 18/19 wake the
		 * taker, 20 lands on the timeout tick (the race), 21/22
		 * latch into the count after the timeout. */
		k_msleep(18 + (i % 5));
		k_sem_give(&stress_sem);
		k_sem_give(&stress_ack);
	}
	vTaskDelete(NULL);
}

static void test_sem_timeout_vs_give_stress(void)
{
	TEST_ASSERT_EQUAL(0, k_sem_init(&stress_sem, 0, 1));
	TEST_ASSERT_EQUAL(0, k_sem_init(&stress_go, 0, 1));
	TEST_ASSERT_EQUAL(0, k_sem_init(&stress_ack, 0, 1));

	TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(stress_giver_task, "sem_stress", 4096,
							  NULL, 6, NULL, STRESS_GIVER_CORE));

	int timeouts = 0;

	for (int i = 0; i < STRESS_ITERS; i++) {
		k_sem_give(&stress_go);

		int ret = k_sem_take(&stress_sem, K_MSEC(20));

		/* The ack orders this iteration's give before the checks. */
		TEST_ASSERT_EQUAL(0, k_sem_take(&stress_ack, K_SECONDS(2)));

		if (ret == 0) {
			TEST_ASSERT_EQUAL(0, k_sem_count_get(&stress_sem));
		} else {
			TEST_ASSERT_EQUAL(-EAGAIN, ret);
			TEST_ASSERT_EQUAL(1, k_sem_count_get(&stress_sem));
			TEST_ASSERT_EQUAL(0, k_sem_take(&stress_sem, K_NO_WAIT)); /* drain */
			timeouts++;
		}

		/* No stranded notification on the reserved index: an empty
		 * take must fail both immediately and after blocking. */
		TEST_ASSERT_EQUAL(-EBUSY, k_sem_take(&stress_sem, K_NO_WAIT));
		TEST_ASSERT_EQUAL(-EAGAIN, k_sem_take(&stress_sem, K_MSEC(2)));
	}

#if !CONFIG_IDF_TARGET_LINUX
	/* The sweep must produce both outcomes or the race was never
	 * exercised (18/19 ms reliably wake, 21/22 ms reliably latch).
	 * HW only: on the linux target a loaded CI host can stall the
	 * tick clock and collapse the whole sweep onto one outcome --
	 * the per-iteration invariants above hold regardless. */
	TEST_ASSERT_NOT_EQUAL(0, timeouts);
	TEST_ASSERT_NOT_EQUAL(STRESS_ITERS, timeouts);
#else
	(void)timeouts;
#endif

	k_msleep(20); /* let the self-deleting giver be reaped */
}

/* ----------------------------------------------------------------
 * #43: give racing the park (give-before-block, not give-before-take).
 *
 * A zero-delay giver hammers the windows INSIDE k_sem_take: the
 * unlock-sample-relock recheck window and the enqueue ->
 * notification-wait window. On multicore the giver runs concurrently
 * on the other core; on unicore the higher-priority giver preempts at
 * the kernel calls inside those windows.
 * ---------------------------------------------------------------- */

#define PARK_RACE_ITERS 1000

static void park_race_giver_task(void *arg)
{
	(void)arg;
	for (int i = 0; i < PARK_RACE_ITERS; i++) {
		k_sem_take(&stress_go, K_FOREVER);
		k_sem_give(&stress_sem); /* zero delay */
	}
	vTaskDelete(NULL);
}

static void test_sem_give_racing_park(void)
{
	TEST_ASSERT_EQUAL(0, k_sem_init(&stress_sem, 0, 1));
	TEST_ASSERT_EQUAL(0, k_sem_init(&stress_go, 0, 1));

	TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(park_race_giver_task, "park_race", 4096,
							  NULL, 6, NULL, STRESS_GIVER_CORE));

	for (int i = 0; i < PARK_RACE_ITERS; i++) {
		k_sem_give(&stress_go);
		TEST_ASSERT_EQUAL(0, k_sem_take(&stress_sem, K_MSEC(100)));
	}

	/* Units conserved: every give was consumed by exactly one take. */
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&stress_sem));
	TEST_ASSERT_EQUAL(-EBUSY, k_sem_take(&stress_sem, K_NO_WAIT));

	k_msleep(20); /* let the self-deleting giver be reaped */
}

/* ----------------------------------------------------------------
 * #43: multi-waiter beyond two -- conservation, FIFO order among
 * equal priorities, and reset waking 3+.
 * ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(mw_stack0, 4096);
K_THREAD_STACK_DEFINE(mw_stack1, 4096);
K_THREAD_STACK_DEFINE(mw_stack2, 4096);
K_THREAD_STACK_DEFINE(mw_stack3, 4096);
static struct k_thread mw_threads[4];
static struct k_sem mw_sem;
static volatile int mw_result[4];
static volatile int mw_order[4];
static atomic_t mw_next;

static void mw_waiter_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	int idx = (int)(intptr_t)p1;

	mw_result[idx] = k_sem_take(&mw_sem, K_SECONDS(2));
	if (mw_result[idx] == 0) {
		mw_order[atomic_add(&mw_next, 1)] = idx;
	}
}

static void mw_reset_state(void)
{
	/* Sentinels: a stale value from a previous test must not be able
	 * to satisfy this test's assertions (e.g. a lost wake leaving a
	 * prior run's 0 in mw_result). */
	for (int i = 0; i < 4; i++) {
		mw_result[i] = 0xbad;
		mw_order[i] = -1;
	}
	atomic_set(&mw_next, 0);
}

static void mw_spawn(int idx, k_thread_stack_t *stack, size_t stack_size)
{
	k_thread_create(&mw_threads[idx], stack, stack_size, mw_waiter_entry, (void *)(intptr_t)idx,
			NULL, NULL, 5, 0, K_NO_WAIT);
}

static void test_sem_multi_waiter_conservation(void)
{
	TEST_ASSERT_EQUAL(0, k_sem_init(&mw_sem, 0, 4));
	mw_reset_state();

	/* 4 waiters park... */
	mw_spawn(0, mw_stack0, K_THREAD_STACK_SIZEOF(mw_stack0));
	mw_spawn(1, mw_stack1, K_THREAD_STACK_SIZEOF(mw_stack1));
	mw_spawn(2, mw_stack2, K_THREAD_STACK_SIZEOF(mw_stack2));
	mw_spawn(3, mw_stack3, K_THREAD_STACK_SIZEOF(mw_stack3));
	k_msleep(20);

	/* ...4 gives wake each exactly once, units conserved. */
	for (int i = 0; i < 4; i++) {
		k_sem_give(&mw_sem);
	}
	for (int i = 0; i < 4; i++) {
		TEST_ASSERT_EQUAL(0, k_thread_join(&mw_threads[i], K_SECONDS(2)));
		TEST_ASSERT_EQUAL(0, mw_result[i]);
	}
	TEST_ASSERT_EQUAL(4, atomic_get(&mw_next));
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&mw_sem));
	TEST_ASSERT_EQUAL(-EBUSY, k_sem_take(&mw_sem, K_NO_WAIT));
}

static void test_sem_equal_priority_waiters_fifo(void)
{
	TEST_ASSERT_EQUAL(0, k_sem_init(&mw_sem, 0, 4));
	mw_reset_state();

	/* Enqueue 3 equal-priority waiters in a known order (the sleeps
	 * guarantee each is parked before the next spawns). */
	mw_spawn(0, mw_stack0, K_THREAD_STACK_SIZEOF(mw_stack0));
	k_msleep(10);
	mw_spawn(1, mw_stack1, K_THREAD_STACK_SIZEOF(mw_stack1));
	k_msleep(10);
	mw_spawn(2, mw_stack2, K_THREAD_STACK_SIZEOF(mw_stack2));
	k_msleep(10);

	/* Sequential gives must wake them FIFO (z_sem_pop_waiter's
	 * strict '>' comparison is what preserves enqueue order among
	 * equal priorities -- this test pins that). */
	for (int i = 0; i < 3; i++) {
		k_sem_give(&mw_sem);
		k_msleep(10);
	}
	for (int i = 0; i < 3; i++) {
		TEST_ASSERT_EQUAL(0, k_thread_join(&mw_threads[i], K_SECONDS(2)));
	}
	TEST_ASSERT_EQUAL(0, mw_order[0]);
	TEST_ASSERT_EQUAL(1, mw_order[1]);
	TEST_ASSERT_EQUAL(2, mw_order[2]);
}

static void test_sem_reset_wakes_all_waiters(void)
{
	TEST_ASSERT_EQUAL(0, k_sem_init(&mw_sem, 0, 4));
	mw_reset_state();

	mw_spawn(0, mw_stack0, K_THREAD_STACK_SIZEOF(mw_stack0));
	mw_spawn(1, mw_stack1, K_THREAD_STACK_SIZEOF(mw_stack1));
	mw_spawn(2, mw_stack2, K_THREAD_STACK_SIZEOF(mw_stack2));
	k_msleep(20);

	/* One reset wakes ALL waiters with -EAGAIN (the wake-all loop in
	 * k_sem_reset is otherwise only tested with a single waiter). */
	k_sem_reset(&mw_sem);

	for (int i = 0; i < 3; i++) {
		TEST_ASSERT_EQUAL(0, k_thread_join(&mw_threads[i], K_SECONDS(2)));
		TEST_ASSERT_EQUAL(-EAGAIN, mw_result[i]);
	}
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&mw_sem));

	/* The sem stays functional after the reset. */
	k_sem_give(&mw_sem);
	TEST_ASSERT_EQUAL(0, k_sem_take(&mw_sem, K_NO_WAIT));
}

/* ----------------------------------------------------------------
 * #43: FromISR give -- real ISR context via k_timer ISR dispatch
 * (HW only; prior art: the gated tests in test_k_timer.c).
 * ---------------------------------------------------------------- */

#ifdef CONFIG_K_TIMER_DISPATCH_ISR

static struct k_sem isr_give_sem;
static volatile int64_t isr_give_us;
static volatile bool isr_give_was_isr;

static void IRAM_ATTR sem_isr_give_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	isr_give_was_isr = xPortInIsrContext();
	isr_give_us = esp_timer_get_time();
	k_sem_give(&isr_give_sem);
}

static void test_sem_isr_give_prompt_wake(void)
{
	struct k_timer timer;

	TEST_ASSERT_EQUAL(0, k_sem_init(&isr_give_sem, 0, 1));
	isr_give_us = 0;
	isr_give_was_isr = false;

	k_timer_init(&timer, sem_isr_give_cb, NULL);
	/* Early-in-tick expiry (10.1 ms at 1000 Hz): if the FromISR give
	 * failed to request the context switch (portYIELD_FROM_ISR), the
	 * wake would be deferred to the next tick interrupt ~900 us
	 * later, failing the bound below; a genuine yield wakes in tens
	 * of microseconds. (The causal model assumes giver ISR and
	 * waiter share a core: the esp_timer ISR and the main task are
	 * both CPU0 by default.) */
	k_timer_start(&timer, K_USEC(10100), K_NO_WAIT);

	TEST_ASSERT_EQUAL(0, k_sem_take(&isr_give_sem, K_FOREVER));
	int64_t latency_us = esp_timer_get_time() - isr_give_us;

	k_timer_stop(&timer);
	TEST_ASSERT_TRUE_MESSAGE(isr_give_was_isr, "giver did not run in ISR context");
	TEST_ASSERT_TRUE_MESSAGE(latency_us < 700, "ISR give did not wake the waiter promptly");
}

static struct k_sem isr_take_sem;
static volatile int isr_take_ret1;
static volatile int isr_take_ret2;

static void IRAM_ATTR sem_isr_take_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	/* Upstream contract: k_sem_take is isr-ok with K_NO_WAIT (the
	 * K_NO_WAIT paths make no FreeRTOS calls). */
	isr_take_ret1 = k_sem_take(&isr_take_sem, K_NO_WAIT);
	isr_take_ret2 = k_sem_take(&isr_take_sem, K_NO_WAIT);
}

static void test_sem_isr_take_no_wait(void)
{
	struct k_timer timer;

	TEST_ASSERT_EQUAL(0, k_sem_init(&isr_take_sem, 1, 1));
	isr_take_ret1 = 0xbad;
	isr_take_ret2 = 0xbad;

	k_timer_init(&timer, sem_isr_take_cb, NULL);
	k_timer_start(&timer, K_MSEC(10), K_NO_WAIT);
	k_msleep(50);
	k_timer_stop(&timer);

	TEST_ASSERT_EQUAL(0, isr_take_ret1);
	TEST_ASSERT_EQUAL(-EBUSY, isr_take_ret2);
	TEST_ASSERT_EQUAL(0, k_sem_count_get(&isr_take_sem));
}

extern int _iram_text_start;
extern int _iram_text_end;

static void test_k_sem_take_iram_attr(void)
{
	/* The isr-ok-with-K_NO_WAIT contract requires IRAM residency:
	 * the esp_timer ISR is allocated ESP_INTR_FLAG_IRAM, so flash-
	 * resident code would fault if the ISR fired during a flash
	 * operation (the suite cannot provoke that; pin it statically). */
	uintptr_t fn = (uintptr_t)k_sem_take;
	TEST_ASSERT_TRUE_MESSAGE(
		(fn >= (uintptr_t)&_iram_text_start && fn < (uintptr_t)&_iram_text_end),
		"k_sem_take is not in IRAM address range");
}

#endif /* CONFIG_K_TIMER_DISPATCH_ISR */

void test_k_sem_group(void)
{
	RUN_TEST(test_sem_stack_forever_same_prio);
	RUN_TEST(test_sem_stack_forever_high_prio);
	RUN_TEST(test_sem_stack_forever_timer_giver);
	RUN_TEST(test_sem_april_oneshot_status_sync);
	RUN_TEST(test_sem_april_periodic_status_sync);
	RUN_TEST(test_sem_init_and_count);
	RUN_TEST(test_sem_give_and_take);
	RUN_TEST(test_sem_take_timeout);
	RUN_TEST(test_sem_reset);
	RUN_TEST(test_sem_give_at_limit);
	RUN_TEST(test_sem_init_at_limit);
	RUN_TEST(test_sem_take_no_wait_empty);
	RUN_TEST(test_sem_auto_init_pre_main);
	RUN_TEST(test_sem_define_usable_in_constructor);
	RUN_TEST(test_sem_init_invalid_args);
	RUN_TEST(test_sem_reset_wakes_waiter_eagain);
	RUN_TEST(test_sem_give_wakes_highest_priority_waiter);
	RUN_TEST(test_sem_timeout_vs_give_stress);
	RUN_TEST(test_sem_give_racing_park);
	RUN_TEST(test_sem_multi_waiter_conservation);
	RUN_TEST(test_sem_equal_priority_waiters_fifo);
	RUN_TEST(test_sem_reset_wakes_all_waiters);
#ifdef CONFIG_K_TIMER_DISPATCH_ISR
	RUN_TEST(test_sem_isr_give_prompt_wake);
	RUN_TEST(test_sem_isr_take_no_wait);
	RUN_TEST(test_k_sem_take_iram_attr);
#endif
}
