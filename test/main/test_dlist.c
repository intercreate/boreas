/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "unity.h"
#include "zephyr/sys/dlist.h"

struct test_ditem {
    int value;
    sys_dnode_t node;
};

static void test_dlist_init_empty(void)
{
    sys_dlist_t list;
    sys_dlist_init(&list);
    TEST_ASSERT_TRUE(sys_dlist_is_empty(&list));
    TEST_ASSERT_NULL(sys_dlist_peek_head(&list));
    TEST_ASSERT_NULL(sys_dlist_peek_tail(&list));
    TEST_ASSERT_EQUAL(0, sys_dlist_len(&list));
}

static void test_dlist_append_and_remove(void)
{
    sys_dlist_t list;
    sys_dlist_init(&list);

    struct test_ditem a = {.value = 1};
    struct test_ditem b = {.value = 2};
    struct test_ditem c = {.value = 3};

    sys_dlist_append(&list, &a.node);
    sys_dlist_append(&list, &b.node);
    sys_dlist_append(&list, &c.node);
    TEST_ASSERT_EQUAL(3, sys_dlist_len(&list));

    /* O(1) removal from middle */
    sys_dlist_remove(&b.node);
    TEST_ASSERT_EQUAL(2, sys_dlist_len(&list));
    TEST_ASSERT_FALSE(sys_dnode_is_linked(&b.node));

    /* Verify remaining order */
    sys_dnode_t *n = sys_dlist_peek_head(&list);
    TEST_ASSERT_EQUAL_PTR(&a.node, n);
    n = sys_dlist_peek_next(&list, n);
    TEST_ASSERT_EQUAL_PTR(&c.node, n);
}

static void test_dlist_get_fifo(void)
{
    sys_dlist_t list;
    sys_dlist_init(&list);

    struct test_ditem a = {.value = 1};
    struct test_ditem b = {.value = 2};

    sys_dlist_append(&list, &a.node);
    sys_dlist_append(&list, &b.node);

    sys_dnode_t *node = sys_dlist_get(&list);
    struct test_ditem *item = CONTAINER_OF(node, struct test_ditem, node);
    TEST_ASSERT_EQUAL(1, item->value);

    node = sys_dlist_get(&list);
    item = CONTAINER_OF(node, struct test_ditem, node);
    TEST_ASSERT_EQUAL(2, item->value);

    TEST_ASSERT_TRUE(sys_dlist_is_empty(&list));
}

static void test_dlist_prepend(void)
{
    sys_dlist_t list;
    sys_dlist_init(&list);

    struct test_ditem a = {.value = 1};
    struct test_ditem b = {.value = 2};

    sys_dlist_append(&list, &a.node);
    sys_dlist_prepend(&list, &b.node);

    TEST_ASSERT_EQUAL_PTR(&b.node, sys_dlist_peek_head(&list));
    TEST_ASSERT_EQUAL_PTR(&a.node, sys_dlist_peek_tail(&list));
}

static void test_dlist_insert_before(void)
{
    sys_dlist_t list;
    sys_dlist_init(&list);

    struct test_ditem a = {.value = 1};
    struct test_ditem b = {.value = 3};
    struct test_ditem c = {.value = 2};

    sys_dlist_append(&list, &a.node);
    sys_dlist_append(&list, &b.node);
    /* Insert c before b */
    sys_dlist_insert(&b.node, &c.node);

    TEST_ASSERT_EQUAL(3, sys_dlist_len(&list));

    /* Order should be a(1), c(2), b(3) */
    sys_dnode_t *n = sys_dlist_peek_head(&list);
    TEST_ASSERT_EQUAL(1, CONTAINER_OF(n, struct test_ditem, node)->value);
    n = sys_dlist_peek_next(&list, n);
    TEST_ASSERT_EQUAL(2, CONTAINER_OF(n, struct test_ditem, node)->value);
    n = sys_dlist_peek_next(&list, n);
    TEST_ASSERT_EQUAL(3, CONTAINER_OF(n, struct test_ditem, node)->value);
}

static void test_dlist_peek_prev(void)
{
    sys_dlist_t list;
    sys_dlist_init(&list);

    struct test_ditem a = {.value = 1};
    struct test_ditem b = {.value = 2};

    sys_dlist_append(&list, &a.node);
    sys_dlist_append(&list, &b.node);

    sys_dnode_t *tail = sys_dlist_peek_tail(&list);
    TEST_ASSERT_EQUAL_PTR(&b.node, tail);

    sys_dnode_t *prev = sys_dlist_peek_prev(&list, tail);
    TEST_ASSERT_EQUAL_PTR(&a.node, prev);

    /* prev of head is NULL */
    TEST_ASSERT_NULL(sys_dlist_peek_prev(&list, &a.node));
}

static void test_dlist_container_iteration(void)
{
    sys_dlist_t list;
    sys_dlist_init(&list);

    struct test_ditem items[3] = {{.value = 10}, {.value = 20}, {.value = 30}};
    for (int i = 0; i < 3; i++) {
        sys_dlist_append(&list, &items[i].node);
    }

    int sum = 0;
    struct test_ditem *item;
    SYS_DLIST_FOR_EACH_CONTAINER(&list, item, node) {
        sum += item->value;
    }
    TEST_ASSERT_EQUAL(60, sum);
}

void test_dlist_group(void)
{
    RUN_TEST(test_dlist_init_empty);
    RUN_TEST(test_dlist_append_and_remove);
    RUN_TEST(test_dlist_get_fifo);
    RUN_TEST(test_dlist_prepend);
    RUN_TEST(test_dlist_insert_before);
    RUN_TEST(test_dlist_peek_prev);
    RUN_TEST(test_dlist_container_iteration);
}
