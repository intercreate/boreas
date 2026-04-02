/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Per-module runtime log level control.
 *
 * Usage:
 *   // In source file
 *   LOG_MODULE_REGISTER(sip_service, LOG_LEVEL_INF);
 *
 *   // At runtime (e.g., from CLI)
 *   zsys_log_set_level("sip_service", ESP_LOG_DEBUG);
 *   zsys_log_list_modules();
 */

#pragma once

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Log levels -- alias Zephyr names to ESP-IDF levels */
#define LOG_LEVEL_NONE ESP_LOG_NONE
#define LOG_LEVEL_ERR  ESP_LOG_ERROR
#define LOG_LEVEL_WRN  ESP_LOG_WARN
#define LOG_LEVEL_INF  ESP_LOG_INFO
#define LOG_LEVEL_DBG  ESP_LOG_DEBUG

/**
 * Register a log module with a default level.
 * Sets the ESP-IDF log level for this module's TAG at startup.
 */
#if defined(CONFIG_ZSYS_LOG_MODULE)

#define LOG_MODULE_REGISTER(module_name, default_level)             \
    static const char *TAG = #module_name;                          \
    static void __attribute__((constructor))                        \
    _log_module_register_##module_name(void)                        \
    {                                                               \
        esp_log_level_set(#module_name, (default_level));           \
        zsys_log_register_module(#module_name, (default_level));    \
    }

#else

#define LOG_MODULE_REGISTER(module_name, default_level) \
    static const char *TAG = #module_name

#endif

/**
 * Register a module (called from LOG_MODULE_REGISTER constructor).
 */
void zsys_log_register_module(const char *name, esp_log_level_t default_level);

/**
 * Set the runtime log level for a module by name.
 * Returns 0 on success, -1 if module not found.
 */
int zsys_log_set_level(const char *module_name, esp_log_level_t level);

/**
 * Print all registered modules and their current levels.
 */
void zsys_log_list_modules(void);

#ifdef __cplusplus
}
#endif
