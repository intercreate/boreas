/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zsys/watchdog.h"

#include "zsys/log.h"
#include "zephyr/kernel.h"

#if defined(CONFIG_ZSYS_WATCHDOG)

LOG_MODULE_REGISTER(watchdog, LOG_LEVEL_INF);

static struct zsys_watchdog_entry *entries[CONFIG_ZSYS_WATCHDOG_MAX_ENTRIES];
static int entry_count = 0;
static zsys_watchdog_timeout_cb_t timeout_callback = NULL;

static struct k_timer supervisor_timer;

static void supervisor_check(struct k_timer *timer)
{
    (void)timer;
    int64_t now = k_uptime_get();

    for (int i = 0; i < entry_count; i++) {
        struct zsys_watchdog_entry *entry = entries[i];
        if (!entry->active) {
            continue;
        }
        int64_t elapsed = now - entry->last_feed_ms;
        if (elapsed > entry->timeout_ms) {
            LOG_ERR("Watchdog timeout: %s (elapsed=%lld ms, limit=%lld ms)",
                     entry->name, (long long)elapsed,
                     (long long)entry->timeout_ms);
            if (timeout_callback) {
                timeout_callback(entry->name);
            }
        }
    }
}

void zsys_watchdog_init(k_timeout_t check_interval,
                        zsys_watchdog_timeout_cb_t timeout_cb)
{
    timeout_callback = timeout_cb;
    k_timer_init(&supervisor_timer, supervisor_check, NULL);
    k_timer_start(&supervisor_timer, check_interval, check_interval);
    LOG_INF("Watchdog supervisor started");
}

int zsys_watchdog_register(struct zsys_watchdog_entry *entry,
                           const char *name, k_timeout_t timeout)
{
    if (entry_count >= CONFIG_ZSYS_WATCHDOG_MAX_ENTRIES) {
        LOG_ERR("Watchdog registry full");
        return -1;
    }

    entry->name = name;
    entry->timeout_ms = k_timeout_to_ms(timeout);
    entry->last_feed_ms = k_uptime_get();
    entry->active = true;

    entries[entry_count++] = entry;
    LOG_INF("Registered: %s (timeout=%lld ms)",
             name, (long long)entry->timeout_ms);
    return 0;
}

void zsys_watchdog_feed(struct zsys_watchdog_entry *entry)
{
    entry->last_feed_ms = k_uptime_get();
}

#else /* !CONFIG_ZSYS_WATCHDOG */

void zsys_watchdog_init(k_timeout_t check_interval,
                        zsys_watchdog_timeout_cb_t timeout_cb)
{
    (void)check_interval;
    (void)timeout_cb;
}

int zsys_watchdog_register(struct zsys_watchdog_entry *entry,
                           const char *name, k_timeout_t timeout)
{
    (void)entry;
    (void)name;
    (void)timeout;
    return 0;
}

void zsys_watchdog_feed(struct zsys_watchdog_entry *entry)
{
    (void)entry;
}

#endif
