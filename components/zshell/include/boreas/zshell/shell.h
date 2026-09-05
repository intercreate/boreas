/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas Shell: Zephyr-compatible interactive shell for ESP-IDF.
 *
 * Provides hierarchical command registration, tab completion,
 * history, and thread-safe output over a transport backend.
 *
 * Zephyr reference: include/zephyr/shell/shell.h, subsys/shell/
 */

#pragma once

/* sdkconfig.h must be visible before the CONFIG_IDF_TARGET_LINUX guard on the
 * SHELL_CMD_REGISTER macro below. */
#include "sdkconfig.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zephyr/kernel.h"
#include "zephyr/sys/dlist.h"
#include "zshell/shell_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * Kconfig defaults (overridden by sdkconfig)
 * ---------------------------------------------------------------- */

#ifndef CONFIG_ZSHELL_CMD_BUFF_SIZE
#define CONFIG_ZSHELL_CMD_BUFF_SIZE 128
#endif

#ifndef CONFIG_ZSHELL_PRINTF_BUFF_SIZE
#define CONFIG_ZSHELL_PRINTF_BUFF_SIZE 256
#endif

#ifndef CONFIG_ZSHELL_MAX_ROOT_CMDS
#define CONFIG_ZSHELL_MAX_ROOT_CMDS 32
#endif

#ifndef CONFIG_ZSHELL_MAX_ARGC
#define CONFIG_ZSHELL_MAX_ARGC 12
#endif

#ifndef CONFIG_ZSHELL_HISTORY_DEPTH
#define CONFIG_ZSHELL_HISTORY_DEPTH 10
#endif

#ifndef CONFIG_ZSHELL_PROMPT
#define CONFIG_ZSHELL_PROMPT "boreas> "
#endif

#ifndef CONFIG_ZSHELL_CMD_ROOT
#define CONFIG_ZSHELL_CMD_ROOT ""
#endif

/* ----------------------------------------------------------------
 * History
 * ---------------------------------------------------------------- */

struct shell_history_entry {
	sys_dnode_t node;
	char line[CONFIG_ZSHELL_CMD_BUFF_SIZE];
	uint16_t len;
	bool in_use;
};

struct shell_history {
	sys_dlist_t list;
	sys_dnode_t *current; /* navigation pointer for Up/Down */
	uint16_t count;
	struct shell_history_entry entries[CONFIG_ZSHELL_HISTORY_DEPTH];
};

/* ----------------------------------------------------------------
 * Shell Context (mutable runtime state)
 * ---------------------------------------------------------------- */

struct shell_ctx {
	enum shell_state state;
	char cmd_buff[CONFIG_ZSHELL_CMD_BUFF_SIZE];
	uint16_t cmd_buff_len; /* chars in buffer */
	uint16_t cmd_buff_pos; /* cursor position */
	char printf_buff[CONFIG_ZSHELL_PRINTF_BUFF_SIZE];
	struct k_mutex wr_mtx; /* thread-safe output */
	int ret_val;           /* last command return value */
	bool echo;             /* echo characters back */
	bool obscure;          /* echo '*' instead of the typed character */
#ifdef CONFIG_ZSHELL_PROMPT_CHANGE
	char prompt[CONFIG_ZSHELL_PROMPT_BUFF_SIZE];
#endif

	/* VT100 escape sequence decoder */
	uint8_t esc_state; /* 0=normal, 1=got ESC, 2=got CSI */
	char esc_buff[8];
	uint8_t esc_len;
};

/* ----------------------------------------------------------------
 * Shell Instance
 * ---------------------------------------------------------------- */

struct shell {
	const char *default_prompt;
	const char *name;
	const struct shell_transport *iface;
	struct shell_ctx ctx;
	struct shell_history history;
	struct k_thread thread;
	struct k_event rx_event;
};

/* ----------------------------------------------------------------
 * Command Registration Macros
 * ---------------------------------------------------------------- */

/* Runtime root command registry. Populated at init time:
 *   - ESP target: shell_init() walks the .shell_root_cmds linker section and
 *     calls shell_cmd_register() for each entry.
 *   - macOS host (Mach-O): SHELL_CMD_REGISTER emits a constructor that calls
 *     shell_cmd_register() directly -- the host test executable is
 *     whole-linked, so archive-stripping isn't a concern.
 */
extern struct shell_static_entry *_shell_root_cmds[];
extern size_t _shell_root_cmd_count;

void shell_cmd_register(struct shell_static_entry *entry);

/**
 * Register a root-level command.
 *
 * NOTE: SHELL_CMD_REGISTER() must live in a TU that has at least one other
 * externally-referenced symbol, or in main/. Linker scripts do not pull
 * archive members -- only unresolved-symbol references do. (Same constraint
 * as SYS_INIT / LOG_MODULE_REGISTER.)
 */
#if defined(CONFIG_IDF_TARGET_LINUX)
/* Mach-O fallback: see log.h. Whole-link safe -> constructor is fine. */
#define SHELL_CMD_REGISTER(_syntax, _subcmd, _help, _handler)                                      \
	static struct shell_static_entry _shell_cmd_##_syntax = {                                  \
		.syntax = #_syntax,                                                                \
		.help = (_help),                                                                   \
		.subcmd = (const union shell_cmd_entry *)(_subcmd),                                \
		.handler = (_handler),                                                             \
	};                                                                                         \
	static void __attribute__((constructor)) _shell_reg_##_syntax(void)                        \
	{                                                                                          \
		shell_cmd_register(&_shell_cmd_##_syntax);                                         \
	}
#else
#define SHELL_CMD_REGISTER(_syntax, _subcmd, _help, _handler)                                      \
	static struct shell_static_entry                                                           \
		__attribute__((section(".shell_root_cmds"), used)) _shell_cmd_##_syntax = {        \
			.syntax = #_syntax,                                                        \
			.help = (_help),                                                           \
			.subcmd = (const union shell_cmd_entry *)(_subcmd),                        \
			.handler = (_handler),                                                     \
	}
#endif

/**
 * Create a static subcommand set (array of shell_static_entry).
 * Terminated by SHELL_SUBCMD_SET_END sentinel.
 */
#define SHELL_STATIC_SUBCMD_SET_CREATE(_name, ...)                                                 \
	static const struct shell_static_entry _name##_cmds[] = {__VA_ARGS__{.syntax = NULL}};     \
	static const union shell_cmd_entry _name = {.entry = _name##_cmds}

/**
 * Define one entry within a SHELL_STATIC_SUBCMD_SET_CREATE.
 */
#define SHELL_CMD(_syntax, _subcmd, _help, _handler)                                               \
	{                                                                                          \
		.syntax = #_syntax,                                                                \
		.help = (_help),                                                                   \
		.subcmd = (const union shell_cmd_entry *)(_subcmd),                                \
		.handler = (_handler),                                                             \
	},

/* ----------------------------------------------------------------
 * Shell API
 * ---------------------------------------------------------------- */

/**
 * Initialize and start the shell.
 * Typically called via SYS_INIT at APPLICATION level.
 */
int shell_init(struct shell *sh, const struct shell_transport *transport, const char *prompt);

/**
 * Thread-safe formatted print to the shell.
 * Acquires write mutex, formats output, writes via transport.
 */
void shell_print(const struct shell *sh, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

void shell_error(const struct shell *sh, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

void shell_warn(const struct shell *sh, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

void shell_info(const struct shell *sh, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/**
 * Write raw data to the shell transport (mutex-protected).
 */
void shell_write(const struct shell *sh, const void *data, size_t len);

/**
 * Obscure typed characters (echo '*'), e.g. while reading a password.
 *
 * @return Previous obscure state (0 or 1), -EINVAL if @p sh is NULL.
 */
int shell_obscure_set(const struct shell *sh, bool obscure);

/**
 * Change the prompt at runtime. Requires CONFIG_ZSHELL_PROMPT_CHANGE.
 *
 * @param sh     Shell instance.
 * @param prompt New prompt; an empty string is allowed, as upstream.
 * @return 0 on success, -EINVAL if @p prompt is NULL or does not fit in
 *         CONFIG_ZSHELL_PROMPT_BUFF_SIZE, -EPERM if the option is disabled.
 *
 * @note Upstream trylocks the output mutex and returns -EBUSY; Boreas blocks
 *       on it instead, so -EBUSY is never returned.
 */
int shell_prompt_change(const struct shell *sh, const char *prompt);

/**
 * Restrict dispatch to one root command. Typed input is passed as arguments
 * beneath @p cmd, so sibling root commands (including help) are unreachable
 * and tab completion offers only its subcommands. Applies to every shell
 * instance, as upstream. NULL restores the full tree.
 *
 * @param cmd Root command name, or NULL.
 * @return 0 on success, -EINVAL if @p cmd names no root command.
 */
int shell_set_root_cmd(const char *cmd);

/**
 * Drop all history entries, e.g. after a login command consumed a password.
 *
 * @param sh Shell instance.
 *
 * @note Upstream's sample calls z_shell_history_purge(sh->history); Boreas
 *       embeds the history in struct shell rather than pointing at it, so
 *       this takes the shell. Call it from a command handler (the shell
 *       thread); the history is not locked. No-op without
 *       CONFIG_ZSHELL_HISTORY.
 */
void shell_history_purge(const struct shell *sh);

/* ----------------------------------------------------------------
 * Internal API (used by shell subsystem files)
 * ---------------------------------------------------------------- */

/* Command registry */
void shell_cmd_sort_root(void);
const struct shell_static_entry *shell_root_cmd_find(const char *name);
/* Selected root (shell_set_root_cmd), or NULL for the full tree */
const struct shell_static_entry *shell_root_cmd_get(void);
void shell_builtins_register(void);
const struct shell_static_entry *shell_cmd_find(const struct shell_static_entry *cmds, size_t count,
						const char *name);

/* Line editing */
void shell_process_char(struct shell *sh, char ch);
void shell_print_prompt(const struct shell *sh);
/* Echo len chars of cmd_buff starting at off (the one place the buffer reaches the terminal) */
void shell_write_cmd(const struct shell *sh, uint16_t off, uint16_t len);

/* Parsing */
size_t shell_make_argv(char *line, char **argv, size_t max_argc);
/* shell_make_argv() with the selected root command, if any, placed at argv[0].
 * argv must hold CONFIG_ZSHELL_MAX_ARGC + 1 entries. */
size_t shell_make_argv_root(char *line, char **argv);
int shell_execute(struct shell *sh);

/* History */
void shell_history_init(struct shell_history *hist);
void shell_history_add(struct shell_history *hist, const char *line, uint16_t len);
bool shell_history_get(struct shell_history *hist, bool up, char *dst, uint16_t *len);
void shell_history_reset(struct shell_history *hist);

/* VT100 */
void shell_vt100_process(struct shell *sh, char ch);

/* Tab completion */
void shell_completion(struct shell *sh);

/* ----------------------------------------------------------------
 * stdio transport (provided by shell_stdio.c)
 *
 * Rides ESP-IDF's VFS console layer, so it works over whatever
 * CONFIG_ESP_CONSOLE_* selects -- UART, USB-SERIAL-JTAG, USB-CDC.
 * ---------------------------------------------------------------- */

extern const struct shell_transport shell_transport_stdio;

#ifdef __cplusplus
}
#endif
