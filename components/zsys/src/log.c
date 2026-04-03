/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zsys/log.h"

#include <string.h>

#include "esp_log.h"

#if defined(CONFIG_ZSYS_LOG_MODULE)

static const char *TAG = "zsys_log";

struct log_module_entry {
    const char     *name;
    esp_log_level_t current_level;
};

static struct log_module_entry modules[CONFIG_ZSYS_LOG_MAX_MODULES];
static int module_count = 0;

void zsys_log_register_module(const char *name, esp_log_level_t default_level)
{
    if (module_count >= CONFIG_ZSYS_LOG_MAX_MODULES) {
        ESP_LOGW(TAG, "Log module registry full, cannot register '%s'", name);
        return;
    }

    /* Check for duplicates */
    for (int i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, name) == 0) {
            return; /* Already registered */
        }
    }

    modules[module_count].name = name;
    modules[module_count].current_level = default_level;
    module_count++;
}

int zsys_log_set_level(const char *module_name, esp_log_level_t level)
{
    for (int i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, module_name) == 0) {
            modules[i].current_level = level;
            esp_log_level_set(module_name, level);
            ESP_LOGI(TAG, "Set '%s' log level to %d", module_name, level);
            return 0;
        }
    }
    ESP_LOGW(TAG, "Module '%s' not found", module_name);
    return -1;
}

static const char *level_to_str(esp_log_level_t level)
{
    switch (level) {
    case ESP_LOG_NONE:    return "NONE";
    case ESP_LOG_ERROR:   return "ERR";
    case ESP_LOG_WARN:    return "WRN";
    case ESP_LOG_INFO:    return "INF";
    case ESP_LOG_DEBUG:   return "DBG";
    case ESP_LOG_VERBOSE: return "VRB";
    default:              return "???";
    }
}

void zsys_log_list_modules(void)
{
    ESP_LOGI(TAG, "Registered log modules (%d):", module_count);
    ESP_LOGI(TAG, "  %-24s  %s", "Module", "Level");
    ESP_LOGI(TAG, "  %-24s  %s", "------------------------", "-----");
    for (int i = 0; i < module_count; i++) {
        ESP_LOGI(TAG, "  %-24s  %s",
                 modules[i].name,
                 level_to_str(modules[i].current_level));
    }
}

int zsys_log_get_module_count(void)
{
    return module_count;
}

int zsys_log_get_module_info(int index, const char **name, int *level)
{
    if (index < 0 || index >= module_count) {
        return -1;
    }
    *name = modules[index].name;
    *level = (int)modules[index].current_level;
    return 0;
}

#else /* !CONFIG_ZSYS_LOG_MODULE */

void zsys_log_register_module(const char *name, esp_log_level_t default_level)
{
    (void)name;
    (void)default_level;
}

int zsys_log_set_level(const char *module_name, esp_log_level_t level)
{
    esp_log_level_set(module_name, level);
    return 0;
}

void zsys_log_list_modules(void)
{
}

int zsys_log_get_module_count(void)
{
    return 0;
}

int zsys_log_get_module_info(int index, const char **name, int *level)
{
    (void)index; (void)name; (void)level;
    return -1;
}

#endif
