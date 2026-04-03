/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Built-in commands that depend on zsys: log, thread.
 * Registered explicitly via shell_builtins_zsys_register().
 */

#include "zshell/shell.h"

#include <string.h>

/* ----------------------------------------------------------------
 * log commands
 * ---------------------------------------------------------------- */

#ifdef CONFIG_ZSHELL_CMD_LOG

#include "zsys/log.h"

static int cmd_log_list(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    shell_print(sh, "Registered log modules:");
    zsys_log_list_modules();
    return 0;
}

static int cmd_log_level(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: log level <module> <0-4>");
        shell_print(sh, "  0=NONE 1=ERR 2=WRN 3=INF 4=DBG");
        return -1;
    }

    int level = argv[2][0] - '0';
    if (level < 0 || level > 4) {
        shell_error(sh, "Invalid level: %s (0-4)", argv[2]);
        return -1;
    }

    int ret = zsys_log_set_level(argv[1], level);
    if (ret != 0) {
        shell_error(sh, "Module '%s' not found", argv[1]);
        return -1;
    }

    shell_print(sh, "Set %s log level to %d", argv[1], level);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(log_subcmds,
    SHELL_CMD(list,  NULL, "List registered log modules", cmd_log_list)
    SHELL_CMD(level, NULL, "Set log level: log level <module> <0-4>", cmd_log_level)
);

static struct shell_static_entry _builtin_log = {
    .syntax = "log", .help = "Log commands",
    .subcmd = (const union shell_cmd_entry *)&log_subcmds,
    .handler = NULL,
};

#endif /* CONFIG_ZSHELL_CMD_LOG */

/* ----------------------------------------------------------------
 * thread command
 * ---------------------------------------------------------------- */

#ifdef CONFIG_ZSHELL_CMD_THREAD

#include "zsys/thread_analyzer.h"

static int cmd_thread(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    shell_print(sh, "Thread statistics:");
    zsys_thread_analyzer_print();
    return 0;
}

static struct shell_static_entry _builtin_thread = {
    .syntax = "thread", .help = "Print thread statistics",
    .subcmd = NULL, .handler = cmd_thread,
};

#endif /* CONFIG_ZSHELL_CMD_THREAD */

/* ----------------------------------------------------------------
 * Registration
 * ---------------------------------------------------------------- */

void shell_builtins_zsys_register(void)
{
#ifdef CONFIG_ZSHELL_CMD_LOG
    _shell_root_cmds[_shell_root_cmd_count++] = &_builtin_log;
#endif

#ifdef CONFIG_ZSHELL_CMD_THREAD
    _shell_root_cmds[_shell_root_cmd_count++] = &_builtin_thread;
#endif
}
