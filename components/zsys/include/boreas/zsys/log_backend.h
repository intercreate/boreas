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
    bool                          active;
    void                         *ctx;   /* backend-private context */
};

/* --------------------------------------------------------------------------
 * Backend registration (constructor-based, matches SHELL_CMD_REGISTER pattern)
 * -------------------------------------------------------------------------- */

#if defined(CONFIG_ZSYS_LOG_MODULE)

void zsys_log_backend_register(struct log_backend *backend);

#define LOG_BACKEND_DEFINE(_name, _api, _ctx)                                \
    static struct log_backend _log_backend_##_name = {                       \
        .api    = (_api),                                                    \
        .name   = #_name,                                                    \
        .active = true,                                                      \
        .ctx    = (_ctx),                                                    \
    };                                                                       \
    static void __attribute__((constructor))                                  \
    _log_backend_register_##_name(void)                                      \
    {                                                                        \
        zsys_log_backend_register(&_log_backend_##_name);                    \
    }

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
