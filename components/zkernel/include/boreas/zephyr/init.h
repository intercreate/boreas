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
 *   EARLY       -- logging, NVS, hardware ID
 *   STORAGE     -- cert manager, persistent config
 *   DEVICE      -- buses, peripherals (board_init)
 *   NETWORK     -- WiFi, MQTT
 *   APPLICATION -- state machine, CLI, diagnostics
 *
 * Within a level, entries are sorted by priority (lower = earlier).
 * Shutdown runs in reverse order.
 */

#pragma once

/* sdkconfig.h must be visible before the CONFIG_IDF_TARGET_LINUX guard on the
 * section attribute below. */
#include "sdkconfig.h"

#include <stddef.h>
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
    SYS_INIT_LEVEL_NETWORK,
    SYS_INIT_LEVEL_APPLICATION,
    SYS_INIT_LEVEL_COUNT,
};

#if defined(CONFIG_IDF_TARGET_LINUX)
/* Mach-O (macOS host) doesn't accept plain section names. The linux-target
 * unit tests don't exercise SYS_INIT, so the attribute becomes a no-op here
 * and sys_init_run_all() treats the section as empty (see sys_init.c). */
#define _BOREAS_SYS_INIT_SECTION_ATTR  __attribute__((unused))
#else
#define _BOREAS_SYS_INIT_SECTION_ATTR  __attribute__((section(".sys_init_entries"), used))
#endif

typedef int (*sys_init_fn_t)(void);

struct sys_init_entry {
    sys_init_fn_t    init_fn;
    sys_init_fn_t    shutdown_fn; /* NULL if no shutdown needed */
    enum sys_init_level level;
    uint8_t          priority; /* 0-255, lower = earlier within level */
    const char      *name;
};

/**
 * Register an init function.
 *
 * @param _init_fn   Function returning 0 on success.
 * @param _level     One of: EARLY, STORAGE, DEVICE, NETWORK, APPLICATION
 * @param _prio      Priority within level (0-255, lower = earlier).
 *
 * NOTE: Place SYS_INIT() in a translation unit that has at least one other
 * externally-referenced symbol, or in `main/` (which is linked whole). Linker
 * scripts do not pull archive members -- only unresolved-symbol references do.
 * A TU whose only contribution is the SYS_INIT() struct will be stripped from
 * its component static library and the entry will silently fail to register.
 * This matches ESP-IDF's own constraint on `ESP_SYSTEM_INIT_FN`
 * (see `esp_system/include/esp_private/startup_internal.h`).
 */
#define SYS_INIT(_init_fn, _level, _prio)                                     \
    static const struct sys_init_entry                                        \
        _BOREAS_SYS_INIT_SECTION_ATTR                                         \
        _sys_init_entry_##_init_fn = {                                        \
            .init_fn     = (_init_fn),                                        \
            .shutdown_fn = NULL,                                              \
            .level       = SYS_INIT_LEVEL_##_level,                           \
            .priority    = (_prio),                                           \
            .name        = #_init_fn,                                         \
        }

/**
 * Register init + shutdown pair.
 */
#define SYS_INIT_WITH_SHUTDOWN(_init_fn, _shutdown_fn, _level, _prio)         \
    static const struct sys_init_entry                                        \
        _BOREAS_SYS_INIT_SECTION_ATTR                                         \
        _sys_init_entry_##_init_fn = {                                        \
            .init_fn     = (_init_fn),                                        \
            .shutdown_fn = (_shutdown_fn),                                    \
            .level       = SYS_INIT_LEVEL_##_level,                           \
            .priority    = (_prio),                                           \
            .name        = #_init_fn,                                         \
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
