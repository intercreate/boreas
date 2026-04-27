/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Built-in shell commands for device inspection.
 * Registered explicitly via shell_builtins_device_register().
 */

#include "zshell/shell.h"
#include "device_model.h"

#include <string.h>

/* ----------------------------------------------------------------
 * device list
 * ---------------------------------------------------------------- */

static int cmd_device_list(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;
	size_t count = device_get_count();

	shell_print(sh, "Registered devices (%u):", (unsigned)count);
	shell_print(sh, "  %-24s  %-6s  %s", "Name", "Ready", "Bus");
	shell_print(sh, "  %-24s  %-6s  %s", "------------------------", "------", "---");

	for (size_t i = 0; i < count; i++) {
		const struct device *dev = device_get_by_index(i);
		const char *bus_name = (dev->bus != NULL) ? dev->bus->name : "-";
		shell_print(sh, "  %-24s  %-6s  %s", dev->name, device_is_ready(dev) ? "yes" : "no",
			    bus_name);
	}
	return 0;
}

/* ----------------------------------------------------------------
 * device info <name>
 * ---------------------------------------------------------------- */

static int cmd_device_info(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: device info <name>");
		return -1;
	}

	const struct device *dev = device_get_binding(argv[1]);
	if (dev == NULL) {
		shell_error(sh, "Device '%s' not found", argv[1]);
		return -1;
	}

	shell_print(sh, "Name:    %s", dev->name);
	shell_print(sh, "Ready:   %s", device_is_ready(dev) ? "yes" : "no");
	shell_print(sh, "Bus:     %s", dev->bus ? dev->bus->name : "(none)");
	shell_print(sh, "API:     %s", dev->api ? "yes" : "no");
	shell_print(sh, "Init:    %s", dev->init ? "yes" : "no");

	return 0;
}

/* ----------------------------------------------------------------
 * Subcommand set + registration
 * ---------------------------------------------------------------- */

SHELL_STATIC_SUBCMD_SET_CREATE(device_subcmds,
			       SHELL_CMD(list, NULL, "List registered devices", cmd_device_list)
				       SHELL_CMD(info, NULL,
						 "Show device details: device info <name>",
						 cmd_device_info));

static struct shell_static_entry _builtin_device = {
	.syntax = "device",
	.help = "Device commands",
	.subcmd = (const union shell_cmd_entry *)&device_subcmds,
	.handler = NULL,
};

void shell_builtins_device_register(void)
{
	shell_cmd_register(&_builtin_device);
}
