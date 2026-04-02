/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Subsystem watchdog -- detect hung subsystems.
 *
 * Usage:
 *   // Registration (at init)
 *   static struct zsys_watchdog_entry sip_wd;
 *   zsys_watchdog_register(&sip_wd, "sip_service", K_SECONDS(60));
 *
 *   // In subsystem main loop
 *   zsys_watchdog_feed(&sip_wd);
 *
 *   // Supervisor checks all feeds periodically
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "zephyr/sys/time_units.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*zsys_watchdog_timeout_cb_t)(const char *name);

struct zsys_watchdog_entry {
    const char *name;
    int64_t     timeout_ms;
    int64_t     last_feed_ms;
    bool        active;
};

/**
 * Initialize the watchdog subsystem.
 * Starts a supervisor task that checks all registered entries.
 *
 * @param check_interval  How often the supervisor checks feeds.
 * @param timeout_cb      Called when a subsystem misses its deadline.
 */
void zsys_watchdog_init(k_timeout_t check_interval,
                        zsys_watchdog_timeout_cb_t timeout_cb);

/**
 * Register a subsystem watchdog.
 */
int zsys_watchdog_register(struct zsys_watchdog_entry *entry,
                           const char *name, k_timeout_t timeout);

/**
 * Feed (reset) a subsystem's watchdog timer.
 */
void zsys_watchdog_feed(struct zsys_watchdog_entry *entry);

#ifdef __cplusplus
}
#endif
