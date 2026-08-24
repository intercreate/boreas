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
	int64_t timestamp_ms;                   /*  8 bytes -- k_uptime_get() */
	uint8_t level;                          /*  1 byte  */
	uint8_t _reserved;                      /*  1 byte  */
	char module[ZSYS_LOG_MODULE_NAME_MAX];  /* 16 bytes */
	char thread[ZSYS_LOG_THREAD_NAME_MAX];  /* 16 bytes */
	char text[CONFIG_ZSYS_LOG_MSG_MAX_LEN]; /* 80 bytes (default) */
};
/* Total ~122 bytes with defaults, naturally aligned by int64_t */

/* --------------------------------------------------------------------------
 * Backend API
 * -------------------------------------------------------------------------- */

struct log_backend;

typedef void (*log_backend_init_fn)(const struct log_backend *backend);
typedef void (*log_backend_put_fn)(const struct log_backend *backend, const struct log_msg *msg);
typedef void (*log_backend_panic_fn)(const struct log_backend *backend);

struct log_backend_api {
	log_backend_init_fn init;   /* called once at log subsystem init (may be NULL) */
	log_backend_put_fn put;     /* called for each message */
	log_backend_panic_fn panic; /* called on panic mode switch (may be NULL) */
};

struct log_backend {
	const struct log_backend_api *api;
	const char *name;
	void *ctx; /* backend-private context */
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

#if defined(CONFIG_IDF_TARGET_LINUX)
/* Mach-O fallback: host unit-test executable is whole-linked, so the legacy
 * constructor path is safe. See LOG_MODULE_REGISTER for the rationale. */
#define LOG_BACKEND_DEFINE(_name, _api, _ctx)                                                      \
	static const struct log_backend _log_backend_##_name = {                                   \
		.api = (_api),                                                                     \
		.name = #_name,                                                                    \
		.ctx = (_ctx),                                                                     \
	};                                                                                         \
	static void __attribute__((constructor)) _log_backend_register_##_name(void)               \
	{                                                                                          \
		zsys_log_backend_register(&_log_backend_##_name);                                  \
	}
#else
#define LOG_BACKEND_DEFINE(_name, _api, _ctx)                                                      \
	static const struct log_backend                                                            \
		__attribute__((section(".log_backends"), used)) _log_backend_##_name = {           \
			.api = (_api),                                                             \
			.name = #_name,                                                            \
			.ctx = (_ctx),                                                             \
	}
#endif

#else

#define LOG_BACKEND_DEFINE(_name, _api, _ctx)

#endif

/* --------------------------------------------------------------------------
 * Default message formatter
 * -------------------------------------------------------------------------- */

/**
 * @brief ANSI reset sequence, or "" when color is disabled.
 *
 * Closes a sequence opened with zsys_log_level_color().
 */
#if defined(CONFIG_ZSYS_LOG_BACKEND_SHOW_COLOR)
#define ZSYS_LOG_COLOR_RESET "\x1B[0m"
#else
#define ZSYS_LOG_COLOR_RESET ""
#endif

/**
 * @brief ANSI color escape for a log level.
 *
 * Follows Zephyr: bold red ERR, bold yellow WRN, and nothing else, unless
 * CONFIG_ZSYS_LOG_INFO_COLOR_GREEN / CONFIG_ZSYS_LOG_DBG_COLOR_BLUE are set.
 * Returns "" for every level when CONFIG_ZSYS_LOG_BACKEND_SHOW_COLOR is off.
 *
 * For backends that format the log_msg fields themselves; backends using
 * zsys_log_format_msg_color() get color applied for them.
 *
 * @note Governs LOG_* (zsys) output only. ESP_LOG* traffic from ESP-IDF
 *       internals is colored by ESP-IDF under CONFIG_LOG_COLORS, which uses
 *       a different, non-bold palette. A console carrying both will not look
 *       uniform; that is deliberate, since matching ESP-IDF here would mean
 *       diverging from Zephyr for the API Boreas actually implements.
 *
 * @note A level with no color still pairs with ZSYS_LOG_COLOR_RESET, so an
 *       uncolored line carries a bare reset. Upstream does the same --
 *       color_print() falls back to LOG_COLOR_CODE_DEFAULT whenever
 *       colors[level] is NULL, on the prefix and the postfix both -- and it
 *       clears color left set by another writer on the same UART.
 *
 * @param level  LOG_LEVEL_* value
 * @return Escape sequence, never NULL. Close it with ZSYS_LOG_COLOR_RESET.
 */
const char *zsys_log_level_color(int level);

/**
 * @brief Format a log message into a human-readable string.
 *
 * Output: [12.345] <INF> module: message text
 *
 * Never emits color, whatever the color options are set to, so the result is
 * safe for a file, network or RTT transport. Terminal-bound backends that want
 * color call zsys_log_format_msg_color() instead.
 *
 * @param msg  Log message to format
 * @param buf  Output buffer
 * @param buf_size  Size of output buffer
 * @return Number of characters written (excluding null terminator), or
 *         negative on error. May be >= buf_size if truncated.
 */
int zsys_log_format_msg(const struct log_msg *msg, char *buf, size_t buf_size);

/**
 * @brief Format a log message, optionally colorizing the level token.
 *
 * As zsys_log_format_msg(), but the caller decides whether the level token is
 * wrapped in ANSI escapes -- the per-backend control Zephyr spells
 * LOG_OUTPUT_FLAG_COLORS on a struct log_output. Color is applied only when
 * @p color is true AND CONFIG_ZSYS_LOG_BACKEND_SHOW_COLOR is enabled, so the
 * Kconfig remains a global off switch.
 *
 * Deferred mode hands every backend the same struct log_msg, so this is where
 * a terminal backend and a file backend part ways.
 *
 * @note As upstream, the color spans the level indicator through the end of
 *       the message; the leading timestamp stays uncolored.
 *
 * @param msg  Log message to format
 * @param buf  Output buffer
 * @param buf_size  Size of output buffer
 * @param color  Wrap the level token in ANSI escapes
 * @return Number of characters written (excluding null terminator), or
 *         negative on error. May be >= buf_size if truncated.
 */
int zsys_log_format_msg_color(const struct log_msg *msg, char *buf, size_t buf_size, bool color);

#ifdef __cplusplus
}
#endif
