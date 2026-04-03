/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Command history: fixed-slot ring buffer using sys_dlist.
 *
 * Zephyr reference: subsys/shell/shell_history.c
 */

#include "zshell/shell.h"

#include <string.h>

void shell_history_init(struct shell_history *hist)
{
    sys_dlist_init(&hist->list);
    hist->current = NULL;
    hist->count = 0;
    for (int i = 0; i < CONFIG_ZSHELL_HISTORY_DEPTH; i++) {
        hist->entries[i].in_use = false;
        hist->entries[i].len = 0;
        memset(&hist->entries[i].node, 0, sizeof(sys_dnode_t));
    }
}

void shell_history_add(struct shell_history *hist, const char *line, uint16_t len)
{
    if (len == 0) {
        return;
    }

    /* Skip if duplicate of most recent entry */
    if (!sys_dlist_is_empty(&hist->list)) {
        struct shell_history_entry *tail =
            CONTAINER_OF(sys_dlist_peek_tail(&hist->list),
                         struct shell_history_entry, node);
        if (tail->len == len && memcmp(tail->line, line, len) == 0) {
            return;
        }
    }

    /* Find a free slot, or evict the oldest */
    struct shell_history_entry *entry = NULL;

    for (int i = 0; i < CONFIG_ZSHELL_HISTORY_DEPTH; i++) {
        if (!hist->entries[i].in_use) {
            entry = &hist->entries[i];
            break;
        }
    }

    if (entry == NULL) {
        /* Evict oldest (head of list) */
        sys_dnode_t *oldest = sys_dlist_get(&hist->list);
        entry = CONTAINER_OF(oldest, struct shell_history_entry, node);
        entry->in_use = false;
        hist->count--;
    }

    /* Copy line into entry */
    uint16_t copy_len = len < CONFIG_ZSHELL_CMD_BUFF_SIZE - 1
                        ? len : CONFIG_ZSHELL_CMD_BUFF_SIZE - 1;
    memcpy(entry->line, line, copy_len);
    entry->line[copy_len] = '\0';
    entry->len = copy_len;
    entry->in_use = true;

    /* Append to tail (newest) */
    sys_dlist_append(&hist->list, &entry->node);
    hist->count++;
}

bool shell_history_get(struct shell_history *hist, bool up,
                       char *dst, uint16_t *len)
{
    if (sys_dlist_is_empty(&hist->list)) {
        return false;
    }

    if (up) {
        /* Navigate toward older (head) */
        if (hist->current == NULL) {
            /* Start from newest (tail) */
            hist->current = sys_dlist_peek_tail(&hist->list);
        } else {
            sys_dnode_t *prev = sys_dlist_peek_prev(&hist->list,
                                                     hist->current);
            if (prev != NULL) {
                hist->current = prev;
            }
            /* If prev is NULL, stay at oldest */
        }
    } else {
        /* Navigate toward newer (tail) */
        if (hist->current == NULL) {
            return false;
        }
        sys_dnode_t *next = sys_dlist_peek_next(&hist->list, hist->current);
        if (next == NULL) {
            /* Past newest -- exit history mode */
            hist->current = NULL;
            *len = 0;
            return false;
        }
        hist->current = next;
    }

    if (hist->current != NULL) {
        struct shell_history_entry *entry =
            CONTAINER_OF(hist->current, struct shell_history_entry, node);
        memcpy(dst, entry->line, entry->len);
        dst[entry->len] = '\0';
        *len = entry->len;
        return true;
    }

    return false;
}

void shell_history_reset(struct shell_history *hist)
{
    hist->current = NULL;
}
