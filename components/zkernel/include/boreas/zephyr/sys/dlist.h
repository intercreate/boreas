/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible intrusive doubly-linked list.
 *
 * Layout matches upstream Zephyr: sys_dlist_t and sys_dnode_t are the
 * same struct -- a list head IS a node, used as a circular sentinel.
 * For an empty list, head->next == head->prev == head.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "zephyr/sys/util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _dnode {
	struct _dnode *next;
	struct _dnode *prev;
};

typedef struct _dnode sys_dnode_t;
typedef struct _dnode sys_dlist_t;

/* Static initializer -- empty list points to itself. */
#define SYS_DLIST_STATIC_INIT(ptr_to_list) {.next = (ptr_to_list), .prev = (ptr_to_list)}

static inline void sys_dlist_init(sys_dlist_t *list)
{
	list->next = list;
	list->prev = list;
}

static inline bool sys_dlist_is_empty(const sys_dlist_t *list)
{
	return list->next == list;
}

static inline sys_dnode_t *sys_dlist_peek_head(const sys_dlist_t *list)
{
	return sys_dlist_is_empty(list) ? NULL : list->next;
}

static inline sys_dnode_t *sys_dlist_peek_tail(const sys_dlist_t *list)
{
	return sys_dlist_is_empty(list) ? NULL : list->prev;
}

static inline sys_dnode_t *sys_dlist_peek_next(const sys_dlist_t *list, const sys_dnode_t *node)
{
	return (node->next == list) ? NULL : node->next;
}

static inline sys_dnode_t *sys_dlist_peek_prev(const sys_dlist_t *list, const sys_dnode_t *node)
{
	return (node->prev == list) ? NULL : node->prev;
}

static inline void sys_dlist_append(sys_dlist_t *list, sys_dnode_t *node)
{
	sys_dnode_t *tail = list->prev;

	node->next = list;
	node->prev = tail;
	tail->next = node;
	list->prev = node;
}

static inline void sys_dlist_prepend(sys_dlist_t *list, sys_dnode_t *node)
{
	sys_dnode_t *head = list->next;

	node->next = head;
	node->prev = list;
	head->prev = node;
	list->next = node;
}

static inline void sys_dlist_insert(sys_dnode_t *successor, sys_dnode_t *node)
{
	sys_dnode_t *prev = successor->prev;

	node->prev = prev;
	node->next = successor;
	prev->next = node;
	successor->prev = node;
}

static inline void sys_dlist_remove(sys_dnode_t *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
	node->next = NULL;
	node->prev = NULL;
}

static inline sys_dnode_t *sys_dlist_get(sys_dlist_t *list)
{
	sys_dnode_t *node = sys_dlist_peek_head(list);
	if (node != NULL) {
		sys_dlist_remove(node);
	}
	return node;
}

static inline bool sys_dnode_is_linked(const sys_dnode_t *node)
{
	return node->next != NULL;
}

static inline size_t sys_dlist_len(const sys_dlist_t *list)
{
	size_t count = 0;
	const sys_dnode_t *cur = list->next;
	while (cur != list) {
		count++;
		cur = cur->next;
	}
	return count;
}

/* Iteration macros */

#define SYS_DLIST_FOR_EACH_NODE(list, node)                                                        \
	for ((node) = sys_dlist_peek_head(list); (node) != NULL;                                   \
	     (node) = sys_dlist_peek_next(list, node))

#define SYS_DLIST_FOR_EACH_NODE_SAFE(list, node, next_node)                                        \
	for ((node) = sys_dlist_peek_head(list),                                                   \
	    (next_node) = (node) ? sys_dlist_peek_next(list, node) : NULL;                         \
	     (node) != NULL;                                                                       \
	     (node) = (next_node), (next_node) = (node) ? sys_dlist_peek_next(list, node) : NULL)

#define SYS_DLIST_CONTAINER(node, container, field)                                                \
	((node) ? CONTAINER_OF(node, __typeof__(*container), field) : NULL)

#define SYS_DLIST_PEEK_HEAD_CONTAINER(list, container, field)                                      \
	SYS_DLIST_CONTAINER(sys_dlist_peek_head(list), container, field)

#define SYS_DLIST_PEEK_NEXT_CONTAINER(list, container, field)                                      \
	SYS_DLIST_CONTAINER(sys_dlist_peek_next(list, &(container)->field), container, field)

#define SYS_DLIST_FOR_EACH_CONTAINER(list, container, field)                                       \
	for ((container) = SYS_DLIST_PEEK_HEAD_CONTAINER(list, container, field);                  \
	     (container) != NULL;                                                                  \
	     (container) = SYS_DLIST_PEEK_NEXT_CONTAINER(list, container, field))

#define SYS_DLIST_FOR_EACH_CONTAINER_SAFE(list, container, tmp, field)                             \
	for ((container) = SYS_DLIST_PEEK_HEAD_CONTAINER(list, container, field),                  \
	    (tmp) = (container) ? SYS_DLIST_PEEK_NEXT_CONTAINER(list, container, field) : NULL;    \
	     (container) != NULL; (container) = (tmp),                                             \
	    (tmp) = (container) ? SYS_DLIST_PEEK_NEXT_CONTAINER(list, container, field) : NULL)

#ifdef __cplusplus
}
#endif
