/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <errno.h>
#include <string.h>

#include "unity.h"
#include "zephyr/kernel.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_K_TIMER_DISPATCH_ISR
#include "esp_attr.h"
#endif

/* block_size must be >= sizeof(void*) and word-aligned; use a generous
 * 32-byte block so payloads are easy to scribble. */
#define BLK_SIZE   32
#define NUM_BLOCKS 4

static uint8_t slab_buf[BLK_SIZE * NUM_BLOCKS] __attribute__((aligned(sizeof(void *))));

/* ----------------------------------------------------------------
 * init + accounting
 * ---------------------------------------------------------------- */

static void test_mem_slab_init_accounting(void)
{
	struct k_mem_slab slab;

	TEST_ASSERT_EQUAL(0, k_mem_slab_init(&slab, slab_buf, BLK_SIZE, NUM_BLOCKS));
	TEST_ASSERT_EQUAL(0, k_mem_slab_num_used_get(&slab));
	TEST_ASSERT_EQUAL(NUM_BLOCKS, k_mem_slab_num_free_get(&slab));
	TEST_ASSERT_EQUAL(0, k_mem_slab_max_used_get(&slab));
}

static void test_mem_slab_init_invalid_args(void)
{
	struct k_mem_slab slab;

	/* zero block_size / zero num_blocks */
	TEST_ASSERT_EQUAL(-EINVAL, k_mem_slab_init(&slab, slab_buf, 0, NUM_BLOCKS));
	TEST_ASSERT_EQUAL(-EINVAL, k_mem_slab_init(&slab, slab_buf, BLK_SIZE, 0));

	/* non-word-aligned block_size (the free-list next-ptr needs alignment) */
	TEST_ASSERT_EQUAL(-EINVAL, k_mem_slab_init(&slab, slab_buf, BLK_SIZE + 1, NUM_BLOCKS));

	/* non-word-aligned buffer base */
	TEST_ASSERT_EQUAL(-EINVAL, k_mem_slab_init(&slab, slab_buf + 1, BLK_SIZE, NUM_BLOCKS));
}

/* ----------------------------------------------------------------
 * alloc / free round-trip, distinctness, accounting
 * ---------------------------------------------------------------- */

static void test_mem_slab_alloc_distinct_in_bounds(void)
{
	struct k_mem_slab slab;
	void *blocks[NUM_BLOCKS] = {0};

	TEST_ASSERT_EQUAL(0, k_mem_slab_init(&slab, slab_buf, BLK_SIZE, NUM_BLOCKS));

	for (int i = 0; i < NUM_BLOCKS; i++) {
		TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&slab, &blocks[i], K_NO_WAIT));
		TEST_ASSERT_NOT_NULL(blocks[i]);

		/* word-aligned, inside the buffer */
		TEST_ASSERT_EQUAL(0, ((uintptr_t)blocks[i]) & (sizeof(void *) - 1));
		TEST_ASSERT_TRUE((uint8_t *)blocks[i] >= slab_buf);
		TEST_ASSERT_TRUE((uint8_t *)blocks[i] + BLK_SIZE <= slab_buf + sizeof(slab_buf));

		/* distinct from every earlier block */
		for (int j = 0; j < i; j++) {
			TEST_ASSERT_NOT_EQUAL(blocks[j], blocks[i]);
		}
		/* writable full width without clobbering neighbours */
		memset(blocks[i], 0xA0 + i, BLK_SIZE);
	}

	TEST_ASSERT_EQUAL(NUM_BLOCKS, k_mem_slab_num_used_get(&slab));
	TEST_ASSERT_EQUAL(0, k_mem_slab_num_free_get(&slab));

	/* The writes did not overlap: each block still holds its own pattern. */
	for (int i = 0; i < NUM_BLOCKS; i++) {
		uint8_t *b = blocks[i];

		for (int k = 0; k < BLK_SIZE; k++) {
			TEST_ASSERT_EQUAL_UINT8(0xA0 + i, b[k]);
		}
	}

	/* Exhausted: K_NO_WAIT alloc fails with -ENOMEM and NULLs the out-ptr. */
	void *extra = (void *)0xdead;

	TEST_ASSERT_EQUAL(-ENOMEM, k_mem_slab_alloc(&slab, &extra, K_NO_WAIT));
	TEST_ASSERT_NULL(extra);

	/* Free all, pool is whole again and re-allocatable. */
	for (int i = 0; i < NUM_BLOCKS; i++) {
		k_mem_slab_free(&slab, blocks[i]);
		TEST_ASSERT_EQUAL(NUM_BLOCKS - 1 - i, k_mem_slab_num_used_get(&slab));
	}
	TEST_ASSERT_EQUAL(NUM_BLOCKS, k_mem_slab_num_free_get(&slab));

	void *again;

	TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&slab, &again, K_NO_WAIT));
	TEST_ASSERT_NOT_NULL(again);
	k_mem_slab_free(&slab, again);

	/* High-water mark survived the frees. */
	TEST_ASSERT_EQUAL(NUM_BLOCKS, k_mem_slab_max_used_get(&slab));
}

static void test_mem_slab_alloc_timeout(void)
{
	struct k_mem_slab slab;
	void *blocks[NUM_BLOCKS];

	TEST_ASSERT_EQUAL(0, k_mem_slab_init(&slab, slab_buf, BLK_SIZE, NUM_BLOCKS));
	for (int i = 0; i < NUM_BLOCKS; i++) {
		TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&slab, &blocks[i], K_NO_WAIT));
	}

	/* A blocking alloc on an exhausted slab times out cleanly. */
	void *late = (void *)0xdead;
	uint32_t t0 = (uint32_t)k_uptime_get();

	TEST_ASSERT_EQUAL(-EAGAIN, k_mem_slab_alloc(&slab, &late, K_MSEC(30)));
	TEST_ASSERT_NULL(late);
	TEST_ASSERT_GREATER_OR_EQUAL(25, (uint32_t)k_uptime_get() - t0);

	for (int i = 0; i < NUM_BLOCKS; i++) {
		k_mem_slab_free(&slab, blocks[i]);
	}
}

/* ----------------------------------------------------------------
 * K_MEM_SLAB_DEFINE -- usable without an explicit init (lazy threading)
 * ---------------------------------------------------------------- */

K_MEM_SLAB_DEFINE(declared_slab, 16, 3, 4);

static void test_mem_slab_define_usable(void)
{
	void *a, *b, *c, *d;

	TEST_ASSERT_EQUAL(3, k_mem_slab_num_free_get(&declared_slab));

	TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&declared_slab, &a, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&declared_slab, &b, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&declared_slab, &c, K_NO_WAIT));
	TEST_ASSERT_NOT_EQUAL(a, b);
	TEST_ASSERT_NOT_EQUAL(b, c);

	/* Empty now. */
	TEST_ASSERT_EQUAL(-ENOMEM, k_mem_slab_alloc(&declared_slab, &d, K_NO_WAIT));

	k_mem_slab_free(&declared_slab, b);
	TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&declared_slab, &d, K_NO_WAIT));
	TEST_ASSERT_EQUAL(b, d); /* the just-freed block came back */

	k_mem_slab_free(&declared_slab, a);
	k_mem_slab_free(&declared_slab, c);
	k_mem_slab_free(&declared_slab, d);
}

/* ----------------------------------------------------------------
 * Multi-thread: a blocking alloc is woken by a free.
 * ---------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(freer_stack, 4096);
static struct k_thread freer_thread;
static struct k_mem_slab mt_slab;
static void *mt_held[NUM_BLOCKS];

static void freer_entry(void *p1, void *p2, void *p3)
{
	(void)p2;
	(void)p3;
	k_msleep(30);
	/* Return one block so the blocked allocator below can proceed. */
	k_mem_slab_free((struct k_mem_slab *)p1, mt_held[0]);
}

static void test_mem_slab_blocking_alloc_woken_by_free(void)
{
	TEST_ASSERT_EQUAL(0, k_mem_slab_init(&mt_slab, slab_buf, BLK_SIZE, NUM_BLOCKS));
	for (int i = 0; i < NUM_BLOCKS; i++) {
		TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&mt_slab, &mt_held[i], K_NO_WAIT));
	}

	k_thread_create(&freer_thread, freer_stack, K_THREAD_STACK_SIZEOF(freer_stack), freer_entry,
			&mt_slab, NULL, NULL, 5, 0, K_NO_WAIT);

	/* Blocks until the freer returns mt_held[0] ~30 ms out. */
	void *got = NULL;

	TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&mt_slab, &got, K_FOREVER));
	TEST_ASSERT_EQUAL(mt_held[0], got);

	TEST_ASSERT_EQUAL(0, k_thread_join(&freer_thread, K_SECONDS(2)));

	k_mem_slab_free(&mt_slab, got);
	for (int i = 1; i < NUM_BLOCKS; i++) {
		k_mem_slab_free(&mt_slab, mt_held[i]);
	}
}

/* ----------------------------------------------------------------
 * Multi-thread: N>blocks allocators block; frees wake them; every
 * waiter gets a distinct block (units conserved, no double-alloc).
 * ---------------------------------------------------------------- */

#define N_WAITERS 6

/* Boreas has no K_THREAD_STACK_ARRAY_DEFINE; K_THREAD_STACK_DEFINE is a
 * StackType_t[] sized in words, so an array of those is the 2D form. */
static StackType_t waiter_stacks[N_WAITERS][4096 / sizeof(StackType_t)];
static struct k_thread waiter_threads[N_WAITERS];
static void *waiter_block[N_WAITERS];
static volatile int waiter_ret[N_WAITERS];

static void waiter_entry(void *p1, void *p2, void *p3)
{
	(void)p3;
	int idx = (int)(intptr_t)p1;

	waiter_ret[idx] =
		k_mem_slab_alloc((struct k_mem_slab *)p2, &waiter_block[idx], K_SECONDS(2));
}

static void test_mem_slab_multi_waiter_conservation(void)
{
	TEST_ASSERT_EQUAL(0, k_mem_slab_init(&mt_slab, slab_buf, BLK_SIZE, NUM_BLOCKS));

	/* Drain the pool so every waiter must block. */
	void *seed[NUM_BLOCKS];

	for (int i = 0; i < NUM_BLOCKS; i++) {
		TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&mt_slab, &seed[i], K_NO_WAIT));
	}

	for (int i = 0; i < N_WAITERS; i++) {
		waiter_block[i] = NULL;
		waiter_ret[i] = 0xbad;
		k_thread_create(&waiter_threads[i], waiter_stacks[i],
				K_THREAD_STACK_SIZEOF(waiter_stacks[i]), waiter_entry,
				(void *)(intptr_t)i, &mt_slab, NULL, 5, 0, K_NO_WAIT);
	}
	k_msleep(20); /* all six parked in k_mem_slab_alloc */

	/* Release the 4 seed blocks; exactly 4 of the 6 waiters should win,
	 * each with a distinct block, the other 2 time out. */
	for (int i = 0; i < NUM_BLOCKS; i++) {
		k_mem_slab_free(&mt_slab, seed[i]);
	}

	for (int i = 0; i < N_WAITERS; i++) {
		TEST_ASSERT_EQUAL(0, k_thread_join(&waiter_threads[i], K_SECONDS(3)));
	}

	int winners = 0;

	for (int i = 0; i < N_WAITERS; i++) {
		if (waiter_ret[i] == 0) {
			winners++;
			TEST_ASSERT_NOT_NULL(waiter_block[i]);
			/* distinct from every other winner */
			for (int j = 0; j < N_WAITERS; j++) {
				if (j != i && waiter_ret[j] == 0) {
					TEST_ASSERT_NOT_EQUAL(waiter_block[j], waiter_block[i]);
				}
			}
		} else {
			TEST_ASSERT_EQUAL(-EAGAIN, waiter_ret[i]);
			TEST_ASSERT_NULL(waiter_block[i]);
		}
	}
	TEST_ASSERT_EQUAL(NUM_BLOCKS, winners);
	TEST_ASSERT_EQUAL(NUM_BLOCKS, k_mem_slab_num_used_get(&mt_slab));

	/* Hand the won blocks back. */
	for (int i = 0; i < N_WAITERS; i++) {
		if (waiter_ret[i] == 0) {
			k_mem_slab_free(&mt_slab, waiter_block[i]);
		}
	}
}

/* ----------------------------------------------------------------
 * FromISR free wakes a blocked allocator (HW only; prior art: the
 * CONFIG_K_TIMER_DISPATCH_ISR tests in test_k_timer.c).
 * ---------------------------------------------------------------- */

#ifdef CONFIG_K_TIMER_DISPATCH_ISR

static struct k_mem_slab isr_slab;
static void *isr_held[NUM_BLOCKS];
static volatile bool isr_free_was_isr;

static void IRAM_ATTR slab_isr_free_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	isr_free_was_isr = xPortInIsrContext();
	k_mem_slab_free(&isr_slab, isr_held[0]);
}

static void test_mem_slab_isr_free_wakes_waiter(void)
{
	struct k_timer timer;

	TEST_ASSERT_EQUAL(0, k_mem_slab_init(&isr_slab, slab_buf, BLK_SIZE, NUM_BLOCKS));
	for (int i = 0; i < NUM_BLOCKS; i++) {
		TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&isr_slab, &isr_held[i], K_NO_WAIT));
	}
	isr_free_was_isr = false;

	k_timer_init(&timer, slab_isr_free_cb, NULL);
	k_timer_start(&timer, K_MSEC(10), K_NO_WAIT);

	void *got = NULL;

	TEST_ASSERT_EQUAL(0, k_mem_slab_alloc(&isr_slab, &got, K_SECONDS(1)));
	TEST_ASSERT_EQUAL(isr_held[0], got);

	k_timer_stop(&timer);
	TEST_ASSERT_TRUE_MESSAGE(isr_free_was_isr, "free did not run in ISR context");

	k_mem_slab_free(&isr_slab, got);
	for (int i = 1; i < NUM_BLOCKS; i++) {
		k_mem_slab_free(&isr_slab, isr_held[i]);
	}
}

#endif /* CONFIG_K_TIMER_DISPATCH_ISR */

void test_k_mem_slab_group(void)
{
	RUN_TEST(test_mem_slab_init_accounting);
	RUN_TEST(test_mem_slab_init_invalid_args);
	RUN_TEST(test_mem_slab_alloc_distinct_in_bounds);
	RUN_TEST(test_mem_slab_alloc_timeout);
	RUN_TEST(test_mem_slab_define_usable);
	RUN_TEST(test_mem_slab_blocking_alloc_woken_by_free);
	RUN_TEST(test_mem_slab_multi_waiter_conservation);
#ifdef CONFIG_K_TIMER_DISPATCH_ISR
	RUN_TEST(test_mem_slab_isr_free_wakes_waiter);
#endif
}
