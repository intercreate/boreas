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

	/* First-interval transition: first expiry was start_once, now switch to periodic */
	if (timer->first_interval_pending) {
		timer->first_interval_pending = false;
		esp_timer_start_periodic(timer->handle, timer->period_us);
	}

	__atomic_fetch_add(&timer->status, 1, __ATOMIC_RELAXED);
	if (timer->expiry_fn) {
		timer->expiry_fn(timer);
	}
}

void k_timer_init(struct k_timer *timer, k_timer_expiry_t expiry_fn, k_timer_stop_t stop_fn)
{
	timer->expiry_fn = expiry_fn;
	timer->stop_fn = stop_fn;
	timer->user_data = NULL;
	timer->status = 0;
	timer->running = false;
	timer->first_interval_pending = false;
	timer->period_us = 0;
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

void k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period)
{
	/* Stop if already running */
	if (timer->running) {
		esp_timer_stop(timer->handle);
	}

	timer->status = 0;
	timer->first_interval_pending = false;

	if (timer->handle == NULL) {
		ESP_LOGE(TAG, "Timer not initialized");
		return;
	}

	uint64_t duration_us = k_timeout_to_us(duration);

	if (k_timeout_is_no_wait(period)) {
		/* One-shot */
		esp_err_t err = esp_timer_start_once(timer->handle, duration_us);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "start_once failed: %s", esp_err_to_name(err));
			return;
		}
	} else {
		/* Periodic */
		uint64_t period_us = k_timeout_to_us(period);

		if (duration_us != period_us) {
			/* Different first interval: use start_once for the first expiry,
			 * then the callback switches to periodic at period_us. */
			timer->period_us = period_us;
			timer->first_interval_pending = true;
			esp_err_t err = esp_timer_start_once(timer->handle, duration_us);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "start_once failed: %s", esp_err_to_name(err));
				return;
			}
		} else {
			esp_err_t err = esp_timer_start_periodic(timer->handle, period_us);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "start_periodic failed: %s", esp_err_to_name(err));
				return;
			}
		}
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
	return __atomic_exchange_n(&timer->status, 0, __ATOMIC_RELAXED);
}

uint32_t k_timer_status_sync(struct k_timer *timer)
{
	/* Block until at least one expiry. Simple polling approach. */
	while (timer->running && __atomic_load_n(&timer->status, __ATOMIC_RELAXED) == 0) {
		k_msleep(1);
	}
	return k_timer_status_get(timer);
}

uint32_t k_timer_remaining_get(struct k_timer *timer)
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
	return (remaining_us > 0) ? (uint32_t)(remaining_us / 1000) : 0;
}

void k_timer_user_data_set(struct k_timer *timer, void *user_data)
{
	timer->user_data = user_data;
}

void *k_timer_user_data_get(struct k_timer *timer)
{
	return timer->user_data;
}
