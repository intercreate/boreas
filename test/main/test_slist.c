/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/sys/slist.h"

struct test_item {
	int value;
	sys_snode_t node;
};

static void test_slist_init_empty(void)
{
	sys_slist_t list;
	sys_slist_init(&list);
	TEST_ASSERT_TRUE(sys_slist_is_empty(&list));
	TEST_ASSERT_NULL(sys_slist_peek_head(&list));
	TEST_ASSERT_NULL(sys_slist_peek_tail(&list));
	TEST_ASSERT_EQUAL(0, sys_slist_len(&list));
}

static void test_slist_append(void)
{
	sys_slist_t list;
	sys_slist_init(&list);

	struct test_item a = {.value = 1};
	struct test_item b = {.value = 2};
	struct test_item c = {.value = 3};

	sys_slist_append(&list, &a.node);
	sys_slist_append(&list, &b.node);
	sys_slist_append(&list, &c.node);

	TEST_ASSERT_EQUAL(3, sys_slist_len(&list));
	TEST_ASSERT_EQUAL_PTR(&a.node, sys_slist_peek_head(&list));
	TEST_ASSERT_EQUAL_PTR(&c.node, sys_slist_peek_tail(&list));
}

static void test_slist_prepend(void)
{
	sys_slist_t list;
	sys_slist_init(&list);

	struct test_item a = {.value = 1};
	struct test_item b = {.value = 2};

	sys_slist_append(&list, &a.node);
	sys_slist_prepend(&list, &b.node);

	TEST_ASSERT_EQUAL_PTR(&b.node, sys_slist_peek_head(&list));
	TEST_ASSERT_EQUAL_PTR(&a.node, sys_slist_peek_tail(&list));
}

static void test_slist_get(void)
{
	sys_slist_t list;
	sys_slist_init(&list);

	struct test_item a = {.value = 1};
	struct test_item b = {.value = 2};

	sys_slist_append(&list, &a.node);
	sys_slist_append(&list, &b.node);

	sys_snode_t *node = sys_slist_get(&list);
	TEST_ASSERT_EQUAL_PTR(&a.node, node);
	TEST_ASSERT_EQUAL(1, sys_slist_len(&list));

	node = sys_slist_get(&list);
	TEST_ASSERT_EQUAL_PTR(&b.node, node);
	TEST_ASSERT_TRUE(sys_slist_is_empty(&list));

	node = sys_slist_get(&list);
	TEST_ASSERT_NULL(node);
}

static void test_slist_find_and_remove(void)
{
	sys_slist_t list;
	sys_slist_init(&list);

	struct test_item a = {.value = 1};
	struct test_item b = {.value = 2};
	struct test_item c = {.value = 3};

	sys_slist_append(&list, &a.node);
	sys_slist_append(&list, &b.node);
	sys_slist_append(&list, &c.node);

	TEST_ASSERT_TRUE(sys_slist_find_and_remove(&list, &b.node));
	TEST_ASSERT_EQUAL(2, sys_slist_len(&list));
	TEST_ASSERT_FALSE(sys_slist_find_and_remove(&list, &b.node));
}

static void test_slist_container_iteration(void)
{
	sys_slist_t list;
	sys_slist_init(&list);

	struct test_item items[3] = {{.value = 10}, {.value = 20}, {.value = 30}};
	for (int i = 0; i < 3; i++) {
		sys_slist_append(&list, &items[i].node);
	}

	int sum = 0;
	struct test_item *item;
	SYS_SLIST_FOR_EACH_CONTAINER(&list, item, node) {
		sum += item->value;
	}
	TEST_ASSERT_EQUAL(60, sum);
}

static void test_slist_remove_direct(void)
{
	sys_slist_t list;
	sys_slist_init(&list);

	struct test_item a = {.value = 1};
	struct test_item b = {.value = 2};
	struct test_item c = {.value = 3};

	sys_slist_append(&list, &a.node);
	sys_slist_append(&list, &b.node);
	sys_slist_append(&list, &c.node);

	/* Remove middle node (prev=a, node=b) */
	sys_slist_remove(&list, &a.node, &b.node);
	TEST_ASSERT_EQUAL(2, sys_slist_len(&list));

	/* Remove head (prev=NULL, node=a) */
	sys_slist_remove(&list, NULL, &a.node);
	TEST_ASSERT_EQUAL(1, sys_slist_len(&list));
	TEST_ASSERT_EQUAL_PTR(&c.node, sys_slist_peek_head(&list));
}

void test_slist_group(void)
{
	RUN_TEST(test_slist_init_empty);
	RUN_TEST(test_slist_append);
	RUN_TEST(test_slist_prepend);
	RUN_TEST(test_slist_get);
	RUN_TEST(test_slist_find_and_remove);
	RUN_TEST(test_slist_container_iteration);
	RUN_TEST(test_slist_remove_direct);
}
