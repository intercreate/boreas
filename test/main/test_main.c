/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas test runner.
 */

#include <stdlib.h>

#include "unity.h"

/* Test group declarations -- always available */
void test_timeout_group(void);
void test_slist_group(void);
void test_dlist_group(void);
void test_byteorder_group(void);
void test_atomic_group(void);
void test_k_sem_group(void);
void test_k_mutex_group(void);
void test_k_msgq_group(void);
void test_k_event_group(void);
void test_retry_group(void);
void test_log_group(void);
void test_device_registry_group(void);

/* Shell tests -- available on all targets when ZSHELL is enabled */
#if defined(CONFIG_ZSHELL)
void test_shell_group(void);
#endif

void test_uptime_group(void);
void test_sleep_group(void);
void test_k_timer_group(void);
void test_k_work_group(void);
void test_k_thread_group(void);
void test_k_mutex_pi_group(void);
void test_k_event_mt_group(void);
void test_init_group(void);
void test_k_msgq_mt_group(void);

#if !CONFIG_IDF_TARGET_LINUX
/* gpio_flags requires zdevice GPIO types (driver component). */
void test_gpio_flags_group(void);
#endif

void app_main(void)
{
	UNITY_BEGIN();

	/* Layer 0: Foundation */
	test_timeout_group();
	test_slist_group();
	test_dlist_group();
	test_byteorder_group();
	test_atomic_group();

	/* Layer 1: Kernel primitives */
	test_k_sem_group();
	test_k_mutex_group();
	test_k_msgq_group();
	test_k_event_group();

	/* System services */
	test_retry_group();
	test_log_group();

	/* Device model */
	test_device_registry_group();

#if defined(CONFIG_ZSHELL)
	/* Shell */
	test_shell_group();
#endif

	/* Layer 0: Uptime & Sleep */
	test_uptime_group();
	test_sleep_group();

	/* Layer 2: Timer & Work Queue (before the thread-heavy groups so a
	 * thread-lifecycle failure can't mask timer/work results) */
	test_k_timer_group();
	test_k_work_group();

	/* Layer 1: Thread */
	test_k_thread_group();

	/* Mutex priority inheritance (needs real scheduling) */
	test_k_mutex_pi_group();

	/* Multi-thread event tests */
	test_k_event_mt_group();

	/* Multi-thread msgq tests */
	test_k_msgq_mt_group();

#if !CONFIG_IDF_TARGET_LINUX
	/* SYS_INIT ordering tests.
	 * SKIPPED ON LINUX: SYS_INIT registration (constructor/linker-
	 * section based) does not fire in the host binary -- "No SYS_INIT
	 * entries registered" (Mach-O hosts at minimum). */
	test_init_group();
#endif

#if !CONFIG_IDF_TARGET_LINUX
	/* GPIO flag logic (needs zdevice GPIO types) */
	test_gpio_flags_group();
#endif

#if CONFIG_IDF_TARGET_LINUX
	exit(UNITY_END());
#else
	UNITY_END();
#endif
}
