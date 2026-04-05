/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible intrusive singly-linked list.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "zephyr/sys/util.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _snode {
    struct _snode *next;
} sys_snode_t;

typedef struct _slist {
    sys_snode_t *head;
    sys_snode_t *tail;
} sys_slist_t;

/* Static initializer */
#define SYS_SLIST_STATIC_INIT(ptr_to_list) {NULL, NULL}

static inline void sys_slist_init(sys_slist_t *list)
{
    list->head = NULL;
    list->tail = NULL;
}

static inline bool sys_slist_is_empty(const sys_slist_t *list)
{
    return list->head == NULL;
}

static inline sys_snode_t *sys_slist_peek_head(const sys_slist_t *list)
{
    return list->head;
}

static inline sys_snode_t *sys_slist_peek_tail(const sys_slist_t *list)
{
    return list->tail;
}

static inline sys_snode_t *sys_slist_peek_next(const sys_snode_t *node)
{
    return node->next;
}

static inline void sys_slist_prepend(sys_slist_t *list, sys_snode_t *node)
{
    node->next = list->head;
    list->head = node;
    if (list->tail == NULL) {
        list->tail = node;
    }
}

static inline void sys_slist_append(sys_slist_t *list, sys_snode_t *node)
{
    node->next = NULL;
    if (list->tail != NULL) {
        list->tail->next = node;
    }
    list->tail = node;
    if (list->head == NULL) {
        list->head = node;
    }
}

static inline sys_snode_t *sys_slist_get(sys_slist_t *list)
{
    sys_snode_t *node = list->head;
    if (node != NULL) {
        list->head = node->next;
        if (list->head == NULL) {
            list->tail = NULL;
        }
        node->next = NULL;
    }
    return node;
}

static inline void sys_slist_remove(sys_slist_t *list, sys_snode_t *prev,
                                    sys_snode_t *node)
{
    if (prev == NULL) {
        list->head = node->next;
    } else {
        prev->next = node->next;
    }
    if (list->tail == node) {
        list->tail = prev;
    }
    node->next = NULL;
}

static inline bool sys_slist_find_and_remove(sys_slist_t *list,
                                             sys_snode_t *node)
{
    sys_snode_t *prev = NULL;
    sys_snode_t *cur = list->head;

    while (cur != NULL) {
        if (cur == node) {
            sys_slist_remove(list, prev, cur);
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    return false;
}

static inline size_t sys_slist_len(const sys_slist_t *list)
{
    size_t count = 0;
    sys_snode_t *cur = list->head;
    while (cur != NULL) {
        count++;
        cur = cur->next;
    }
    return count;
}

/* Iteration macros */

#define SYS_SLIST_FOR_EACH_NODE(list, node) \
    for ((node) = sys_slist_peek_head(list); \
         (node) != NULL; \
         (node) = sys_slist_peek_next(node))

#define SYS_SLIST_FOR_EACH_NODE_SAFE(list, node, next_node) \
    for ((node) = sys_slist_peek_head(list), \
         (next_node) = (node) ? sys_slist_peek_next(node) : NULL; \
         (node) != NULL; \
         (node) = (next_node), \
         (next_node) = (node) ? sys_slist_peek_next(node) : NULL)

#define SYS_SLIST_CONTAINER(node, container, field) \
    ((node) ? CONTAINER_OF(node, __typeof__(*container), field) : NULL)

#define SYS_SLIST_PEEK_HEAD_CONTAINER(list, container, field) \
    SYS_SLIST_CONTAINER(sys_slist_peek_head(list), container, field)

#define SYS_SLIST_PEEK_NEXT_CONTAINER(container, field) \
    SYS_SLIST_CONTAINER(sys_slist_peek_next(&(container)->field), \
                        container, field)

#define SYS_SLIST_FOR_EACH_CONTAINER(list, container, field) \
    for ((container) = SYS_SLIST_PEEK_HEAD_CONTAINER(list, container, field); \
         (container) != NULL; \
         (container) = SYS_SLIST_PEEK_NEXT_CONTAINER(container, field))

#define SYS_SLIST_FOR_EACH_CONTAINER_SAFE(list, container, tmp, field) \
    for ((container) = SYS_SLIST_PEEK_HEAD_CONTAINER(list, container, field), \
         (tmp) = (container) ? \
                 SYS_SLIST_PEEK_NEXT_CONTAINER(container, field) : NULL; \
         (container) != NULL; \
         (container) = (tmp), \
         (tmp) = (container) ? \
                 SYS_SLIST_PEEK_NEXT_CONTAINER(container, field) : NULL)

#ifdef __cplusplus
}
#endif
