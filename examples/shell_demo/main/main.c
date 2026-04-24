/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Shell demo: demonstrates the Boreas shell with built-in commands
 * and a custom application command.
 */

#include <boreas/zephyr/kernel.h>
#include <boreas/zshell/shell.h>
#include <boreas/zsys/log.h>

LOG_MODULE_REGISTER(shell_demo, LOG_LEVEL_INF);

/* ----------------------------------------------------------------
 * Custom application command: "app"
 * Demonstrates hierarchical command registration.
 * ---------------------------------------------------------------- */

static int cmd_app_status(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    shell_print(sh, "Application status: running");
    shell_print(sh, "Uptime: %lld ms", k_uptime_get());
    return 0;
}

static int cmd_app_version(const struct shell *sh, size_t argc, char **argv)
{
    (void)argc; (void)argv;
    shell_print(sh, "Boreas Shell Demo v1.0.0");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(app_subcmds,
    SHELL_CMD(status,  NULL, "Show application status", cmd_app_status)
    SHELL_CMD(version, NULL, "Show version info",       cmd_app_version)
);

SHELL_CMD_REGISTER(app, &app_subcmds, "Application commands", NULL);

/* ----------------------------------------------------------------
 * Shell instance
 * ---------------------------------------------------------------- */

static struct shell demo_shell = {
    .name = "demo",
};

void app_main(void)
{
    LOG_INF("=== Boreas Shell Demo ===");

    /* Start the shell on the VFS console (UART by default, USB-Serial-JTAG
     * or USB-CDC if CONFIG_ESP_CONSOLE_* selects them). */
    shell_init(&demo_shell, &shell_transport_stdio, "boreas> ");

    /* Main task can do other work or just idle */
    LOG_INF("Shell started. Type 'help' for available commands.");

    /* Keep main task alive */
    while (1) {
        k_msleep(1000);
    }
}
