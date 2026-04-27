/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Fatal error capture framework.
 *
 * On assertion failure or unrecoverable error, captures context
 * (task name, backtrace, heap state) to NVS. On next boot, prints
 * the saved crash info before normal startup.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum k_fatal_reason {
	K_FATAL_ASSERT = 0,
	K_FATAL_STACK_OVERFLOW,
	K_FATAL_OOM,
	K_FATAL_WATCHDOG,
	K_FATAL_USER,
};

struct k_fatal_info {
	enum k_fatal_reason reason;
	const char *file;
	int line;
	const char *task_name;
	uint32_t free_heap;
	int64_t uptime_ms;
};

/**
 * Initialize fatal error capture. Call early in boot.
 * Checks NVS for a saved crash and prints it if found.
 */
void k_fatal_init(void);

/**
 * Capture fatal error context and reboot.
 * Called from assertion handler or panic hook.
 */
void k_fatal_error(enum k_fatal_reason reason, const char *file, int line);

/**
 * User-overridable hook called before reboot.
 * Default implementation is a no-op (weak symbol).
 */
void k_fatal_user_hook(const struct k_fatal_info *info);

/* Assertion macro that captures context before aborting */
#define K_ASSERT(cond)                                                                             \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			k_fatal_error(K_FATAL_ASSERT, __FILE__, __LINE__);                         \
		}                                                                                  \
	} while (0)

#define K_ASSERT_MSG(cond, fmt, ...)                                                               \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			ESP_LOGE("ASSERT", fmt, ##__VA_ARGS__);                                    \
			k_fatal_error(K_FATAL_ASSERT, __FILE__, __LINE__);                         \
		}                                                                                  \
	} while (0)

#ifdef __cplusplus
}
#endif
