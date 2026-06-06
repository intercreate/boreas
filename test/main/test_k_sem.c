/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <errno.h>
#include <string.h>

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
 * Repro harness for the latent corruption surfaced 2026-04-29 on
 * ESP32-S3 (stack-local sem + K_FOREVER take + higher-priority giver
 * corrupted the waiter's frame across the yield; static sems and
 * same-priority givers were immune). Run EARLY so the rest of the
 * suite acts as the delayed-corruption detector -- the original bug
 * panicked unrelated later tests, not the trigger itself.
 * ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(sem_giver_stack, 4096);
static struct k_thread sem_giver_thread;
static struct k_sem *volatile giver_target;

static void sem_giver_entry(void *p1, void *p2, void *p3)
{
	(void)p1;
	(void)p2;
	(void)p3;
	k_msleep(30);
	k_sem_give(giver_target);
}

/* Each cycle uses a fresh stack frame: take on a STACK-LOCAL sem,
 * given from a thread at @p giver_prio, then let the frame die and
 * scribble over it via the next call. */
static void stack_sem_forever_cycle(int giver_prio)
{
	struct k_sem sem; /* stack-local: the trigger */

	TEST_ASSERT_EQUAL(0, k_sem_init(&sem, 0, 1));
	giver_target = &sem;

	k_thread_create(&sem_giver_thread, sem_giver_stack, K_THREAD_STACK_SIZEOF(sem_giver_stack),
			sem_giver_entry, NULL, NULL, NULL, giver_prio, 0, K_NO_WAIT);

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
 * EXPERIMENT A: faithful reconstruction of the April 2026 crash
 * shapes (devlog/2026-04-29_k_sem_stack_local_kforever_corruption.md).
 * Requires CONFIG_K_TIMER_DISPATCH_ISR=n so expiry gives from
 * ESP_TIMER_TASK in task context (prio ~22) -- the original giver.
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

#if CONFIG_IDF_TARGET_LINUX
/* ----------------------------------------------------------------
 * EXPERIMENT B: zombie-injection mechanism proof.
 *
 * Hypothesis (#21): the April 2026 corruption was the pre-#18
 * k_thread defect -- a parked task whose TCB (and thus its
 * xStateListItem) lived in a DEAD stack frame. Any later insert into
 * the same kernel list (e.g. a K_FOREVER sem take parking the caller
 * on xSuspendedTaskList) makes the kernel WRITE through the dangling
 * node into memory it doesn't own.
 *
 * This test recreates that zombie deliberately with raw FreeRTOS
 * calls, snapshots the dead-frame TCB bytes (never writing them
 * ourselves -- frame depths are controlled so no live local overlaps
 * the region), parks a stack-sem waiter, and memcmp's. A delta is
 * PROOF of the mechanism. A zero delta is INCONCLUSIVE (node
 * adjacency in the shared suspended list isn't guaranteed), not a
 * refutation.
 * ---------------------------------------------------------------- */

static TaskHandle_t zombie_handle;
static uint8_t *volatile zombie_region;

static void zombie_entry(void *arg)
{
	(void)arg;
	vTaskSuspend(NULL); /* park forever: the pre-#18 zombie shape */
}

static StackType_t zombie_stack[2048 / sizeof(StackType_t)];

static __attribute__((noinline)) void make_zombie(void)
{
	StaticTask_t tcb; /* FUNCTION-LOCAL: becomes the dangling node */

	zombie_handle = xTaskCreateStaticPinnedToCore(zombie_entry, "zombie",
						      sizeof(zombie_stack) / sizeof(StackType_t),
						      NULL, 5, zombie_stack, &tcb, 0);
	TEST_ASSERT_NOT_NULL(zombie_handle);
	zombie_region = (uint8_t *)&tcb;
	k_msleep(20); /* let it reach the park */
	/* Return: tcb now dangles in this dead frame. Deliberate. */
}

/* Push the zombie's frame deeper than any later live frame so nothing
 * in this test overwrites the region before we inspect it. The pad
 * must exceed the deepest subsequent call chain -- on the POSIX port
 * the waiter's blocking path (k_sem_take -> xQueueSemaphoreTake ->
 * vPortYield -> event_wait -> pthread internals) runs on the same
 * pthread stack, so be generous. A 512 B pad demonstrably was NOT
 * enough: the waiter chain trampled the zombie TCB and vTaskDelete
 * wedged in uxListRemove (which is itself the April mechanism, via
 * frame reuse -- but the surgical kernel-write proof needs the region
 * untouched by us). */
static void *volatile pad_escape; /* defeats array shrinking/elision */

static __attribute__((noinline)) void call_deep(void (*fn)(void))
{
	uint8_t pad[8192];

	pad_escape = pad; /* address escapes: array must exist */
	memset(pad, 0x5A, sizeof(pad));
	__asm__ volatile("" ::"r"(pad) : "memory");
	fn();
	__asm__ volatile("" ::"r"(pad) : "memory"); /* alive across the call */
}

static void test_sem_zombie_injection_mechanism(void)
{
	static uint8_t snapshot[sizeof(StaticTask_t)];

	call_deep(make_zombie);
	memcpy(snapshot, zombie_region, sizeof(snapshot));

	/* Park a stack-sem waiter: the suspended-list insert links our
	 * TCB adjacent to the dangling zombie node, writing into it. */
	stack_sem_forever_cycle(22);

	int delta = memcmp(snapshot, zombie_region, sizeof(snapshot));

	/* Clean the list BEFORE asserting: delete-other reclaims
	 * synchronously on silicon and unlinks the dangling node. On the
	 * POSIX port teardown is idle-deferred and dereferences the TCB
	 * in this (still live) frame -- give idle its reap window before
	 * returning, mirroring k_thread_abort. */
	vTaskDelete(zombie_handle);
	k_msleep(2 * portTICK_PERIOD_MS);

	if (delta == 0) {
		TEST_MESSAGE("INCONCLUSIVE: no kernel write landed in the "
			     "zombie region this run (list adjacency not "
			     "guaranteed)");
	} else {
		TEST_MESSAGE("MECHANISM PROVEN: kernel wrote through the "
			     "dangling suspended-list node into dead-frame "
			     "memory");
	}
	TEST_PASS(); /* outcome is reported, not asserted -- see above */
}
#endif /* CONFIG_IDF_TARGET_LINUX -- the 8K pad would overflow the S3                              \
	* main task stack; the mechanism is proven on linux. */

void test_k_sem_group(void)
{
	RUN_TEST(test_sem_stack_forever_same_prio);
	RUN_TEST(test_sem_stack_forever_high_prio);
	RUN_TEST(test_sem_stack_forever_timer_giver);
	RUN_TEST(test_sem_april_oneshot_status_sync);
	RUN_TEST(test_sem_april_periodic_status_sync);
#if CONFIG_IDF_TARGET_LINUX
	RUN_TEST(test_sem_zombie_injection_mechanism);
#endif
	RUN_TEST(test_sem_init_and_count);
	RUN_TEST(test_sem_give_and_take);
	RUN_TEST(test_sem_take_timeout);
	RUN_TEST(test_sem_reset);
	RUN_TEST(test_sem_give_at_limit);
	RUN_TEST(test_sem_init_at_limit);
	RUN_TEST(test_sem_take_no_wait_empty);
	RUN_TEST(test_sem_auto_init_pre_main);
}
