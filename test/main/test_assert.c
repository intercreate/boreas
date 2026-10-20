/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/sys/__assert.h"

static void test_assert_disabled_arguments(void)
{
#if !defined(CONFIG_ASSERT) || !CONFIG_ASSERT
	int condition_calls = 0;
	int message_calls = 0;

	__ASSERT(++condition_calls, (++message_calls, "disabled assertion"));
	__ASSERT_NO_MSG(++condition_calls);

	TEST_ASSERT_EQUAL_INT(0, condition_calls);
	TEST_ASSERT_EQUAL_INT(0, message_calls);

	/* A false condition must not terminate the program when disabled. */
	__ASSERT(0, "must not abort");
	__ASSERT_NO_MSG(0);
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_ASSERT=n");
#endif
}

static void test_assert_enabled_arguments(void)
{
#if defined(CONFIG_ASSERT) && CONFIG_ASSERT
	int condition_calls = 0;
	int message_calls = 0;

	__ASSERT(++condition_calls, (++message_calls, "unused message"));
	__ASSERT_NO_MSG(++condition_calls);

	TEST_ASSERT_EQUAL_INT(2, condition_calls);
	TEST_ASSERT_EQUAL_INT(0, message_calls);
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_ASSERT=y");
#endif
}

void test_assert_group(void)
{
	RUN_TEST(test_assert_disabled_arguments);
	RUN_TEST(test_assert_enabled_arguments);
}
