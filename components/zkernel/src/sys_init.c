/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/init.h"

#include "esp_log.h"

static const char *TAG = "sys_init";

#if defined(CONFIG_IDF_TARGET_LINUX)
/* Mach-O host stub -- see zephyr/init.h. The macOS unit-test build does not
 * populate .sys_init_entries; expose an empty pair so count = 0. */
static const struct sys_init_entry _sys_init_empty[0];
#define _sys_init_entries_start (_sys_init_empty)
#define _sys_init_entries_end   (_sys_init_empty)
#else
/* Boundary symbols emitted by ldgen SURROUND(sys_init_entries) in zkernel.lf. */
extern const struct sys_init_entry _sys_init_entries_start[];
extern const struct sys_init_entry _sys_init_entries_end[];
#endif

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
    size_t count = (size_t)(_sys_init_entries_end - _sys_init_entries_start);

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
        sorted[i] = &_sys_init_entries_start[i];
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
    size_t count = (size_t)(_sys_init_entries_end - _sys_init_entries_start);

    if (count == 0) {
        return;
    }

    if (count > CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES) {
        count = CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES;
    }

    const struct sys_init_entry *sorted[CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES];
    for (size_t i = 0; i < count; i++) {
        sorted[i] = &_sys_init_entries_start[i];
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
