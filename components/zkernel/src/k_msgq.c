/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include "esp_log.h"

static const char *TAG = "k_msgq";

int k_msgq_init(struct k_msgq *msgq, char *buffer, size_t msg_size,
                uint32_t max_msgs)
{
    msgq->msg_size = msg_size;
    msgq->max_msgs = max_msgs;
    msgq->storage = (uint8_t *)buffer;
    msgq->handle = xQueueCreateStatic(max_msgs, msg_size, msgq->storage,
                                      &msgq->buffer);
    if (msgq->handle == NULL) {
        ESP_LOGE(TAG, "Failed to create message queue");
        return -1;
    }
    return 0;
}

int k_msgq_put(struct k_msgq *msgq, const void *data, k_timeout_t timeout)
{
    BaseType_t ret;

    if (xPortInIsrContext()) {
        BaseType_t wake = pdFALSE;
        ret = xQueueSendToBackFromISR(msgq->handle, data, &wake);
        if (wake) {
            portYIELD_FROM_ISR(wake);
        }
    } else {
        ret = xQueueSendToBack(msgq->handle, data,
                               k_timeout_to_ticks(timeout));
    }
    return (ret == pdTRUE) ? 0 : -1;
}

int k_msgq_get(struct k_msgq *msgq, void *data, k_timeout_t timeout)
{
    BaseType_t ret;

    if (xPortInIsrContext()) {
        BaseType_t wake = pdFALSE;
        ret = xQueueReceiveFromISR(msgq->handle, data, &wake);
        if (wake) {
            portYIELD_FROM_ISR(wake);
        }
    } else {
        ret = xQueueReceive(msgq->handle, data,
                            k_timeout_to_ticks(timeout));
    }
    return (ret == pdTRUE) ? 0 : -1;
}

int k_msgq_peek(struct k_msgq *msgq, void *data)
{
    BaseType_t ret = xQueuePeek(msgq->handle, data, 0);
    return (ret == pdTRUE) ? 0 : -1;
}

void k_msgq_purge(struct k_msgq *msgq)
{
    xQueueReset(msgq->handle);
}

uint32_t k_msgq_num_used_get(struct k_msgq *msgq)
{
    return (uint32_t)uxQueueMessagesWaiting(msgq->handle);
}

uint32_t k_msgq_num_free_get(struct k_msgq *msgq)
{
    return (uint32_t)uxQueueSpacesAvailable(msgq->handle);
}
