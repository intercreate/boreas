/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Constructor-based root command registry.
 * Same pattern as sys_init.c: constructors fill an array,
 * then shell_cmd_sort_root() sorts alphabetically at init time.
 *
 * Zephyr reference: iterable sections in shell.h
 */

#include "zshell/shell.h"

#include <string.h>

/* Global registry -- populated by __attribute__((constructor)) */
struct shell_static_entry *_shell_root_cmds[CONFIG_ZSHELL_MAX_ROOT_CMDS];
size_t _shell_root_cmd_count = 0;

/* Insertion sort by syntax name (alphabetical) -- runs once at init */
void shell_cmd_sort_root(void)
{
    for (size_t i = 1; i < _shell_root_cmd_count; i++) {
        struct shell_static_entry *key = _shell_root_cmds[i];
        int j = (int)i - 1;
        while (j >= 0 && strcmp(_shell_root_cmds[j]->syntax,
                                key->syntax) > 0) {
            _shell_root_cmds[j + 1] = _shell_root_cmds[j];
            j--;
        }
        _shell_root_cmds[j + 1] = key;
    }
}

/* Find a command by name in a static entry array (NULL-terminated) */
const struct shell_static_entry *shell_cmd_find(
    const struct shell_static_entry *cmds, size_t count,
    const char *name)
{
    for (size_t i = 0; i < count; i++) {
        if (cmds[i].syntax == NULL) {
            break;
        }
        if (strcmp(cmds[i].syntax, name) == 0) {
            return &cmds[i];
        }
    }
    return NULL;
}
