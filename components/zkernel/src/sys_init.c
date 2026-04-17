/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/init.h"

#include "esp_log.h"

static const char *TAG = "sys_init";

extern const struct sys_init_entry __start_sys_init_entries[];
extern const struct sys_init_entry __stop_sys_init_entries[];

/* Simple insertion sort by (level, priority) -- runs once at boot */
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
    size_t count = (size_t)(__stop_sys_init_entries - __start_sys_init_entries);

    if (count == 0) {
        ESP_LOGI(TAG, "No SYS_INIT entries registered");
        return 0;
    }

    if (count > CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES) {
        ESP_LOGE(TAG, "Too many init entries (%u > %d)",
                 (unsigned)count, CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES);
        count = CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES;
    }

    ESP_LOGI(TAG, "Running %u init entries", (unsigned)count);

    /* Build a local mutable pointer array so we can sort without touching
     * the const section data. */
    const struct sys_init_entry *sorted[CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES];
    for (size_t i = 0; i < count; i++) {
        sorted[i] = &__start_sys_init_entries[i];
    }
    sort_entries(sorted, count);

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
    }

    ESP_LOGI(TAG, "All init entries completed successfully");
    return 0;
}

void sys_shutdown_run_all(void)
{
    size_t count = (size_t)(__stop_sys_init_entries - __start_sys_init_entries);

    if (count == 0) {
        return;
    }

    if (count > CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES) {
        count = CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES;
    }

    const struct sys_init_entry *sorted[CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES];
    for (size_t i = 0; i < count; i++) {
        sorted[i] = &__start_sys_init_entries[i];
    }
    sort_entries(sorted, count);

    ESP_LOGI(TAG, "Running shutdown (%u entries)", (unsigned)count);
    for (int i = (int)count - 1; i >= 0; i--) {
        const struct sys_init_entry *entry = sorted[i];
        if (entry->shutdown_fn) {
            ESP_LOGI(TAG, "Shutdown: %s", entry->name);
            entry->shutdown_fn();
        }
    }
}
