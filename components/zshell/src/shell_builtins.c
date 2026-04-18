/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Built-in shell commands: help, clear, echo, kernel.
 *
 * These are registered explicitly from shell_builtins_register() rather
 * than via constructors, because constructors in static libraries get
 * stripped by the linker if no symbol from the object file is referenced.
 *
 * Zephyr reference: subsys/shell/shell_cmds.c
 */

#include "zshell/shell.h"

#include <string.h>

/* ----------------------------------------------------------------
 * help
 * ---------------------------------------------------------------- */

static int cmd_help(const struct shell *sh, size_t argc, char **argv)
{
    if (argc > 1) {
        /* Help for a specific command */
        for (size_t i = 0; i < _shell_root_cmd_count; i++) {
            if (strcmp(_shell_root_cmds[i]->syntax, argv[1]) == 0) {
                const struct shell_static_entry *cmd = _shell_root_cmds[i];
                shell_print(sh, "%s - %s", cmd->syntax,
                            cmd->help ? cmd->help : "");
                if (cmd->subcmd != NULL && cmd->subcmd->entry != NULL) {
                    shell_print(sh, "Subcommands:");
                    const struct shell_static_entry *entries =
                        cmd->subcmd->entry;
                    for (size_t j = 0; entries[j].syntax != NULL; j++) {
                        shell_print(sh, "  %-16s %s", entries[j].syntax,
                                    entries[j].help ? entries[j].help : "");
                    }
                }
                return 0;
            }
        }
        shell_error(sh, "Unknown command: %s", argv[1]);
        return -1;
    }

    /* List all root commands */
    shell_print(sh, "Available commands:");
    for (size_t i = 0; i < _shell_root_cmd_count; i++) {
        shell_print(sh, "  %-16s %s", _shell_root_cmds[i]->syntax,
                    _shell_root_cmds[i]->help ? _shell_root_cmds[i]->help : "");
    }
    return 0;
}

static struct shell_static_entry _builtin_help = {
    .syntax = "help", .help = "List commands or show help for a command",
    .subcmd = NULL, .handler = cmd_help,
};

/* ----------------------------------------------------------------
 * clear
 * ---------------------------------------------------------------- */

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    shell_write(sh, "\033[2J\033[H", 7);
    return 0;
}

static struct shell_static_entry _builtin_clear = {
    .syntax = "clear", .help = "Clear screen",
    .subcmd = NULL, .handler = cmd_clear,
};

/* ----------------------------------------------------------------
 * echo
 * ---------------------------------------------------------------- */

static int cmd_echo(const struct shell *sh, size_t argc, char **argv)
{
    struct shell *msh = (struct shell *)sh;

    if (argc > 1) {
        if (strcmp(argv[1], "on") == 0) {
            msh->ctx.echo = true;
            shell_print(sh, "Echo enabled");
        } else if (strcmp(argv[1], "off") == 0) {
            msh->ctx.echo = false;
            shell_print(sh, "Echo disabled");
        } else {
            shell_error(sh, "Usage: echo [on|off]");
            return -1;
        }
    } else {
        shell_print(sh, "Echo is %s", msh->ctx.echo ? "on" : "off");
    }
    return 0;
}

static struct shell_static_entry _builtin_echo = {
    .syntax = "echo", .help = "Toggle echo: echo [on|off]",
    .subcmd = NULL, .handler = cmd_echo,
};

/* ----------------------------------------------------------------
 * kernel (uptime, heap)
 * ---------------------------------------------------------------- */

#ifdef CONFIG_ZSHELL_CMD_KERNEL

#include "esp_system.h"

static int cmd_kernel_uptime(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    int64_t uptime = k_uptime_get();
    int64_t sec = uptime / 1000;
    int64_t ms = uptime % 1000;
    shell_print(sh, "Uptime: %lld.%03lld s", sec, ms);
    return 0;
}

static int cmd_kernel_heap(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    shell_print(sh, "Free heap:    %lu bytes",
                (unsigned long)esp_get_free_heap_size());
    shell_print(sh, "Min free:     %lu bytes",
                (unsigned long)esp_get_minimum_free_heap_size());
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(kernel_subcmds,
    SHELL_CMD(uptime, NULL, "Print system uptime", cmd_kernel_uptime)
    SHELL_CMD(heap,   NULL, "Print heap statistics", cmd_kernel_heap)
);

static struct shell_static_entry _builtin_kernel = {
    .syntax = "kernel", .help = "Kernel commands",
    .subcmd = (const union shell_cmd_entry *)&kernel_subcmds,
    .handler = NULL,
};

#endif /* CONFIG_ZSHELL_CMD_KERNEL */

/* ----------------------------------------------------------------
 * zsys builtins (log, thread) -- defined in shell_builtins_zsys.c
 * ---------------------------------------------------------------- */

extern void shell_builtins_zsys_register(void);

/* ----------------------------------------------------------------
 * device builtins -- defined in shell_builtins_device.c
 * ---------------------------------------------------------------- */

#ifdef CONFIG_ZSHELL_CMD_DEVICE
extern void shell_builtins_device_register(void);
#endif

/* ----------------------------------------------------------------
 * Registration function -- called from shell_init()
 * ---------------------------------------------------------------- */

void shell_builtins_register(void)
{
    shell_cmd_register(&_builtin_help);
    shell_cmd_register(&_builtin_clear);
    shell_cmd_register(&_builtin_echo);

#ifdef CONFIG_ZSHELL_CMD_KERNEL
    shell_cmd_register(&_builtin_kernel);
#endif

#if defined(CONFIG_ZSHELL_CMD_LOG) || defined(CONFIG_ZSHELL_CMD_THREAD)
    shell_builtins_zsys_register();
#endif

#ifdef CONFIG_ZSHELL_CMD_DEVICE
    shell_builtins_device_register();
#endif
}
