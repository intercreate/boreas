/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/fatal.h"
#include "zephyr/kernel.h"

#include "esp_log.h"
#include "esp_system.h"

#if defined(CONFIG_ZKERNEL_FATAL_CAPTURE)
#include "nvs.h"
#include "nvs_flash.h"
#endif

static const char *TAG = "fatal";

#if defined(CONFIG_ZKERNEL_FATAL_CAPTURE)

#define FATAL_NVS_NAMESPACE "boreas_fatal"
#define FATAL_NVS_KEY       "crash_info"

/* Serialized crash info stored in NVS */
struct fatal_nvs_record {
	uint8_t reason;
	char task_name[16];
	char file[64];
	int32_t line;
	uint32_t free_heap;
	int64_t uptime_ms;
};

static void print_saved_crash(void)
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open(FATAL_NVS_NAMESPACE, NVS_READONLY, &handle);
	if (err != ESP_OK) {
		return; /* No saved crash */
	}

	struct fatal_nvs_record record;
	size_t len = sizeof(record);
	err = nvs_get_blob(handle, FATAL_NVS_KEY, &record, &len);
	nvs_close(handle);

	if (err != ESP_OK) {
		return;
	}

	ESP_LOGE(TAG, "=== PREVIOUS CRASH DETECTED ===");
	ESP_LOGE(TAG, "  Reason:    %d", record.reason);
	ESP_LOGE(TAG, "  Task:      %s", record.task_name);
	ESP_LOGE(TAG, "  Location:  %s:%ld", record.file, (long)record.line);
	ESP_LOGE(TAG, "  Free heap: %lu bytes", (unsigned long)record.free_heap);
	ESP_LOGE(TAG, "  Uptime:    %lld ms", (long long)record.uptime_ms);
	ESP_LOGE(TAG, "===============================");

	/* Clear after printing */
	nvs_open(FATAL_NVS_NAMESPACE, NVS_READWRITE, &handle);
	nvs_erase_key(handle, FATAL_NVS_KEY);
	nvs_commit(handle);
	nvs_close(handle);
}

static void save_crash_info(enum k_fatal_reason reason, const char *file, int line)
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open(FATAL_NVS_NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		return;
	}

	struct fatal_nvs_record record = {
		.reason = (uint8_t)reason,
		.line = (int32_t)line,
		.free_heap = (uint32_t)esp_get_free_heap_size(),
		.uptime_ms = k_uptime_get(),
	};

	/* Safe string copies */
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	if (task) {
		strncpy(record.task_name, pcTaskGetName(task), sizeof(record.task_name) - 1);
		record.task_name[sizeof(record.task_name) - 1] = '\0';
	} else {
		strncpy(record.task_name, "unknown", sizeof(record.task_name));
	}

	if (file) {
		strncpy(record.file, file, sizeof(record.file) - 1);
		record.file[sizeof(record.file) - 1] = '\0';
	} else {
		strncpy(record.file, "unknown", sizeof(record.file));
	}

	nvs_set_blob(handle, FATAL_NVS_KEY, &record, sizeof(record));
	nvs_commit(handle);
	nvs_close(handle);
}

#endif /* CONFIG_ZKERNEL_FATAL_CAPTURE */

void k_fatal_init(void)
{
#if defined(CONFIG_ZKERNEL_FATAL_CAPTURE)
	print_saved_crash();
#endif
}

/* Weak hook for zsys logging panic mode.
 * zsys provides the strong definition that drains the log queue
 * and switches to synchronous output. */
__attribute__((weak)) void zsys_log_panic(void)
{
}

void k_fatal_error(enum k_fatal_reason reason, const char *file, int line)
{
	/* Switch logging to synchronous/panic mode before any output */
	zsys_log_panic();

	ESP_LOGE(TAG, "FATAL: reason=%d at %s:%d", reason, file ? file : "?", line);

	struct k_fatal_info info = {
		.reason = reason,
		.file = file,
		.line = line,
		.free_heap = (uint32_t)esp_get_free_heap_size(),
		.uptime_ms = k_uptime_get(),
	};

	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	info.task_name = task ? pcTaskGetName(task) : "unknown";

	/* User hook (weak, default no-op) */
	k_fatal_user_hook(&info);

#if defined(CONFIG_ZKERNEL_FATAL_CAPTURE)
	save_crash_info(reason, file, line);
#endif

	esp_restart();
}

__attribute__((weak)) void k_fatal_user_hook(const struct k_fatal_info *info)
{
	(void)info;
}
