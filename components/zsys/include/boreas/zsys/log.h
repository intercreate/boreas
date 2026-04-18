/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible logging subsystem.
 *
 * Usage:
 *   // In source file (one per .c file)
 *   #include "zsys/log.h"
 *   LOG_MODULE_REGISTER(sip_service, LOG_LEVEL_INF);
 *
 *   void my_function(void) {
 *       LOG_INF("started with %d items", count);
 *       LOG_ERR("failed: %s", reason);
 *       LOG_DBG("detail: x=%d y=%d", x, y);
 *   }
 *
 *   // At runtime (e.g., from shell)
 *   zsys_log_set_level("sip_service", LOG_LEVEL_DBG);
 */

#pragma once

#include "esp_log.h"

#include "zsys/log_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Log levels (0-4, numerically matches ESP-IDF values)
 * -------------------------------------------------------------------------- */

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERR  1
#define LOG_LEVEL_WRN  2
#define LOG_LEVEL_INF  3
#define LOG_LEVEL_DBG  4

/* --------------------------------------------------------------------------
 * Compile-time level stripping
 * -------------------------------------------------------------------------- */

#ifndef CONFIG_ZSYS_LOG_MAX_LEVEL
#define CONFIG_ZSYS_LOG_MAX_LEVEL LOG_LEVEL_DBG
#endif

/* --------------------------------------------------------------------------
 * Module registration
 * -------------------------------------------------------------------------- */

#if defined(CONFIG_ZSYS_LOG_MODULE)

/* Descriptor placed into a linker-section registry. Enumerated by
 * zsys_log_init() at boot. See also zsys/zsys.lf and docs/linker-section-registration.md.
 *
 * NOTE: LOG_MODULE_REGISTER() must live in a TU that has at least one other
 * externally-referenced symbol, or in main/. Linker scripts do not pull
 * archive members -- only unresolved-symbol references do. (Same constraint
 * as ESP-IDF's ESP_SYSTEM_INIT_FN and boreas's SYS_INIT / DEVICE_DEFINE.)
 */
struct zsys_log_module_desc {
    const char     *name;
    esp_log_level_t default_level;
};

#if defined(CONFIG_IDF_TARGET_LINUX)
/* Mach-O (macOS host) doesn't accept plain section names. The linux-target
 * unit test executable is whole-linked, so the legacy constructor path is
 * safe here -- no archive-stripping risk to work around. */
#define LOG_MODULE_REGISTER(module_name, default_level_)                    \
    static const char *TAG = #module_name;                                  \
    static void __attribute__((constructor))                                \
    _zsys_log_module_register_##module_name(void)                           \
    {                                                                       \
        esp_log_level_set(#module_name, (esp_log_level_t)(default_level_)); \
        zsys_log_register_module(#module_name,                              \
                                 (esp_log_level_t)(default_level_));        \
    }
#else
#define LOG_MODULE_REGISTER(module_name, default_level_)                    \
    static const char *TAG = #module_name;                                  \
    static const struct zsys_log_module_desc                                \
        __attribute__((section(".log_module_entries"), used))               \
        _zsys_log_module_desc_##module_name = {                             \
            .name          = #module_name,                                  \
            .default_level = (esp_log_level_t)(default_level_),             \
        }
#endif

/**
 * Declare use of a log module registered in another file.
 * Shares the same TAG and level control as the registering file.
 */
#define LOG_MODULE_DECLARE(module_name) \
    extern const char *TAG

#else

#define LOG_MODULE_REGISTER(module_name, default_level) \
    static const char *TAG = #module_name

#define LOG_MODULE_DECLARE(module_name) \
    extern const char *TAG

#endif

/* --------------------------------------------------------------------------
 * LOG output macros
 *
 * When CONFIG_ZSYS_LOG_MODULE is enabled:
 *   - Compile-time stripping via CONFIG_ZSYS_LOG_MAX_LEVEL
 *   - Routes through zsys_log_msg_emit (sync or deferred)
 *
 * When CONFIG_ZSYS_LOG_MODULE is disabled:
 *   - Falls back to ESP_LOG* with zero overhead
 * -------------------------------------------------------------------------- */

#if defined(CONFIG_ZSYS_LOG_MODULE)

#define _ZSYS_LOG(_level, _tag, _fmt, ...)                                  \
    do {                                                                     \
        if ((_level) <= CONFIG_ZSYS_LOG_MAX_LEVEL) {                        \
            zsys_log_msg_emit((_level), (_tag), _fmt, ##__VA_ARGS__);       \
        }                                                                    \
    } while (0)

#define LOG_ERR(fmt, ...) _ZSYS_LOG(LOG_LEVEL_ERR, TAG, fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) _ZSYS_LOG(LOG_LEVEL_WRN, TAG, fmt, ##__VA_ARGS__)
#define LOG_INF(fmt, ...) _ZSYS_LOG(LOG_LEVEL_INF, TAG, fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) _ZSYS_LOG(LOG_LEVEL_DBG, TAG, fmt, ##__VA_ARGS__)

#define _ZSYS_LOG_HEXDUMP(_level, _tag, _data, _len, _label)               \
    do {                                                                     \
        if ((_level) <= CONFIG_ZSYS_LOG_MAX_LEVEL) {                        \
            zsys_log_hexdump((_level), (_tag), (_data), (_len), (_label));  \
        }                                                                    \
    } while (0)

#define LOG_HEXDUMP_ERR(data, len, label) _ZSYS_LOG_HEXDUMP(LOG_LEVEL_ERR, TAG, data, len, label)
#define LOG_HEXDUMP_WRN(data, len, label) _ZSYS_LOG_HEXDUMP(LOG_LEVEL_WRN, TAG, data, len, label)
#define LOG_HEXDUMP_INF(data, len, label) _ZSYS_LOG_HEXDUMP(LOG_LEVEL_INF, TAG, data, len, label)
#define LOG_HEXDUMP_DBG(data, len, label) _ZSYS_LOG_HEXDUMP(LOG_LEVEL_DBG, TAG, data, len, label)

#else

/* Fallback: direct ESP-IDF logging, no overhead */
#define LOG_ERR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define LOG_INF(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)

#define LOG_HEXDUMP_ERR(data, len, label) ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_ERROR)
#define LOG_HEXDUMP_WRN(data, len, label) ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_WARN)
#define LOG_HEXDUMP_INF(data, len, label) ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO)
#define LOG_HEXDUMP_DBG(data, len, label) ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG)

#endif

/* --------------------------------------------------------------------------
 * Core API
 * -------------------------------------------------------------------------- */

/**
 * Emit a log message. Handles runtime level check and sync/deferred dispatch.
 * ISR-safe when deferred mode is active.
 */
void zsys_log_msg_emit(uint8_t level, const char *module,
                       const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * Initialize the logging subsystem.
 * - Initializes all registered backends
 * - In deferred mode: creates the output thread
 * Call from app_main() or via SYS_INIT. If never called, logging
 * stays in synchronous mode with the default ESP backend.
 */
int zsys_log_init(void);

/**
 * Switch to panic mode: drain the message queue synchronously,
 * switch to direct output, and notify backends.
 * Called from k_fatal_error() before reboot.
 */
void zsys_log_panic(void);

/**
 * Get the number of dropped messages (queue-full events in deferred mode).
 */
uint32_t zsys_log_get_dropped_count(void);

/**
 * Log a hex dump of binary data. Output as one message per 16-byte line.
 * Goes through the same dispatch path as LOG_* (backends, deferred mode).
 */
void zsys_log_hexdump(uint8_t level, const char *module,
                      const void *data, size_t len, const char *label);

/* --------------------------------------------------------------------------
 * Module registry API (unchanged)
 * -------------------------------------------------------------------------- */

void zsys_log_register_module(const char *name, esp_log_level_t default_level);
int  zsys_log_set_level(const char *module_name, esp_log_level_t level);
void zsys_log_list_modules(void);
int  zsys_log_get_module_count(void);
int  zsys_log_get_module_info(int index, const char **name, int *level);

#ifdef __cplusplus
}
#endif
