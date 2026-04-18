/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible logging subsystem implementation.
 *
 * This file uses ESP_LOG* for its own diagnostics to avoid recursion.
 */

#include "zsys/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "zephyr/kernel.h"

#if defined(CONFIG_ZSYS_LOG_MODULE)

static const char *TAG = "zsys_log";

/* -------------------------------------------------------------------------
 * Module registry (existing functionality, preserved)
 * ------------------------------------------------------------------------- */

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

    for (int i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, name) == 0) {
            return;
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

static const char *level_to_str(int level)
{
    switch (level) {
    case LOG_LEVEL_NONE: return "NONE";
    case LOG_LEVEL_ERR:  return "ERR";
    case LOG_LEVEL_WRN:  return "WRN";
    case LOG_LEVEL_INF:  return "INF";
    case LOG_LEVEL_DBG:  return "DBG";
    default:             return "???";
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
                 level_to_str((int)modules[i].current_level));
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

/* -------------------------------------------------------------------------
 * Backend registry
 *
 * Runtime array populated by zsys_log_backend_register(). On ESP targets,
 * zsys_log_init() walks the .log_backends linker section and registers each
 * entry. On the macOS host (Mach-O), LOG_BACKEND_DEFINE emits a constructor
 * that calls register directly; the host test executable is whole-linked so
 * archive-stripping isn't a concern.
 * ------------------------------------------------------------------------- */

#ifndef CONFIG_ZSYS_LOG_MAX_BACKENDS
#define CONFIG_ZSYS_LOG_MAX_BACKENDS 4
#endif

static const struct log_backend *_log_backends[CONFIG_ZSYS_LOG_MAX_BACKENDS];
static int _log_backend_count = 0;

void zsys_log_backend_register(const struct log_backend *backend)
{
    if (_log_backend_count >= CONFIG_ZSYS_LOG_MAX_BACKENDS) {
        ESP_LOGW(TAG, "Backend registry full, cannot register '%s'",
                 backend->name);
        return;
    }
    _log_backends[_log_backend_count++] = backend;
}

#if !defined(CONFIG_IDF_TARGET_LINUX)
extern const struct log_backend _log_backends_start[];
extern const struct log_backend _log_backends_end[];
#endif

/* -------------------------------------------------------------------------
 * Mode tracking
 * ------------------------------------------------------------------------- */

enum log_mode {
    LOG_MODE_SYNC = 0,
    LOG_MODE_DEFERRED,
    LOG_MODE_PANIC,
};

static volatile enum log_mode _log_mode = LOG_MODE_SYNC;
static uint32_t _log_dropped_count = 0;

/* -------------------------------------------------------------------------
 * Deferred mode resources (compiled only when enabled)
 * ------------------------------------------------------------------------- */

#if defined(CONFIG_ZSYS_LOG_MODE_DEFERRED)

#include "zephyr/kernel.h"

K_MSGQ_DEFINE(_zsys_log_msgq, sizeof(struct log_msg),
              CONFIG_ZSYS_LOG_BUFFER_COUNT, 4);

static K_THREAD_STACK_DEFINE(_log_thread_stack,
                             CONFIG_ZSYS_LOG_THREAD_STACK_SIZE);
static struct k_thread _log_thread;

static void log_output_thread(void *p1, void *p2, void *p3)
{
    (void)p2;
    (void)p3;
    (void)p1;
    struct log_msg msg;

    for (;;) {
        if (k_msgq_get(&_zsys_log_msgq, &msg, K_FOREVER) == 0) {
            for (int i = 0; i < _log_backend_count; i++) {
                const struct log_backend *b = _log_backends[i];
                if (b->api->put) {
                    b->api->put(b, &msg);
                }
            }
        }
    }
}

#endif /* CONFIG_ZSYS_LOG_MODE_DEFERRED */

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int get_runtime_level(const char *module)
{
    for (int i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, module) == 0) {
            return (int)modules[i].current_level;
        }
    }
    /* Module not registered -- allow all levels */
    return LOG_LEVEL_DBG;
}

static void fill_log_msg(struct log_msg *msg, uint8_t level,
                         const char *module, const char *fmt, va_list args)
{
    msg->timestamp_ms = k_uptime_get();
    msg->level = level;
    msg->_reserved = 0;

    strncpy(msg->module, module, ZSYS_LOG_MODULE_NAME_MAX - 1);
    msg->module[ZSYS_LOG_MODULE_NAME_MAX - 1] = '\0';

    if (xPortInIsrContext()) {
        strncpy(msg->thread, "ISR", ZSYS_LOG_THREAD_NAME_MAX);
    } else {
        TaskHandle_t task = xTaskGetCurrentTaskHandle();
        if (task) {
            strncpy(msg->thread, pcTaskGetName(task),
                    ZSYS_LOG_THREAD_NAME_MAX - 1);
            msg->thread[ZSYS_LOG_THREAD_NAME_MAX - 1] = '\0';
        } else {
            strncpy(msg->thread, "???", ZSYS_LOG_THREAD_NAME_MAX);
        }
    }

    vsnprintf(msg->text, CONFIG_ZSYS_LOG_MSG_MAX_LEN, fmt, args);
}

static void dispatch_to_backends(const struct log_msg *msg)
{
    for (int i = 0; i < _log_backend_count; i++) {
        const struct log_backend *b = _log_backends[i];
        if (b->api->put) {
            b->api->put(b, msg);
        }
    }
}

/* -------------------------------------------------------------------------
 * Core emit function
 * ------------------------------------------------------------------------- */

void zsys_log_msg_emit(uint8_t level, const char *module,
                       const char *fmt, ...)
{
    /* Runtime level check */
    if ((int)level > get_runtime_level(module)) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    struct log_msg msg;
    fill_log_msg(&msg, level, module, fmt, args);

    va_end(args);

    switch (_log_mode) {
    case LOG_MODE_SYNC:
    case LOG_MODE_PANIC:
        if (_log_backend_count > 0) {
            dispatch_to_backends(&msg);
        } else {
            /* No backends registered yet -- fall back to ESP_LOG */
            esp_log_write((esp_log_level_t)level, module, "%s\n", msg.text);
        }
        break;

#if defined(CONFIG_ZSYS_LOG_MODE_DEFERRED)
    case LOG_MODE_DEFERRED:
        if (k_msgq_put(&_zsys_log_msgq, &msg, K_NO_WAIT) != 0) {
            __atomic_fetch_add(&_log_dropped_count, 1, __ATOMIC_RELAXED);
        }
        break;
#endif

    default:
        break;
    }
}

/* -------------------------------------------------------------------------
 * Subsystem lifecycle
 * ------------------------------------------------------------------------- */

int zsys_log_init(void)
{
#if !defined(CONFIG_IDF_TARGET_LINUX)
    /* Pull log_backend_esp.c out of libzsys.a on ESP targets so its
     * LOG_BACKEND_DEFINE lands in .log_backends. The reference must survive
     * both compiler optimization and linker --gc-sections; we do it as a
     * volatile load inside this function (which is reached), which
     * guarantees actual runtime code generation that references the symbol.
     * A file-scope used-attr pointer is insufficient -- --gc-sections still
     * drops unreferenced exported rodata. */
    {
        extern const int zsys_log_backend_esp_anchor;
        const int * volatile p = &zsys_log_backend_esp_anchor;
        (void)*p;
    }


    /* ESP target: walk .log_module_entries and .log_backends linker sections
     * and populate the runtime registries. On Apple, constructors emitted by
     * the macros have already populated them. */
    extern const struct zsys_log_module_desc _log_module_entries_start[];
    extern const struct zsys_log_module_desc _log_module_entries_end[];
    {
        const size_t n = (size_t)(_log_module_entries_end -
                                  _log_module_entries_start);
        for (size_t i = 0; i < n; i++) {
            const struct zsys_log_module_desc *m = &_log_module_entries_start[i];
            esp_log_level_set(m->name, m->default_level);
            zsys_log_register_module(m->name, m->default_level);
        }
    }
    {
        const size_t n = (size_t)(_log_backends_end - _log_backends_start);
        for (size_t i = 0; i < n; i++) {
            zsys_log_backend_register(&_log_backends_start[i]);
        }
    }
#endif

    /* Initialize registered backends. */
    for (int i = 0; i < _log_backend_count; i++) {
        const struct log_backend *b = _log_backends[i];
        if (b->api->init) {
            b->api->init(b);
        }
    }

#if defined(CONFIG_ZSYS_LOG_MODE_DEFERRED)
    k_msgq_init(&_zsys_log_msgq, (char *)_zsys_log_msgq.storage,
                sizeof(struct log_msg), CONFIG_ZSYS_LOG_BUFFER_COUNT);

    _log_mode = LOG_MODE_DEFERRED;

    k_thread_create(&_log_thread, _log_thread_stack,
                    CONFIG_ZSYS_LOG_THREAD_STACK_SIZE,
                    log_output_thread, NULL, NULL, NULL,
                    CONFIG_ZSYS_LOG_THREAD_PRIORITY, 0, K_NO_WAIT);
    ESP_LOGI(TAG, "Deferred logging started (queue=%d, stack=%d)",
             CONFIG_ZSYS_LOG_BUFFER_COUNT, CONFIG_ZSYS_LOG_THREAD_STACK_SIZE);
#else
    ESP_LOGI(TAG, "Synchronous logging initialized (%d backends)",
             _log_backend_count);
#endif

    return 0;
}

void zsys_log_panic(void)
{
    _log_mode = LOG_MODE_PANIC;

#if defined(CONFIG_ZSYS_LOG_MODE_DEFERRED)
    /* Drain the message queue synchronously */
    struct log_msg msg;
    while (k_msgq_get(&_zsys_log_msgq, &msg, K_NO_WAIT) == 0) {
        dispatch_to_backends(&msg);
    }
#endif

    /* Notify backends */
    for (int i = 0; i < _log_backend_count; i++) {
        const struct log_backend *b = _log_backends[i];
        if (b->api->panic) {
            b->api->panic(b);
        }
    }
}

uint32_t zsys_log_get_dropped_count(void)
{
    return __atomic_load_n(&_log_dropped_count, __ATOMIC_RELAXED);
}

/* -------------------------------------------------------------------------
 * Default message formatter
 * ------------------------------------------------------------------------- */

int zsys_log_format_msg(const struct log_msg *msg, char *buf, size_t buf_size)
{
    uint32_t ms = (uint32_t)msg->timestamp_ms;
    return snprintf(buf, buf_size, "[%lu.%03lu] <%s> %s: %s",
                    (unsigned long)(ms / 1000),
                    (unsigned long)(ms % 1000),
                    level_to_str(msg->level),
                    msg->module,
                    msg->text);
}

void zsys_log_hexdump(uint8_t level, const char *module,
                      const void *data, size_t len, const char *label)
{
    if ((int)level > get_runtime_level(module)) {
        return;
    }

    const uint8_t *bytes = (const uint8_t *)data;

    /* First line: label with length */
    zsys_log_msg_emit(level, module, "%s (%u bytes):", label ? label : "hex",
                      (unsigned)len);

    /* Format 16 bytes per line */
    for (size_t offset = 0; offset < len; offset += 16) {
        char line[80];
        int pos = 0;
        size_t row_len = (len - offset > 16) ? 16 : (len - offset);

        for (size_t i = 0; i < row_len; i++) {
            if (i == 8) {
                line[pos++] = ' ';
            }
            pos += snprintf(line + pos, sizeof(line) - pos, " %02X",
                            bytes[offset + i]);
        }
        line[pos] = '\0';

        zsys_log_msg_emit(level, module, "%s", line);
    }
}

#else /* !CONFIG_ZSYS_LOG_MODULE */

/* Stubs when logging module is disabled */

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

void zsys_log_list_modules(void) {}

int zsys_log_get_module_count(void) { return 0; }

int zsys_log_get_module_info(int index, const char **name, int *level)
{
    (void)index; (void)name; (void)level;
    return -1;
}

void zsys_log_msg_emit(uint8_t level, const char *module,
                       const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_writev((esp_log_level_t)level, module, fmt, args);
    va_end(args);
}

int zsys_log_init(void) { return 0; }
void zsys_log_panic(void) {}
uint32_t zsys_log_get_dropped_count(void) { return 0; }

void zsys_log_hexdump(uint8_t level, const char *module,
                      const void *data, size_t len, const char *label)
{
    (void)level; (void)module; (void)data; (void)len; (void)label;
}

int zsys_log_format_msg(const struct log_msg *msg, char *buf, size_t buf_size)
{
    (void)msg; (void)buf; (void)buf_size;
    return 0;
}

#endif
