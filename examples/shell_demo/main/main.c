/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Shell demo: demonstrates the Boreas shell with built-in commands,
 * a custom application command, and an optional login/logout gate.
 */

#include <errno.h>
#include <string.h>

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
	(void)argc;
	(void)argv;
	shell_print(sh, "Application status: running");
	shell_print(sh, "Uptime: %lld ms", k_uptime_get());
	return 0;
}

static int cmd_app_version(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;
	shell_print(sh, "Boreas Shell Demo v1.0.0");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(app_subcmds,
			       SHELL_CMD(status, NULL, "Show application status", cmd_app_status)
				       SHELL_CMD(version, NULL, "Show version info",
						 cmd_app_version));

SHELL_CMD_REGISTER(app, &app_subcmds, "Application commands", NULL);

/* ----------------------------------------------------------------
 * login / logout
 *
 * Mirrors zephyr/samples/subsys/shell/shell_module/src/login_cmd.c. The
 * shell only supplies the primitives (obscured echo, root command
 * selection, prompt change, history purge); the password check is the
 * application's. Build with -DLOGIN=y to start locked (sdkconfig.login).
 * ---------------------------------------------------------------- */

#define DEFAULT_PASSWORD "boreas"
#define PROMPT_LOGIN     "login: "

static int cmd_login(const struct shell *sh, size_t argc, char **argv)
{
	static uint32_t attempts;

	/* example only -- not recommended for production use */
	if (argc < 2 || strcmp(argv[1], DEFAULT_PASSWORD) != 0) {
		shell_error(sh, "Incorrect password!");
		attempts++;
		if (attempts > 3) {
			k_sleep(K_SECONDS(attempts));
		}
		return -EINVAL;
	}

	/* clear history so password not visible there */
	shell_history_purge(sh);
	shell_obscure_set(sh, false);
	shell_set_root_cmd(NULL);
	shell_prompt_change(sh, CONFIG_ZSHELL_PROMPT);
	shell_print(sh, "Logged in. Type 'help' for available commands.");
	attempts = 0;
	return 0;
}

static int cmd_logout(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;
	shell_set_root_cmd("login");
	shell_obscure_set(sh, true);
	shell_prompt_change(sh, PROMPT_LOGIN);
	return 0;
}

SHELL_CMD_REGISTER(login, NULL, "<password>", cmd_login);
SHELL_CMD_REGISTER(logout, NULL, "Log out", cmd_logout);

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
	shell_init(&demo_shell, &shell_transport_stdio,
		   CONFIG_ZSHELL_CMD_ROOT[0] != '\0' ? PROMPT_LOGIN : CONFIG_ZSHELL_PROMPT);

	/* Main task can do other work or just idle */
	if (CONFIG_ZSHELL_CMD_ROOT[0] != '\0') {
		LOG_INF("Shell started locked. Hint: password = %s", DEFAULT_PASSWORD);
	} else {
		LOG_INF("Shell started. Type 'help' for available commands.");
	}

	/* Keep main task alive */
	while (1) {
		k_msleep(1000);
	}
}
