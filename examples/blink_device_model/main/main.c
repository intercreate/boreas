/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas Blink -- Device Model Example
 *
 * Blinks an LED using the Zephyr-inspired device model:
 *   - device_t with DEVICE_DEFINE for static device declarations
 *   - gpio_dt_spec for pin + active-low configuration
 *   - gpio_api vtable for hardware abstraction
 *   - k_timer for periodic toggle
 *
 * Shows the pattern used in real projects: hardware description is
 * separated from application logic. Changing the LED pin or adding
 * a second board revision is a config change, not a code change.
 *
 * Default: GPIO 2
 * Change BLINK_GPIO below for your board.
 */

#include <boreas/device_model.h>
#include <boreas/gpio_dt.h>
#include <boreas/zephyr/kernel.h>
#include <boreas/zsys/log.h>

LOG_MODULE_REGISTER(blink, LOG_LEVEL_INF);

/* -------------------------------------------------------------------
 * Hardware description -- change these for your board
 * ---------------------------------------------------------------- */

#define BLINK_GPIO GPIO_NUM_2

/* GPIO controller device (wraps ESP-IDF GPIO driver) */
DEVICE_DEFINE(gpio0, gpio_esp32_init, &gpio_esp32_api, NULL, NULL, NULL);

/* LED pin spec -- ties a specific pin to the GPIO controller */
static const struct gpio_dt_spec led_spec = {
    .port = &gpio0,
    .pin = BLINK_GPIO,
    .dt_flags = 0, /* Active-high. Set GPIO_DT_ACTIVE_LOW for inverted LEDs */
};

/* -------------------------------------------------------------------
 * Application logic -- no hardware details below this line
 * ---------------------------------------------------------------- */

static void blink_timer_expiry(struct k_timer *timer)
{
    gpio_pin_toggle_dt(&led_spec);
    LOG_INF("toggle");
}

static struct k_timer blink_timer;

void app_main(void)
{
    LOG_INF("=== Boreas Blink -- Device Model Example ===");

    /* Initialize the GPIO controller device */
    esp_err_t err = device_init(&gpio0);
    if (err != ESP_OK) {
        LOG_ERR("GPIO device init failed: %s", esp_err_to_name(err));
        return;
    }

    /* Configure the LED pin as output */
    err = gpio_pin_configure_dt(&led_spec, GPIO_DT_OUTPUT);
    if (err != ESP_OK) {
        LOG_ERR("LED pin configure failed: %s", esp_err_to_name(err));
        return;
    }

    /* Start blinking at 2 Hz (toggle every 500ms) */
    k_timer_init(&blink_timer, blink_timer_expiry, NULL);
    k_timer_start(&blink_timer, K_MSEC(500), K_MSEC(500));

    LOG_INF("Blinking GPIO %d at 2 Hz. Ctrl+] to exit.", BLINK_GPIO);

    /* Main task has nothing else to do */
    while (1) {
        k_msleep(10000);
    }
}
