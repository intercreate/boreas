/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Hierarchical tab completion.
 *
 * Zephyr reference: subsys/shell/shell.c (tab_handle, autocomplete)
 */

#include "zshell/shell.h"

#include <string.h>

/**
 * Perform tab completion at the current cursor position.
 *
 * Algorithm:
 * 1. Tokenize cmd_buff up to cursor into argv
 * 2. Walk command tree to find parent node for current depth
 * 3. Prefix-match partial token against parent's subcommand entries
 * 4. 0 matches -> beep
 * 5. 1 match -> insert remaining chars + space
 * 6. N matches -> print all, insert common prefix
 */
void shell_completion(struct shell *sh)
{
    char line_copy[CONFIG_ZSHELL_CMD_BUFF_SIZE];
    char *argv[CONFIG_ZSHELL_MAX_ARGC];

    /* Copy buffer up to cursor position */
    memcpy(line_copy, sh->ctx.cmd_buff, sh->ctx.cmd_buff_pos);
    line_copy[sh->ctx.cmd_buff_pos] = '\0';

    size_t argc = shell_make_argv(line_copy, argv, CONFIG_ZSHELL_MAX_ARGC);

    /* Determine if we're completing a partial token or starting a new one */
    bool completing_partial = (sh->ctx.cmd_buff_pos > 0 &&
                               sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos - 1] != ' ');
    const char *partial = completing_partial && argc > 0 ? argv[argc - 1] : "";
    size_t partial_len = strlen(partial);

    /* Walk the command tree to find the parent for completion */
    const struct shell_static_entry *candidates = NULL;
    size_t candidate_count = 0;
    size_t walk_depth = completing_partial ? argc - 1 : argc;

    if (walk_depth == 0) {
        /* Completing root commands */
        /* Use root commands as candidates -- they're pointers, not a contiguous array,
         * so we handle this case specially below */
        candidates = NULL; /* signal: use root commands */
        candidate_count = _shell_root_cmd_count;
    } else {
        /* Walk tree to find parent */
        const struct shell_static_entry *cmd = NULL;

        /* Find root command */
        for (size_t i = 0; i < _shell_root_cmd_count; i++) {
            if (strcmp(_shell_root_cmds[i]->syntax, argv[0]) == 0) {
                cmd = _shell_root_cmds[i];
                break;
            }
        }

        /* Walk subcommands */
        for (size_t d = 1; d < walk_depth && cmd != NULL; d++) {
            if (cmd->subcmd == NULL || cmd->subcmd->entry == NULL) {
                cmd = NULL;
                break;
            }
            const struct shell_static_entry *entries = cmd->subcmd->entry;
            const struct shell_static_entry *found = NULL;
            for (size_t i = 0; entries[i].syntax != NULL; i++) {
                if (strcmp(entries[i].syntax, argv[d]) == 0) {
                    found = &entries[i];
                    break;
                }
            }
            cmd = found;
        }

        if (cmd != NULL && cmd->subcmd != NULL && cmd->subcmd->entry != NULL) {
            candidates = cmd->subcmd->entry;
            /* Count entries */
            candidate_count = 0;
            while (candidates[candidate_count].syntax != NULL) {
                candidate_count++;
            }
        } else {
            /* No completions available */
            shell_write(sh, "\a", 1); /* BEL */
            return;
        }
    }

    /* Find matches */
    const struct shell_static_entry *matches[CONFIG_ZSHELL_MAX_ROOT_CMDS];
    size_t match_count = 0;

    for (size_t i = 0; i < candidate_count; i++) {
        const char *syntax;
        if (candidates == NULL) {
            syntax = _shell_root_cmds[i]->syntax;
        } else {
            syntax = candidates[i].syntax;
            if (syntax == NULL) break;
        }

        if (strncmp(syntax, partial, partial_len) == 0) {
            if (candidates == NULL) {
                matches[match_count++] = _shell_root_cmds[i];
            } else {
                matches[match_count++] = &candidates[i];
            }
            if (match_count >= CONFIG_ZSHELL_MAX_ROOT_CMDS) break;
        }
    }

    if (match_count == 0) {
        shell_write(sh, "\a", 1); /* BEL */
        return;
    }

    if (match_count == 1) {
        /* Single match: complete it */
        const char *syntax = matches[0]->syntax;
        size_t insert_len = strlen(syntax) - partial_len;
        const char *insert = syntax + partial_len;

        /* Insert remaining chars */
        if (sh->ctx.cmd_buff_len + insert_len + 1 < CONFIG_ZSHELL_CMD_BUFF_SIZE) {
            memcpy(&sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos], insert, insert_len);
            sh->ctx.cmd_buff_pos += insert_len;
            sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos] = ' ';
            sh->ctx.cmd_buff_pos++;
            sh->ctx.cmd_buff_len = sh->ctx.cmd_buff_pos;
            shell_write(sh, insert, insert_len);
            shell_write(sh, " ", 1);
        }
        return;
    }

    /* Multiple matches: find common prefix and display all */
    size_t common_len = strlen(matches[0]->syntax);
    for (size_t i = 1; i < match_count; i++) {
        size_t j = 0;
        while (j < common_len &&
               matches[0]->syntax[j] == matches[i]->syntax[j]) {
            j++;
        }
        common_len = j;
    }

    /* Insert common prefix beyond partial */
    if (common_len > partial_len) {
        size_t insert_len = common_len - partial_len;
        const char *insert = matches[0]->syntax + partial_len;
        if (sh->ctx.cmd_buff_len + insert_len < CONFIG_ZSHELL_CMD_BUFF_SIZE) {
            memcpy(&sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos], insert, insert_len);
            sh->ctx.cmd_buff_pos += insert_len;
            sh->ctx.cmd_buff_len = sh->ctx.cmd_buff_pos;
            shell_write(sh, insert, insert_len);
        }
    }

    /* Print all matches */
    shell_write(sh, "\r\n", 2);
    for (size_t i = 0; i < match_count; i++) {
        shell_print(sh, "  %s", matches[i]->syntax);
    }

    /* Reprint prompt + current line */
    const char *prompt = sh->default_prompt ? sh->default_prompt
                                            : CONFIG_ZSHELL_PROMPT;
    shell_write(sh, prompt, strlen(prompt));
    shell_write(sh, sh->ctx.cmd_buff, sh->ctx.cmd_buff_len);
}
