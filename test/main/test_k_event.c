/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/kernel.h"

#define EVT_A BIT(0)
#define EVT_B BIT(1)
#define EVT_C BIT(2)

static void test_event_set_and_wait(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A | EVT_B);

	uint32_t got = k_event_wait(&evt, EVT_A, false, K_NO_WAIT);
	TEST_ASSERT_EQUAL(EVT_A, got & EVT_A);
}

static void test_event_wait_all(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A | EVT_B);

	/* Should succeed -- both bits set */
	uint32_t got = k_event_wait_all(&evt, EVT_A | EVT_B, false, K_NO_WAIT);
	TEST_ASSERT_EQUAL(EVT_A | EVT_B, got);
}

static void test_event_wait_all_missing(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A); /* Only A, not B */

	/* Should timeout -- waiting for both A and B */
	uint32_t got = k_event_wait_all(&evt, EVT_A | EVT_B, false, K_NO_WAIT);
	TEST_ASSERT_NOT_EQUAL(EVT_A | EVT_B, got);
}

static void test_event_clear(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A | EVT_B | EVT_C);
	k_event_clear(&evt, EVT_B);

	uint32_t got = k_event_wait(&evt, EVT_B, false, K_NO_WAIT);
	TEST_ASSERT_EQUAL(0, got & EVT_B);
}

static void test_event_post(void)
{
	struct k_event evt;
	k_event_init(&evt);

	/* post MERGES (set replaces -- upstream semantics) */
	k_event_set(&evt, EVT_B);
	k_event_post(&evt, EVT_A | EVT_C);

	uint32_t got = k_event_wait_all(&evt, EVT_A | EVT_B | EVT_C, false, K_NO_WAIT);
	TEST_ASSERT_EQUAL(EVT_A | EVT_B | EVT_C, got); /* B survived the post */
}

static void test_event_wait_with_reset(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A | EVT_B);

	/* Upstream semantics: reset=true zeroes the ENTIRE tracked set
	 * BEFORE waiting -- so a K_NO_WAIT wait after reset sees nothing,
	 * and EVT_B is gone too. (The old FreeRTOS backend cleared only
	 * the matched bits, after the wait.) */
	uint32_t got = k_event_wait(&evt, EVT_A, true, K_NO_WAIT);
	TEST_ASSERT_EQUAL(0, got);

	got = k_event_wait(&evt, EVT_A | EVT_B, false, K_NO_WAIT);
	TEST_ASSERT_EQUAL(0, got);
}

static void test_event_wait_timeout(void)
{
	struct k_event evt;
	k_event_init(&evt);

	/* Wait for a bit that is NOT set -- should return 0 (timeout) */
	uint32_t got = k_event_wait(&evt, EVT_A, false, K_NO_WAIT);
	TEST_ASSERT_EQUAL(0, got);
}

static void test_event_set_clear_all(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A | EVT_B | EVT_C);
	k_event_clear(&evt, EVT_A | EVT_B | EVT_C);

	/* All cleared -- wait should return 0 */
	uint32_t got = k_event_wait(&evt, EVT_A | EVT_B | EVT_C, false, K_NO_WAIT);
	TEST_ASSERT_EQUAL(0, got);
}

static void test_event_set_replaces(void)
{
	struct k_event evt;
	k_event_init(&evt);

	/* Upstream: set REPLACES the tracked set (post merges). */
	k_event_set(&evt, EVT_A);
	k_event_set(&evt, EVT_B);

	TEST_ASSERT_EQUAL(0, k_event_test(&evt, EVT_A)); /* A replaced away */
	TEST_ASSERT_EQUAL(EVT_B, k_event_test(&evt, EVT_B));

	k_event_post(&evt, EVT_A); /* merge keeps B */
	TEST_ASSERT_EQUAL(EVT_A | EVT_B, k_event_test(&evt, EVT_A | EVT_B));
}

static void test_event_previous_value_returns(void)
{
	struct k_event evt;
	k_event_init(&evt);

	/* All mutators return the PREVIOUS value of the affected bits. */
	TEST_ASSERT_EQUAL(0, k_event_set(&evt, EVT_A | EVT_B));
	TEST_ASSERT_EQUAL(EVT_A, k_event_post(&evt, EVT_A | EVT_C));
	TEST_ASSERT_EQUAL(EVT_B, k_event_clear(&evt, EVT_B));
	TEST_ASSERT_EQUAL(EVT_A | EVT_C, k_event_set(&evt, 0));
}

static void test_event_set_masked(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A | EVT_B);

	/* Overwrite only the B|C lanes: B clears, C sets, A untouched. */
	TEST_ASSERT_EQUAL(EVT_B, k_event_set_masked(&evt, EVT_C, EVT_B | EVT_C));
	TEST_ASSERT_EQUAL(EVT_A | EVT_C, k_event_test(&evt, EVT_A | EVT_B | EVT_C));
}

static void test_event_wait_safe_consumes(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A | EVT_B);

	/* _safe consumes the matched bits atomically; others survive. */
	TEST_ASSERT_EQUAL(EVT_A, k_event_wait_safe(&evt, EVT_A, false, K_NO_WAIT));
	TEST_ASSERT_EQUAL(0, k_event_test(&evt, EVT_A));
	TEST_ASSERT_EQUAL(EVT_B, k_event_test(&evt, EVT_B));
}

static void test_event_wait_all_timeout_returns_zero(void)
{
	struct k_event evt;
	k_event_init(&evt);

	k_event_set(&evt, EVT_A); /* partial match only */

	/* Upstream: timeout returns 0 -- NOT the partial bits (the old
	 * event-group backend leaked a truthy partial mask here). */
	TEST_ASSERT_EQUAL(0, k_event_wait_all(&evt, EVT_A | EVT_B, false, K_MSEC(30)));
	TEST_ASSERT_EQUAL(EVT_A, k_event_test(&evt, EVT_A)); /* untouched */
}

static void test_event_full_32_bits(void)
{
	struct k_event evt;
	k_event_init(&evt);

	/* The FreeRTOS event-group backend reserved bits 24-31; the
	 * notification-backed implementation tracks all 32. */
	uint32_t high = BIT(24) | BIT(31);

	TEST_ASSERT_EQUAL(0, k_event_set(&evt, high));
	TEST_ASSERT_EQUAL(high, k_event_wait_all(&evt, high, false, K_NO_WAIT));
	TEST_ASSERT_EQUAL(high, k_event_clear(&evt, high));
	TEST_ASSERT_EQUAL(0, k_event_test(&evt, UINT32_MAX));
}

K_EVENT_DEFINE(static_evt);

static void test_event_define_static_init(void)
{
	/* Compile-time initializer: usable without k_event_init. */
	TEST_ASSERT_EQUAL(0, k_event_test(&static_evt, UINT32_MAX));
	k_event_post(&static_evt, EVT_C);
	TEST_ASSERT_EQUAL(EVT_C, k_event_test(&static_evt, EVT_C));
}

void test_k_event_group(void)
{
	RUN_TEST(test_event_set_and_wait);
	RUN_TEST(test_event_wait_all);
	RUN_TEST(test_event_wait_all_missing);
	RUN_TEST(test_event_clear);
	RUN_TEST(test_event_post);
	RUN_TEST(test_event_wait_with_reset);
	RUN_TEST(test_event_wait_timeout);
	RUN_TEST(test_event_set_clear_all);
	RUN_TEST(test_event_set_replaces);
	RUN_TEST(test_event_previous_value_returns);
	RUN_TEST(test_event_set_masked);
	RUN_TEST(test_event_wait_safe_consumes);
	RUN_TEST(test_event_wait_all_timeout_returns_zero);
	RUN_TEST(test_event_full_32_bits);
	RUN_TEST(test_event_define_static_init);
}
