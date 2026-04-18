/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Default logging backend -- console output via printf.
 *
 * In synchronous mode, output matches standard ESP-IDF format:
 *   I (12345) module: message
 *
 * In deferred mode, uses structured format with the log-time timestamp
 * (not the output-time), since the message may be output later:
 *   [12.345] <INF> module: message
 */

#include "zsys/log_backend.h"

#include <stdio.h>

#include "esp_log.h"

#if defined(CONFIG_ZSYS_LOG_MODULE)

static const char _level_char[] = { '?', 'E', 'W', 'I', 'D' };

static void esp_backend_init(const struct log_backend *backend)
{
    (void)backend;
}

static void esp_backend_put(const struct log_backend *backend,
                            const struct log_msg *msg)
{
    (void)backend;
    uint8_t lvl = (msg->level <= 4) ? msg->level : 0;

#if defined(CONFIG_ZSYS_LOG_MODE_DEFERRED)
    /* Structured format with the original log-time timestamp */
    char buf[CONFIG_ZSYS_LOG_MSG_MAX_LEN + 64];
    zsys_log_format_msg(msg, buf, sizeof(buf));
    printf("%s\n", buf);
#else
    /* Match standard ESP-IDF format: LETTER (timestamp_ms) tag: text */
    printf("%c (%lu) %s: %s\n",
           _level_char[lvl],
           (unsigned long)esp_log_timestamp(),
           msg->module,
           msg->text);
#endif
}

static void esp_backend_panic(const struct log_backend *backend)
{
    (void)backend;
    fflush(stdout);
}

static const struct log_backend_api __attribute__((used)) _esp_backend_api = {
    .init  = esp_backend_init,
    .put   = esp_backend_put,
    .panic = esp_backend_panic,
};

LOG_BACKEND_DEFINE(esp_log, &_esp_backend_api, NULL);

/* Archive-pull anchor: this TU's only externally-visible symbol. log.c
 * references it so the linker pulls this .o out of libzsys.a and places
 * the LOG_BACKEND_DEFINE struct into .log_backends. Without this, the
 * default console backend is silently dropped. */
const int zsys_log_backend_esp_anchor = 1;

#endif /* CONFIG_ZSYS_LOG_MODULE */
