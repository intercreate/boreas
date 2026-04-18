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

static const char *level_str(int level)
{
    switch (level) {
    case 0: return "NONE";
    case 1: return "ERR";
    case 2: return "WRN";
    case 3: return "INF";
    case 4: return "DBG";
    case 5: return "VRB";
    default: return "???";
    }
}

static int cmd_log_list(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    int count = zsys_log_get_module_count();
    shell_print(sh, "Registered log modules (%d):", count);
    shell_print(sh, "  %-24s  %s", "Module", "Level");
    shell_print(sh, "  %-24s  %s", "------------------------", "-----");
    for (int i = 0; i < count; i++) {
        const char *name;
        int level;
        if (zsys_log_get_module_info(i, &name, &level) == 0) {
            shell_print(sh, "  %-24s  %s", name, level_str(level));
        }
    }
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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *state_str(eTaskState state)
{
    switch (state) {
    case eRunning:   return "Running";
    case eReady:     return "Ready";
    case eBlocked:   return "Blocked";
    case eSuspended: return "Suspend";
    case eDeleted:   return "Deleted";
    default:         return "Unknown";
    }
}

static int cmd_thread(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;

    UBaseType_t count = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = pvPortMalloc(count * sizeof(TaskStatus_t));
    if (tasks == NULL) {
        shell_error(sh, "Failed to allocate task status array");
        return -1;
    }

    uint32_t total_runtime;
    UBaseType_t actual = uxTaskGetSystemState(tasks, count, &total_runtime);

    shell_print(sh, "%-20s %6s %6s  %-8s %4s",
                "Name", "Stack", "HWM", "State", "Prio");
    shell_print(sh, "%-20s %6s %6s  %-8s %4s",
                "--------------------", "------", "------",
                "--------", "----");

    for (UBaseType_t i = 0; i < actual; i++) {
        TaskStatus_t *t = &tasks[i];
        uint32_t hwm_bytes = t->usStackHighWaterMark * sizeof(StackType_t);
        shell_print(sh, "%-20s %6s %6lu  %-8s %4lu",
                    t->pcTaskName,
                    "?",
                    (unsigned long)hwm_bytes,
                    state_str(t->eCurrentState),
                    (unsigned long)t->uxCurrentPriority);
    }

    vPortFree(tasks);
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
    shell_cmd_register(&_builtin_log);
#endif

#ifdef CONFIG_ZSHELL_CMD_THREAD
    shell_cmd_register(&_builtin_thread);
#endif
}
