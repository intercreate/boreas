/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <errno.h>

#include "unity.h"
#include "zephyr/kernel.h"

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
 * K_SEM_DEFINE auto-init regression test.
 *
 * K_SEM_DEFINE must produce a fully-initialized semaphore by the
 * time app_main() runs. We verify the post-startup count matches
 * the macro's `initial` argument and that take/give work without
 * an explicit k_sem_init() call.
 *
 * Note: ESP-IDF Xtensa iterates .init_array in descending order, so
 * we cannot reliably consume the sem from another constructor in
 * the same TU (see K_SEM_DEFINE doxygen). The "ready by app_main"
 * contract is what's actually deliverable; that's what we test.
 * ---------------------------------------------------------------- */

K_SEM_DEFINE(auto_sem, 2, 5);

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
}
