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

/* ----------------------------------------------------------------
 * History
 * ---------------------------------------------------------------- */

struct shell_history_entry {
    sys_dnode_t node;
    char        line[CONFIG_ZSHELL_CMD_BUFF_SIZE];
    uint16_t    len;
    bool        in_use;
};

struct shell_history {
    sys_dlist_t list;
    sys_dnode_t *current;    /* navigation pointer for Up/Down */
    uint16_t    count;
    struct shell_history_entry entries[CONFIG_ZSHELL_HISTORY_DEPTH];
};

/* ----------------------------------------------------------------
 * Shell Context (mutable runtime state)
 * ---------------------------------------------------------------- */

struct shell_ctx {
    enum shell_state state;
    char     cmd_buff[CONFIG_ZSHELL_CMD_BUFF_SIZE];
    uint16_t cmd_buff_len;    /* chars in buffer */
    uint16_t cmd_buff_pos;    /* cursor position */
    char     printf_buff[CONFIG_ZSHELL_PRINTF_BUFF_SIZE];
    struct k_mutex wr_mtx;    /* thread-safe output */
    int      ret_val;         /* last command return value */
    bool     echo;            /* echo characters back */

    /* VT100 escape sequence decoder */
    uint8_t  esc_state;       /* 0=normal, 1=got ESC, 2=got CSI */
    char     esc_buff[8];
    uint8_t  esc_len;
};

/* ----------------------------------------------------------------
 * Shell Instance
 * ---------------------------------------------------------------- */

struct shell {
    const char                   *default_prompt;
    const char                   *name;
    const struct shell_transport *iface;
    struct shell_ctx              ctx;
    struct shell_history          history;
    struct k_thread               thread;
    struct k_event                rx_event;
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
#if defined(__APPLE__)
/* Mach-O fallback: see log.h. Whole-link safe -> constructor is fine. */
#define SHELL_CMD_REGISTER(_syntax, _subcmd, _help, _handler)          \
    static struct shell_static_entry _shell_cmd_##_syntax = {          \
        .syntax  = #_syntax,                                           \
        .help    = (_help),                                            \
        .subcmd  = (const union shell_cmd_entry *)(_subcmd),           \
        .handler = (_handler),                                         \
    };                                                                 \
    static void __attribute__((constructor))                           \
    _shell_reg_##_syntax(void) {                                       \
        shell_cmd_register(&_shell_cmd_##_syntax);                     \
    }
#else
#define SHELL_CMD_REGISTER(_syntax, _subcmd, _help, _handler)          \
    static struct shell_static_entry                                   \
        __attribute__((section(".shell_root_cmds"), used))             \
        _shell_cmd_##_syntax = {                                       \
            .syntax  = #_syntax,                                       \
            .help    = (_help),                                        \
            .subcmd  = (const union shell_cmd_entry *)(_subcmd),       \
            .handler = (_handler),                                     \
        }
#endif

/**
 * Create a static subcommand set (array of shell_static_entry).
 * Terminated by SHELL_SUBCMD_SET_END sentinel.
 */
#define SHELL_STATIC_SUBCMD_SET_CREATE(_name, ...)                     \
    static const struct shell_static_entry _name##_cmds[] = {          \
        __VA_ARGS__                                                    \
        { .syntax = NULL }                                             \
    };                                                                 \
    static const union shell_cmd_entry _name = {                       \
        .entry = _name##_cmds                                         \
    }

/**
 * Define one entry within a SHELL_STATIC_SUBCMD_SET_CREATE.
 */
#define SHELL_CMD(_syntax, _subcmd, _help, _handler)                   \
    {                                                                  \
        .syntax  = #_syntax,                                           \
        .help    = (_help),                                            \
        .subcmd  = (const union shell_cmd_entry *)(_subcmd),          \
        .handler = (_handler),                                         \
    },

/* ----------------------------------------------------------------
 * Shell API
 * ---------------------------------------------------------------- */

/**
 * Initialize and start the shell.
 * Typically called via SYS_INIT at APPLICATION level.
 */
int shell_init(struct shell *sh, const struct shell_transport *transport,
               const char *prompt);

/**
 * Thread-safe formatted print to the shell.
 * Acquires write mutex, formats output, writes via transport.
 */
void shell_print(const struct shell *sh, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

void shell_error(const struct shell *sh, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

void shell_warn(const struct shell *sh, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

void shell_info(const struct shell *sh, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * Write raw data to the shell transport (mutex-protected).
 */
void shell_write(const struct shell *sh, const void *data, size_t len);

/* ----------------------------------------------------------------
 * Internal API (used by shell subsystem files)
 * ---------------------------------------------------------------- */

/* Command registry */
void shell_cmd_sort_root(void);
void shell_builtins_register(void);
const struct shell_static_entry *shell_cmd_find(
    const struct shell_static_entry *cmds, size_t count,
    const char *name);

/* Parsing */
size_t shell_make_argv(char *line, char **argv, size_t max_argc);
int shell_execute(struct shell *sh);

/* History */
void shell_history_init(struct shell_history *hist);
void shell_history_add(struct shell_history *hist, const char *line, uint16_t len);
bool shell_history_get(struct shell_history *hist, bool up,
                       char *dst, uint16_t *len);
void shell_history_reset(struct shell_history *hist);

/* VT100 */
void shell_vt100_process(struct shell *sh, char ch);

/* Tab completion */
void shell_completion(struct shell *sh);

/* ----------------------------------------------------------------
 * UART Transport (provided by shell_uart.c)
 * ---------------------------------------------------------------- */

extern const struct shell_transport shell_transport_uart;

#ifdef __cplusplus
}
#endif
