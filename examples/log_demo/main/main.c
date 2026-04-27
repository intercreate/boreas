/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas Logging Demo
 *
 * Demonstrates the Zephyr-compatible logging subsystem:
 *   1. Per-module registration with LOG_MODULE_REGISTER
 *   2. Four log levels: ERR, WRN, INF, DBG
 *   3. Runtime log level control via zsys_log_set_level()
 *   4. Custom log backend (message counter)
 *   5. Compile-time stripping via CONFIG_ZSYS_LOG_MAX_LEVEL
 *
 * The logging subsystem dispatches messages to registered backends.
 * The default ESP backend formats output to match ESP-IDF conventions.
 * Custom backends can route messages to any transport (UART, RTT,
 * network, flash, etc.).
 */

#include <boreas/zephyr/kernel.h>
#include <boreas/zsys/log.h>

LOG_MODULE_REGISTER(log_demo, LOG_LEVEL_DBG);

/* -------------------------------------------------------------------
 * Custom backend: message counter
 *
 * Demonstrates LOG_BACKEND_DEFINE. On ESP targets the backend struct is
 * emplaced into the .log_backends linker section and picked up by
 * zsys_log_init(); on the macOS host test target a constructor fallback
 * runs instead. Either way the backend must live in main/ or a TU with
 * another externally-referenced symbol so the linker doesn't strip it.
 * This backend simply counts messages by level.
 * ---------------------------------------------------------------- */

static volatile int _err_count;
static volatile int _wrn_count;
static volatile int _inf_count;
static volatile int _dbg_count;

static void counter_put(const struct log_backend *backend, const struct log_msg *msg)
{
	(void)backend;
	switch (msg->level) {
	case LOG_LEVEL_ERR:
		_err_count++;
		break;
	case LOG_LEVEL_WRN:
		_wrn_count++;
		break;
	case LOG_LEVEL_INF:
		_inf_count++;
		break;
	case LOG_LEVEL_DBG:
		_dbg_count++;
		break;
	default:
		break;
	}
}

static const struct log_backend_api __attribute__((used)) _counter_api = {
	.put = counter_put,
};

LOG_BACKEND_DEFINE(counter, &_counter_api, NULL);

/* -------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------- */

void app_main(void)
{
	/* Initialize the logging subsystem (backends, deferred thread if enabled) */
	zsys_log_init();

	LOG_INF("=== Boreas Logging Demo ===");

	/* --- 1. Four log levels --- */
	LOG_INF("--- Log Levels ---");
	LOG_ERR("Error: sensor %d not responding", 3);
	LOG_WRN("Warning: battery at %d%%", 15);
	LOG_INF("Info: system started with %d modules", zsys_log_get_module_count());
	LOG_DBG("Debug: internal state x=%d y=%d", 42, 7);

	k_msleep(100); /* let output flush */

	/* --- 2. Runtime level control --- */
	LOG_INF("--- Runtime Level Control ---");
	LOG_INF("Current level: DBG (all messages visible)");

	/* Raise the floor to WRN -- INF and DBG are suppressed */
	zsys_log_set_level("log_demo", LOG_LEVEL_WRN);
	LOG_DBG("This DBG message is filtered (should NOT appear)");
	LOG_INF("This INF message is filtered (should NOT appear)");
	LOG_WRN("This WRN message passes the filter");
	LOG_ERR("This ERR message passes the filter");

	/* Restore to DBG */
	zsys_log_set_level("log_demo", LOG_LEVEL_DBG);
	LOG_INF("Level restored to DBG -- all messages visible again");

	k_msleep(100);

	/* --- 3. Module listing --- */
	LOG_INF("--- Registered Modules ---");
	int count = zsys_log_get_module_count();
	for (int i = 0; i < count; i++) {
		const char *name;
		int level;
		if (zsys_log_get_module_info(i, &name, &level) == 0) {
			LOG_INF("  [%d] %-16s level=%d", i, name, level);
		}
	}

	k_msleep(100);

	/* --- 4. Custom backend results --- */
	LOG_INF("--- Custom Backend (counter) ---");
	LOG_INF("Messages counted: ERR=%d WRN=%d INF=%d DBG=%d", _err_count, _wrn_count, _inf_count,
		_dbg_count);

	LOG_INF("=== Demo complete ===");
}
