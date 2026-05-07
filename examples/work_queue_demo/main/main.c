/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas Work Queue Demo
 *
 * Demonstrates k_timer, k_work, and k_work_delayable patterns.
 * No special hardware required -- runs on any ESP32 dev kit.
 *
 * What it shows:
 *   1. Periodic timer firing a callback
 *   2. Immediate work submission to the system work queue
 *   3. Delayed work (fire once after a delay)
 *   4. Reschedulable work (change the delay while pending)
 *   5. Using CONTAINER_OF to access context from a work handler
 */

#include <boreas/zephyr/kernel.h>
#include <boreas/zsys/log.h>
#include <esp_attr.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

/* -------------------------------------------------------------------
 * Example 1: Periodic timer with expiry callback
 *
 * With CONFIG_K_TIMER_DISPATCH_ISR=y (default), the expiry callback
 * runs in ISR context. Use k_work_submit to defer logging to a task.
 * ---------------------------------------------------------------- */

static volatile int heartbeat_count = 0;
static struct k_work heartbeat_log_work;

static void heartbeat_log_handler(struct k_work *work)
{
	LOG_INF("[Timer] Heartbeat #%d", heartbeat_count);
}

static void IRAM_ATTR heartbeat_expiry(struct k_timer *timer)
{
	heartbeat_count++;
	k_work_submit(&heartbeat_log_work);
}

static void heartbeat_stop(struct k_timer *timer)
{
	LOG_INF("[Timer] Heartbeat stopped after %d beats", heartbeat_count);
}

static struct k_timer heartbeat_timer;

/* -------------------------------------------------------------------
 * Example 2: Work item with context via CONTAINER_OF
 * ---------------------------------------------------------------- */

struct sensor_work_ctx {
	struct k_work work;
	int sensor_id;
	int reading;
};

static void sensor_work_handler(struct k_work *work)
{
	struct sensor_work_ctx *ctx = CONTAINER_OF(work, struct sensor_work_ctx, work);
	LOG_INF("[Work] Processing sensor %d, reading=%d", ctx->sensor_id, ctx->reading);
}

static struct sensor_work_ctx sensor_ctx;

/* -------------------------------------------------------------------
 * Example 3: Delayed work -- one-shot deferred task
 * ---------------------------------------------------------------- */

static void delayed_handler(struct k_work *work)
{
	LOG_INF("[Delayed] This ran 2 seconds after scheduling");
}

static struct k_work_delayable delayed_work;

/* -------------------------------------------------------------------
 * Example 4: Reschedulable work -- deadline keeps moving
 * ---------------------------------------------------------------- */

static int reschedule_count = 0;

static void reschedule_handler(struct k_work *work)
{
	reschedule_count++;
	LOG_INF("[Reschedule] Finally fired after %d reschedules", reschedule_count);
}

static struct k_work_delayable reschedule_work;

/* -------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------- */

void app_main(void)
{
	LOG_INF("=== Boreas Work Queue Demo ===");

	/* System work queue is auto-initialized before main() via constructor.
	 * Priority/stack come from CONFIG_SYSTEM_WORKQUEUE_*. No init call needed. */

	/* --- Example 1: Periodic timer --- */
	LOG_INF("Starting heartbeat timer (every 1s, runs for 5s)...");
	k_work_init(&heartbeat_log_work, heartbeat_log_handler);
	k_timer_init(&heartbeat_timer, heartbeat_expiry, heartbeat_stop);
	k_timer_start(&heartbeat_timer, K_SECONDS(1), K_SECONDS(1));

	/* --- Example 2: Immediate work with context --- */
	LOG_INF("Submitting sensor work...");
	sensor_ctx.sensor_id = 7;
	sensor_ctx.reading = 2350;
	k_work_init(&sensor_ctx.work, sensor_work_handler);
	k_work_submit(&sensor_ctx.work);

	/* --- Example 3: Delayed work --- */
	LOG_INF("Scheduling delayed work (fires in 2s)...");
	k_work_init_delayable(&delayed_work, delayed_handler);
	k_work_schedule(&delayed_work, K_SECONDS(2));

	/* --- Example 4: Reschedulable work --- */
	LOG_INF("Scheduling work for 5s, then rescheduling to 3s...");
	k_work_init_delayable(&reschedule_work, reschedule_handler);
	k_work_schedule(&reschedule_work, K_SECONDS(5));
	k_msleep(500);
	/* Oops, changed our mind -- fire sooner */
	k_work_reschedule(&reschedule_work, K_SECONDS(3));
	LOG_INF("Rescheduled to 3s from now (3.5s total)");

	/* Let everything run */
	k_msleep(5500);

	/* Stop the heartbeat */
	k_timer_stop(&heartbeat_timer);

	/* Show timer status */
	LOG_INF("Timer remaining: %lld ms", (long long)k_timer_remaining_get(&heartbeat_timer));

	LOG_INF("=== Demo complete ===");
}
