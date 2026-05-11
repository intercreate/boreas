/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * SYS_INIT ordering and shutdown tests.
 *
 * Requires real linker sections (HW-only, not linux target).
 * All SYS_INIT entries in this file land in .sys_init_entries and are
 * executed by every call to sys_init_run_all(). Tests use a shared
 * counter array to verify execution order.
 */

#include <stdbool.h>
#include <string.h>

#include "unity.h"
#include "zephyr/init.h"

/* Execution log: each init/shutdown function appends its tag here. */
#define MAX_LOG 16
static int exec_log[MAX_LOG];
static int exec_idx;

static void log_tag(int tag)
{
	int i = exec_idx;
	if (i < MAX_LOG) {
		exec_log[i] = tag;
		exec_idx = i + 1;
	}
}

static void reset_log(void)
{
	memset(exec_log, 0, sizeof(exec_log));
	exec_idx = 0;
}

/* Conditional failure flag for the init-failure-halts test. */
static bool should_fail;

/* --- SYS_INIT entries: spread across levels + priorities --- */

static int init_early_10(void)
{
	log_tag(1);
	return 0;
}
SYS_INIT(init_early_10, EARLY, 10);

static int init_device_5(void)
{
	log_tag(2);
	return 0;
}
SYS_INIT(init_device_5, DEVICE, 5);

static int init_device_20(void)
{
	log_tag(3);
	return should_fail ? -1 : 0;
}
SYS_INIT(init_device_20, DEVICE, 20);

static int init_app_0(void)
{
	log_tag(4);
	return 0;
}
SYS_INIT(init_app_0, APPLICATION, 0);

/* Shutdown entries for the reverse-order test. */

static int shutdown_early_10(void)
{
	log_tag(101);
	return 0;
}

static int shutdown_device_6(void)
{
	log_tag(102);
	return 0;
}

static int init_early_10_sd(void)
{
	log_tag(11);
	return 0;
}
SYS_INIT_WITH_SHUTDOWN(init_early_10_sd, shutdown_early_10, EARLY, 11);

static int init_device_6_sd(void)
{
	log_tag(12);
	return 0;
}
SYS_INIT_WITH_SHUTDOWN(init_device_6_sd, shutdown_device_6, DEVICE, 6);

/* -----------------------------------------------------------
 * Test 1: Multi-level ordering
 *
 * Entries should execute in level order (EARLY < DEVICE <
 * APPLICATION), with priority ordering within each level.
 * --------------------------------------------------------- */

static void test_init_ordering(void)
{
	reset_log();
	should_fail = false;

	int ret = sys_init_run_all();
	TEST_ASSERT_EQUAL(0, ret);

	/* Expected order (level, prio):
	 *   EARLY  10  -> tag 1
	 *   EARLY  11  -> tag 11  (shutdown entry)
	 *   DEVICE  5  -> tag 2
	 *   DEVICE  6  -> tag 12  (shutdown entry)
	 *   DEVICE 20  -> tag 3
	 *   APP     0  -> tag 4
	 */
	TEST_ASSERT_EQUAL(6, exec_idx);
	TEST_ASSERT_EQUAL(1, exec_log[0]);  /* EARLY 10 */
	TEST_ASSERT_EQUAL(11, exec_log[1]); /* EARLY 11 */
	TEST_ASSERT_EQUAL(2, exec_log[2]);  /* DEVICE 5 */
	TEST_ASSERT_EQUAL(12, exec_log[3]); /* DEVICE 6 (shutdown entry) */
	TEST_ASSERT_EQUAL(3, exec_log[4]);  /* DEVICE 20 */
	TEST_ASSERT_EQUAL(4, exec_log[5]);  /* APP 0 */
}

/* -----------------------------------------------------------
 * Test 2: Shutdown reverse order
 *
 * Shutdown runs in reverse of the init sort order. Only
 * entries with a non-NULL shutdown_fn are called.
 * --------------------------------------------------------- */

static void test_shutdown_reverse(void)
{
	/* First run init so entries are valid */
	reset_log();
	should_fail = false;
	sys_init_run_all();

	/* Now clear and run shutdown */
	reset_log();
	sys_shutdown_run_all();

	/* Only 2 entries have shutdown functions.
	 * Init order was: EARLY 11 (tag 11), DEVICE 6 (tag 12).
	 * Shutdown reverses: DEVICE 6 first, EARLY 11 second. */
	TEST_ASSERT_EQUAL(2, exec_idx);
	TEST_ASSERT_EQUAL(102, exec_log[0]); /* shutdown_device_6 */
	TEST_ASSERT_EQUAL(101, exec_log[1]); /* shutdown_early_10 */
}

/* -----------------------------------------------------------
 * Test 3: Init failure halts subsequent entries
 *
 * When an init function returns non-zero, sys_init_run_all()
 * stops and returns that error. Entries after the failing one
 * must not execute.
 * --------------------------------------------------------- */

static void test_init_failure_halts(void)
{
	reset_log();
	should_fail = true;

	int ret = sys_init_run_all();
	TEST_ASSERT_EQUAL(-1, ret);

	/* init_device_20 (tag 3) returns -1.
	 * Entries before it should have run; init_app_0 (tag 4) should NOT. */
	TEST_ASSERT_EQUAL(1, exec_log[0]);  /* EARLY 10 */
	TEST_ASSERT_EQUAL(11, exec_log[1]); /* EARLY 11 */
	TEST_ASSERT_EQUAL(2, exec_log[2]);  /* DEVICE 5 */
	TEST_ASSERT_EQUAL(12, exec_log[3]); /* DEVICE 6 (shutdown entry) */
	TEST_ASSERT_EQUAL(3, exec_log[4]);  /* DEVICE 20 — ran but failed */
	TEST_ASSERT_EQUAL(5, exec_idx);     /* APP 0 did NOT run */
}

void test_init_group(void)
{
	RUN_TEST(test_init_ordering);
	RUN_TEST(test_shutdown_reverse);
	RUN_TEST(test_init_failure_halts);
}
