/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Command line parsing: tokenize into argv, walk command tree, dispatch.
 *
 * Zephyr reference: subsys/shell/shell_utils.c (z_shell_make_argv, z_shell_find_cmd)
 */

#include "zshell/shell.h"

#include <string.h>

/**
 * Split a command line into argv tokens.
 * Handles double-quoted strings. Modifies line in-place.
 * Returns argc.
 */
size_t shell_make_argv(char *line, char **argv, size_t max_argc)
{
    size_t argc = 0;
    char *p = line;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    while (*p != '\0' && argc < max_argc) {
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p != '\0' && *p != '"') {
                p++;
            }
            if (*p == '"') {
                *p++ = '\0';
            }
        } else {
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                p++;
            }
            if (*p != '\0') {
                *p++ = '\0';
            }
        }

        /* Skip whitespace between tokens */
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    }

    return argc;
}

/**
 * Find a root command by name.
 */
static const struct shell_static_entry *find_root_cmd(const char *name)
{
    for (size_t i = 0; i < _shell_root_cmd_count; i++) {
        if (strcmp(_shell_root_cmds[i]->syntax, name) == 0) {
            return _shell_root_cmds[i];
        }
    }
    return NULL;
}

/**
 * Find a subcommand by name within a subcmd set.
 */
static const struct shell_static_entry *find_subcmd(
    const union shell_cmd_entry *subcmd, const char *name)
{
    if (subcmd == NULL || subcmd->entry == NULL) {
        return NULL;
    }

    const struct shell_static_entry *entries = subcmd->entry;
    for (size_t i = 0; entries[i].syntax != NULL; i++) {
        if (strcmp(entries[i].syntax, name) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

/**
 * Execute the command in the shell's cmd_buff.
 * Returns 0 on success, -1 on error (command not found, etc.)
 */
int shell_execute(struct shell *sh)
{
    char *argv[CONFIG_ZSHELL_MAX_ARGC];
    char line_copy[CONFIG_ZSHELL_CMD_BUFF_SIZE];

    /* Copy buffer since make_argv modifies it */
    memcpy(line_copy, sh->ctx.cmd_buff, sh->ctx.cmd_buff_len);
    line_copy[sh->ctx.cmd_buff_len] = '\0';

    size_t argc = shell_make_argv(line_copy, argv, CONFIG_ZSHELL_MAX_ARGC);
    if (argc == 0) {
        return 0; /* empty line */
    }

    /* Walk the command tree */
    const struct shell_static_entry *cmd = find_root_cmd(argv[0]);
    if (cmd == NULL) {
        shell_error(sh, "Unknown command: %s", argv[0]);
        return -1;
    }

    /* Walk subcommands as deep as we can */
    size_t cmd_depth = 1;
    const struct shell_static_entry *handler_cmd = cmd;

    while (cmd_depth < argc && cmd->subcmd != NULL) {
        const struct shell_static_entry *sub =
            find_subcmd(cmd->subcmd, argv[cmd_depth]);
        if (sub == NULL) {
            break; /* remaining args are handler arguments */
        }
        cmd = sub;
        if (cmd->handler != NULL) {
            handler_cmd = cmd;
        }
        cmd_depth++;
    }

    /* Dispatch */
    if (handler_cmd->handler != NULL) {
        /* Pass remaining args after the matched command path */
        size_t handler_argc = argc - cmd_depth + 1;
        char **handler_argv = &argv[cmd_depth - 1];
        handler_argv[0] = (char *)handler_cmd->syntax;

        sh->ctx.ret_val = handler_cmd->handler(sh, handler_argc, handler_argv);
        return sh->ctx.ret_val;
    }

    /* No handler found -- show available subcommands */
    if (cmd->subcmd != NULL && cmd->subcmd->entry != NULL) {
        shell_error(sh, "Subcommand required. Available:");
        const struct shell_static_entry *entries = cmd->subcmd->entry;
        for (size_t i = 0; entries[i].syntax != NULL; i++) {
            shell_print(sh, "  %-16s %s", entries[i].syntax,
                        entries[i].help ? entries[i].help : "");
        }
    } else {
        shell_error(sh, "Unknown command: %s", argv[0]);
    }

    return -1;
}
