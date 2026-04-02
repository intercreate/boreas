/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include "esp_log.h"

static const char *TAG = "k_timer";

static void k_timer_esp_callback(void *arg)
{
    struct k_timer *timer = (struct k_timer *)arg;
    timer->status++;
    if (timer->expiry_fn) {
        timer->expiry_fn(timer);
    }
}

void k_timer_init(struct k_timer *timer, k_timer_expiry_t expiry_fn,
                  k_timer_stop_t stop_fn)
{
    timer->expiry_fn = expiry_fn;
    timer->stop_fn = stop_fn;
    timer->user_data = NULL;
    timer->status = 0;
    timer->running = false;
    timer->handle = NULL;

    const esp_timer_create_args_t args = {
        .callback = k_timer_esp_callback,
        .arg = timer,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "k_timer",
    };
    esp_err_t err = esp_timer_create(&args, &timer->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
    }
}

void k_timer_start(struct k_timer *timer, k_timeout_t duration,
                   k_timeout_t period)
{
    /* Stop if already running */
    if (timer->running) {
        esp_timer_stop(timer->handle);
    }

    timer->status = 0;

    uint64_t duration_us = (uint64_t)k_timeout_to_ticks(duration)
                           * portTICK_PERIOD_MS * 1000;

    if (k_timeout_is_no_wait(period)) {
        /* One-shot */
        esp_err_t err = esp_timer_start_once(timer->handle, duration_us);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "start_once failed: %s", esp_err_to_name(err));
            return;
        }
    } else {
        /* Periodic — duration is first expiry, period is repeat interval */
        uint64_t period_us = (uint64_t)k_timeout_to_ticks(period)
                             * portTICK_PERIOD_MS * 1000;
        esp_err_t err = esp_timer_start_periodic(timer->handle, period_us);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "start_periodic failed: %s", esp_err_to_name(err));
            return;
        }
        /* TODO: esp_timer_start_periodic doesn't support different first
         * interval. If duration != period, we could use start_once first
         * then switch to periodic in the callback. */
    }

    timer->running = true;
}

void k_timer_stop(struct k_timer *timer)
{
    if (timer->running) {
        esp_timer_stop(timer->handle);
        timer->running = false;
        if (timer->stop_fn) {
            timer->stop_fn(timer);
        }
    }
}

uint32_t k_timer_status_get(struct k_timer *timer)
{
    uint32_t status = timer->status;
    timer->status = 0;
    return status;
}

uint32_t k_timer_status_sync(struct k_timer *timer)
{
    /* Block until at least one expiry. Simple polling approach. */
    while (timer->running && timer->status == 0) {
        k_msleep(1);
    }
    return k_timer_status_get(timer);
}

int64_t k_timer_remaining_get(struct k_timer *timer)
{
    if (!timer->running || timer->handle == NULL) {
        return 0;
    }
    uint64_t expiry = 0;
    esp_err_t err = esp_timer_get_expiry_time(timer->handle, &expiry);
    if (err != ESP_OK) {
        return 0;
    }
    int64_t now = esp_timer_get_time();
    int64_t remaining_us = (int64_t)expiry - now;
    return (remaining_us > 0) ? (remaining_us / 1000) : 0;
}

void k_timer_user_data_set(struct k_timer *timer, void *user_data)
{
    timer->user_data = user_data;
}

void *k_timer_user_data_get(struct k_timer *timer)
{
    return timer->user_data;
}
