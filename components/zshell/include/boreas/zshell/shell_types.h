/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Shell type definitions and forward declarations.
 * Zephyr reference: include/zephyr/shell/shell.h
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
struct shell;

/* Forward declarations */
struct shell_static_entry;
union shell_cmd_entry;

/* Command handler -- matches Zephyr signature */
typedef int (*shell_cmd_handler)(const struct shell *sh,
                                 size_t argc, char **argv);

/* Dynamic command getter -- for runtime-generated commands */
typedef void (*shell_dynamic_get)(size_t idx,
                                  struct shell_static_entry *entry);

/* Static command entry -- one node in the command tree */
struct shell_static_entry {
    const char                    *syntax;   /* command name */
    const char                    *help;     /* one-line help text */
    const union shell_cmd_entry   *subcmd;   /* subcommand set, or NULL */
    shell_cmd_handler              handler;  /* handler, or NULL if group-only */
};

/* Union: static array vs dynamic getter */
union shell_cmd_entry {
    const struct shell_static_entry *entry;
    shell_dynamic_get                dynamic_get;
};

/* Sentinel for subcommand arrays */
#define SHELL_SUBCMD_SET_END { .syntax = NULL }

/* Shell state machine */
enum shell_state {
    SHELL_STATE_UNINITIALIZED = 0,
    SHELL_STATE_INITIALIZED,
    SHELL_STATE_ACTIVE,
};

/* Forward declare transport for API vtable */
struct shell_transport;

/* Transport API vtable */
struct shell_transport_api {
    int (*init)(const struct shell_transport *transport, void *context);
    int (*uninit)(const struct shell_transport *transport);
    int (*write)(const struct shell_transport *transport,
                 const void *data, size_t length, size_t *cnt);
    int (*read)(const struct shell_transport *transport,
                void *data, size_t length, size_t *cnt);
};

struct shell_transport {
    const struct shell_transport_api *api;
    void *ctx;
};

#ifdef __cplusplus
}
#endif
