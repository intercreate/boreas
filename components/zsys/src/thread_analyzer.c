/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zsys/thread_analyzer.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "thread_analyzer";

static const char *state_to_str(eTaskState state)
{
    switch (state) {
    case eRunning:   return "Running";
    case eReady:     return "Ready";
    case eBlocked:   return "Blocked";
    case eSuspended: return "Suspend";
    case eDeleted:   return "Deleted";
    default:         return "Unknown";
    }
}

void zsys_thread_analyzer_print(void)
{
    UBaseType_t count = uxTaskGetNumberOfTasks();
    TaskStatus_t *status_array = pvPortMalloc(count * sizeof(TaskStatus_t));
    if (status_array == NULL) {
        ESP_LOGE(TAG, "Failed to allocate task status array");
        return;
    }

    uint32_t total_runtime;
    UBaseType_t actual = uxTaskGetSystemState(status_array, count,
                                              &total_runtime);

    ESP_LOGI(TAG, "%-20s %6s %6s %6s  %-8s %4s",
             "Name", "Stack", "HWM", "Free", "State", "Prio");
    ESP_LOGI(TAG, "%-20s %6s %6s %6s  %-8s %4s",
             "--------------------", "------", "------", "------",
             "--------", "----");

    for (UBaseType_t i = 0; i < actual; i++) {
        TaskStatus_t *t = &status_array[i];
        /* High water mark is in StackType_t units (typically 4 bytes) */
        uint32_t hwm_bytes = t->usStackHighWaterMark * sizeof(StackType_t);

        ESP_LOGI(TAG, "%-20s %6s %6lu %6s  %-8s %4lu",
                 t->pcTaskName,
                 "?",  /* Total stack not available from FreeRTOS */
                 (unsigned long)hwm_bytes,
                 "?",
                 state_to_str(t->eCurrentState),
                 (unsigned long)t->uxCurrentPriority);
    }

    vPortFree(status_array);
}
