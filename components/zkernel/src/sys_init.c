/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/init.h"

#include "esp_log.h"

static const char *TAG = "sys_init";

/* Global registry -- populated by constructors before main() */
struct sys_init_entry *_sys_init_entries[CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES];
size_t _sys_init_count = 0;

/* Simple insertion sort by (level, priority) -- runs once at boot */
static void sort_entries(struct sys_init_entry **sorted, size_t count)
{
    for (size_t i = 1; i < count; i++) {
        struct sys_init_entry *key = sorted[i];
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
    size_t count = _sys_init_count;

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

    /* Sort in place -- constructors register in arbitrary order */
    sort_entries(_sys_init_entries, count);

    /* Run in order */
    for (size_t i = 0; i < count; i++) {
        struct sys_init_entry *entry = _sys_init_entries[i];
        ESP_LOGI(TAG, "[%d/%d] %s (level=%d, prio=%d)",
                 (int)(i + 1), (int)count, entry->name,
                 entry->level, entry->priority);
        int ret = entry->init_fn();
        if (ret != 0) {
            ESP_LOGE(TAG, "Init failed: %s returned %d", entry->name, ret);
            return ret;
        }
        entry->initialized = true;
    }

    ESP_LOGI(TAG, "All init entries completed successfully");
    return 0;
}

void sys_shutdown_run_all(void)
{
    size_t count = _sys_init_count;

    if (count == 0) {
        return;
    }

    if (count > CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES) {
        count = CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES;
    }

    /* Entries were already sorted by sys_init_run_all -- run shutdown in reverse */
    ESP_LOGI(TAG, "Running shutdown (%u entries)", (unsigned)count);
    for (int i = (int)count - 1; i >= 0; i--) {
        struct sys_init_entry *entry = _sys_init_entries[i];
        if (entry->shutdown_fn && entry->initialized) {
            ESP_LOGI(TAG, "Shutdown: %s", entry->name);
            entry->shutdown_fn();
            entry->initialized = false;
        }
    }
}
