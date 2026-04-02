/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Ordered initialization framework.
 *
 * Usage:
 *   static int my_subsystem_init(void) { ... return 0; }
 *   SYS_INIT(my_subsystem_init, DEVICE, 10);
 *
 * Init levels (executed in order):
 *   EARLY       — logging, NVS, hardware ID
 *   STORAGE     — cert manager, persistent config
 *   DEVICE      — buses, peripherals (board_init)
 *   AUDIO       — mixer, codec, tone player
 *   NETWORK     — WiFi, MQTT, SIP
 *   APPLICATION — state machine, CLI, diagnostics
 *
 * Within a level, entries are sorted by priority (lower = earlier).
 * Shutdown runs in reverse order.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Init levels */
enum sys_init_level {
    SYS_INIT_LEVEL_EARLY = 0,
    SYS_INIT_LEVEL_STORAGE,
    SYS_INIT_LEVEL_DEVICE,
    SYS_INIT_LEVEL_AUDIO,
    SYS_INIT_LEVEL_NETWORK,
    SYS_INIT_LEVEL_APPLICATION,
    SYS_INIT_LEVEL_COUNT,
};

typedef int (*sys_init_fn_t)(void);

struct sys_init_entry {
    sys_init_fn_t    init_fn;
    sys_init_fn_t    shutdown_fn; /* NULL if no shutdown needed */
    enum sys_init_level level;
    uint8_t          priority; /* 0-255, lower = earlier within level */
    const char      *name;
    bool             initialized;
};

/**
 * Register an init function.
 *
 * @param _init_fn   Function returning 0 on success.
 * @param _level     One of: EARLY, STORAGE, DEVICE, AUDIO, NETWORK, APPLICATION
 * @param _prio      Priority within level (0-255, lower = earlier).
 */
#define SYS_INIT(_init_fn, _level, _prio) \
    static const struct sys_init_entry \
        __attribute__((used, section(".sys_init"))) \
        _sys_init_##_init_fn = { \
            .init_fn = (_init_fn), \
            .shutdown_fn = NULL, \
            .level = SYS_INIT_LEVEL_##_level, \
            .priority = (_prio), \
            .name = #_init_fn, \
            .initialized = false, \
        }

/**
 * Register init + shutdown pair.
 */
#define SYS_INIT_WITH_SHUTDOWN(_init_fn, _shutdown_fn, _level, _prio) \
    static const struct sys_init_entry \
        __attribute__((used, section(".sys_init"))) \
        _sys_init_##_init_fn = { \
            .init_fn = (_init_fn), \
            .shutdown_fn = (_shutdown_fn), \
            .level = SYS_INIT_LEVEL_##_level, \
            .priority = (_prio), \
            .name = #_init_fn, \
            .initialized = false, \
        }

/**
 * Run all registered init functions in order.
 * Returns 0 if all succeeded, or the first non-zero return code.
 */
int sys_init_run_all(void);

/**
 * Run all registered shutdown functions in reverse order.
 */
void sys_shutdown_run_all(void);

#ifdef __cplusplus
}
#endif
