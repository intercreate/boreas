/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "k_timer";

#ifdef CONFIG_K_TIMER_DISPATCH_ISR
static void IRAM_ATTR k_timer_esp_callback(void *arg)
#else
static void k_timer_esp_callback(void *arg)
#endif
{
	struct k_timer *timer = (struct k_timer *)arg;

	/* First-interval transition: first expiry was start_once, now switch to periodic */
	if (__atomic_load_n(&timer->first_interval_pending, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&timer->first_interval_pending, false, __ATOMIC_RELAXED);
		esp_err_t err = esp_timer_start_periodic(timer->handle, timer->period_us);
		__ASSERT(err == ESP_OK, "esp_timer_start_periodic failed in callback");
		(void)err;
	}

	__atomic_fetch_add(&timer->status, 1, __ATOMIC_RELEASE);

	if (timer->expiry_fn) {
		timer->expiry_fn(timer);
	}

	/* One-shot is done after this expiry; clear running so subsequent
	 * status_sync / remaining_get behave like an upstream stopped timer.
	 * Cleared AFTER expiry_fn so another task can't observe
	 * running==false and call k_timer_start/_stop while the user's
	 * callback is still executing on the same struct. */
	if (!__atomic_load_n(&timer->is_periodic, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&timer->running, false, __ATOMIC_RELEASE);
	}
}

void k_timer_init(struct k_timer *timer, k_timer_expiry_t expiry_fn, k_timer_stop_t stop_fn)
{
	timer->expiry_fn = expiry_fn;
	timer->stop_fn = stop_fn;
	timer->user_data = NULL;
	timer->status = 0;
	timer->running = false;
	timer->is_periodic = false;
	timer->first_interval_pending = false;
	timer->period_us = 0;
	timer->handle = NULL;

	const esp_timer_create_args_t args = {
		.callback = k_timer_esp_callback,
		.arg = timer,
#ifdef CONFIG_K_TIMER_DISPATCH_ISR
		.dispatch_method = ESP_TIMER_ISR,
#else
		.dispatch_method = ESP_TIMER_TASK,
#endif
		.name = "k_timer",
	};
	esp_err_t err = esp_timer_create(&args, &timer->handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
	}
}

void k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period)
{
	/* Stop any in-flight timer. With ESP_TIMER_TASK dispatch,
	 * esp_timer_stop blocks until the callback finishes; with
	 * ESP_TIMER_ISR dispatch it only removes the timer from the
	 * armed list (the ISR callback may still be executing on
	 * another core). Callers must not restart a timer whose
	 * callback is still running — same constraint as upstream Zephyr. */
	if (__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE)) {
		esp_timer_stop(timer->handle);
	}

	__atomic_store_n(&timer->status, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&timer->first_interval_pending, false, __ATOMIC_RELAXED);
	__atomic_store_n(&timer->is_periodic, !k_timeout_is_no_wait(period), __ATOMIC_RELEASE);

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
			__atomic_store_n(&timer->first_interval_pending, true, __ATOMIC_RELEASE);
			esp_err_t err = esp_timer_start_once(timer->handle, duration_us);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "start_once failed: %s", esp_err_to_name(err));
				return;
			}
		} else {
			timer->period_us = period_us;
			esp_err_t err = esp_timer_start_periodic(timer->handle, period_us);
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "start_periodic failed: %s", esp_err_to_name(err));
				return;
			}
		}
	}

	__atomic_store_n(&timer->running, true, __ATOMIC_RELEASE);
}

void k_timer_stop(struct k_timer *timer)
{
	if (__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE)) {
		esp_timer_stop(timer->handle);
		__atomic_store_n(&timer->running, false, __ATOMIC_RELEASE);
		if (timer->stop_fn) {
			timer->stop_fn(timer);
		}
	}
}

uint32_t k_timer_status_get(struct k_timer *timer)
{
	return __atomic_exchange_n(&timer->status, 0, __ATOMIC_ACQ_REL);
}

uint32_t k_timer_status_sync(struct k_timer *timer)
{
	/* Block until the timer expires or is stopped. Polls every
	 * k_msleep(1), which rounds up to one FreeRTOS tick
	 * (portTICK_PERIOD_MS, set by CONFIG_FREERTOS_HZ -- 1ms at the
	 * Boreas default of 1000 Hz, 10ms at the ESP-IDF Kconfig default
	 * of 100 Hz). Caller-visible semantics match upstream Zephyr's
	 * wait-queue version (block until expiry or stop, return count
	 * atomically); the divergence is the polling implementation. */
	while (__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE) &&
	       __atomic_load_n(&timer->status, __ATOMIC_ACQUIRE) == 0) {
		k_msleep(1);
	}
	return __atomic_exchange_n(&timer->status, 0, __ATOMIC_ACQ_REL);
}

uint32_t k_timer_remaining_get(struct k_timer *timer)
{
	if (!__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE) || timer->handle == NULL) {
		return 0;
	}
	uint64_t expiry = 0;
	esp_err_t err = esp_timer_get_expiry_time(timer->handle, &expiry);
	if (err != ESP_OK) {
		return 0;
	}
	int64_t now = esp_timer_get_time();
	int64_t remaining_us = (int64_t)expiry - now;
	if (remaining_us <= 0) {
		return 0;
	}
	int64_t remaining_ms = remaining_us / 1000;
	/* Saturate at UINT32_MAX (~49.7 days) rather than wrapping. */
	return (remaining_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)remaining_ms;
}

k_ticks_t k_timer_remaining_ticks(const struct k_timer *timer)
{
	if (!__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE) || timer->handle == NULL) {
		return 0;
	}
	uint64_t expiry = 0;
	esp_err_t err = esp_timer_get_expiry_time(timer->handle, &expiry);
	if (err != ESP_OK) {
		return 0;
	}
	int64_t now = esp_timer_get_time();
	int64_t remaining_us = (int64_t)expiry - now;
	if (remaining_us <= 0) {
		return 0;
	}
	/* esp_timer microseconds -> FreeRTOS ticks. Round UP so that any
	 * positive remaining time reports at least 1 tick -- the contract
	 * is that 0 means stopped or already expired. */
	const int64_t tick_us = (int64_t)portTICK_PERIOD_MS * 1000;
	return (k_ticks_t)((remaining_us + tick_us - 1) / tick_us);
}

k_ticks_t k_timer_expires_ticks(const struct k_timer *timer)
{
	const int64_t tick_us = (int64_t)portTICK_PERIOD_MS * 1000;

	/* Upstream: when the timer is not running, returns the CURRENT
	 * uptime (not zero). esp_timer_get_time() is uptime in us, and
	 * k_uptime_ticks() shares this clock domain. */
	if (!__atomic_load_n(&timer->running, __ATOMIC_ACQUIRE) || timer->handle == NULL) {
		return (k_ticks_t)(esp_timer_get_time() / tick_us);
	}
	uint64_t expiry = 0;
	esp_err_t err = esp_timer_get_expiry_time(timer->handle, &expiry);
	if (err != ESP_OK) {
		return (k_ticks_t)(esp_timer_get_time() / tick_us);
	}
	return (k_ticks_t)(expiry / (uint64_t)tick_us);
}
