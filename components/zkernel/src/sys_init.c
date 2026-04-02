/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/init.h"

#include "esp_log.h"

static const char *TAG = "sys_init";

/* Linker-defined section boundaries */
extern const struct sys_init_entry __start_sys_init[];
extern const struct sys_init_entry __stop_sys_init[];

/* Simple insertion sort by (level, priority) — runs once at boot */
static void sort_entries(const struct sys_init_entry **sorted, size_t count)
{
    for (size_t i = 1; i < count; i++) {
        const struct sys_init_entry *key = sorted[i];
        int j = (int)i - 1;
        while (j >= 0 &&
               (sorted[j]->level > key->level ||
                (sorted[j]->level == key->level &&
                 sorted[j]->priority > key->priority))) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
}

int sys_init_run_all(void)
{
    const struct sys_init_entry *start = __start_sys_init;
    const struct sys_init_entry *stop = __stop_sys_init;
    size_t count = (size_t)(stop - start);

    if (count == 0) {
        ESP_LOGI(TAG, "No SYS_INIT entries registered");
        return 0;
    }

    ESP_LOGI(TAG, "Running %u init entries", (unsigned)count);

    /* Build sorted pointer array */
    const struct sys_init_entry *sorted[CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES];
    if (count > CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES) {
        ESP_LOGE(TAG, "Too many init entries (%u > %d)",
                 (unsigned)count, CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES);
        count = CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES;
    }

    for (size_t i = 0; i < count; i++) {
        sorted[i] = &start[i];
    }
    sort_entries(sorted, count);

    /* Run in order */
    for (size_t i = 0; i < count; i++) {
        const struct sys_init_entry *entry = sorted[i];
        ESP_LOGI(TAG, "[%d/%d] %s (level=%d, prio=%d)",
                 (int)(i + 1), (int)count, entry->name,
                 entry->level, entry->priority);
        int ret = entry->init_fn();
        if (ret != 0) {
            ESP_LOGE(TAG, "Init failed: %s returned %d", entry->name, ret);
            return ret;
        }
        /* Cast away const — initialized flag is runtime state */
        ((struct sys_init_entry *)entry)->initialized = true;
    }

    ESP_LOGI(TAG, "All init entries completed successfully");
    return 0;
}

void sys_shutdown_run_all(void)
{
    const struct sys_init_entry *start = __start_sys_init;
    const struct sys_init_entry *stop = __stop_sys_init;
    size_t count = (size_t)(stop - start);

    if (count == 0) {
        return;
    }

    /* Build sorted pointer array (same as init) */
    const struct sys_init_entry *sorted[CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES];
    if (count > CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES) {
        count = CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES;
    }
    for (size_t i = 0; i < count; i++) {
        sorted[i] = &start[i];
    }
    sort_entries(sorted, count);

    /* Run shutdown in reverse order */
    ESP_LOGI(TAG, "Running shutdown (%u entries)", (unsigned)count);
    for (int i = (int)count - 1; i >= 0; i--) {
        const struct sys_init_entry *entry = sorted[i];
        if (entry->shutdown_fn && entry->initialized) {
            ESP_LOGI(TAG, "Shutdown: %s", entry->name);
            entry->shutdown_fn();
            ((struct sys_init_entry *)entry)->initialized = false;
        }
    }
}
