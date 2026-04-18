/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Logging backend abstraction.
 *
 * Log backends receive formatted messages and write them to an output
 * (UART, RTT, network, file, etc.). The default ESP backend routes
 * through esp_log_write for compatibility with ESP-IDF tooling.
 *
 * Custom backends:
 *   static void my_put(const struct log_backend *b, const struct log_msg *msg) {
 *       char buf[128];
 *       zsys_log_format_msg(msg, buf, sizeof(buf));
 *       my_transport_write(buf);
 *   }
 *   static const struct log_backend_api my_api = { .put = my_put };
 *   LOG_BACKEND_DEFINE(my_backend, &my_api, NULL);
 */

#pragma once

/* sdkconfig.h must be visible *before* the LOG_BACKEND_DEFINE macro is
 * defined below -- otherwise CONFIG_ZSYS_LOG_MODULE is undefined at macro
 * definition time and the macro silently becomes a no-op, even if callers
 * see CONFIG defined by the time they invoke it. */
#include "sdkconfig.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Log message -- the unit of deferred/sync log storage
 * -------------------------------------------------------------------------- */

#ifndef CONFIG_ZSYS_LOG_MSG_MAX_LEN
#define CONFIG_ZSYS_LOG_MSG_MAX_LEN 80
#endif

#define ZSYS_LOG_MODULE_NAME_MAX 16
#define ZSYS_LOG_THREAD_NAME_MAX 16

struct log_msg {
    int64_t  timestamp_ms;                        /*  8 bytes -- k_uptime_get() */
    uint8_t  level;                               /*  1 byte  */
    uint8_t  _reserved;                           /*  1 byte  */
    char     module[ZSYS_LOG_MODULE_NAME_MAX];    /* 16 bytes */
    char     thread[ZSYS_LOG_THREAD_NAME_MAX];    /* 16 bytes */
    char     text[CONFIG_ZSYS_LOG_MSG_MAX_LEN];   /* 80 bytes (default) */
};
/* Total ~122 bytes with defaults, naturally aligned by int64_t */

/* --------------------------------------------------------------------------
 * Backend API
 * -------------------------------------------------------------------------- */

struct log_backend;

typedef void (*log_backend_init_fn)(const struct log_backend *backend);
typedef void (*log_backend_put_fn)(const struct log_backend *backend,
                                   const struct log_msg *msg);
typedef void (*log_backend_panic_fn)(const struct log_backend *backend);

struct log_backend_api {
    log_backend_init_fn  init;   /* called once at log subsystem init (may be NULL) */
    log_backend_put_fn   put;    /* called for each message */
    log_backend_panic_fn panic;  /* called on panic mode switch (may be NULL) */
};

struct log_backend {
    const struct log_backend_api *api;
    const char                   *name;
    void                         *ctx;   /* backend-private context */
};

/* --------------------------------------------------------------------------
 * Backend registration (linker-section based)
 *
 * Backends are emplaced into the .log_backends section and enumerated by
 * zsys_log_init() at boot. See zsys/zsys.lf and docs/linker-section-registration.md.
 *
 * NOTE: LOG_BACKEND_DEFINE() must live in a TU that has at least one other
 * externally-referenced symbol, or in main/. Linker scripts do not pull
 * archive members -- only unresolved-symbol references do. (Same constraint
 * as ESP-IDF's ESP_SYSTEM_INIT_FN and boreas's SYS_INIT / DEVICE_DEFINE.)
 * -------------------------------------------------------------------------- */

#if defined(CONFIG_ZSYS_LOG_MODULE)

/* Runtime backend registration. Public API so Mach-O host builds (which use
 * constructors, since they can't use plain section names) can register, and
 * so that zsys_log_init() on ESP targets can populate the runtime array from
 * the linker section. */
void zsys_log_backend_register(const struct log_backend *backend);

#if defined(__APPLE__)
/* Mach-O fallback: host unit-test executable is whole-linked, so the legacy
 * constructor path is safe. See LOG_MODULE_REGISTER for the rationale. */
#define LOG_BACKEND_DEFINE(_name, _api, _ctx)                                \
    static const struct log_backend _log_backend_##_name = {                 \
        .api  = (_api),                                                      \
        .name = #_name,                                                      \
        .ctx  = (_ctx),                                                      \
    };                                                                       \
    static void __attribute__((constructor))                                 \
    _log_backend_register_##_name(void)                                      \
    {                                                                        \
        zsys_log_backend_register(&_log_backend_##_name);                    \
    }
#else
#define LOG_BACKEND_DEFINE(_name, _api, _ctx)                                \
    static const struct log_backend                                          \
        __attribute__((section(".log_backends"), used))                      \
        _log_backend_##_name = {                                             \
            .api  = (_api),                                                  \
            .name = #_name,                                                  \
            .ctx  = (_ctx),                                                  \
        }
#endif

#else

#define LOG_BACKEND_DEFINE(_name, _api, _ctx)

#endif

/* --------------------------------------------------------------------------
 * Default message formatter
 * -------------------------------------------------------------------------- */

/**
 * Format a log message into a human-readable string.
 * Output: [12.345] <INF> module: message text
 *
 * @param msg  Log message to format
 * @param buf  Output buffer
 * @param buf_size  Size of output buffer
 * @return Number of characters written (excluding null terminator), or
 *         negative on error. May be >= buf_size if truncated.
 */
int zsys_log_format_msg(const struct log_msg *msg, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
