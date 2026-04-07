/**
 * @file gpio_dt.h
 * @brief GPIO controller device types and convenience functions.
 */
#pragma once

#include "device_model.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "zephyr/sys/util.h"

/* Bits 0-7: dt_flags -- board-level properties, stored in gpio_dt_spec.dt_flags (uint8_t) */
#define GPIO_DT_ACTIVE_LOW  BIT(0)
#define GPIO_DT_OPEN_DRAIN  BIT(1) /* reserved */
#define GPIO_DT_OPEN_SOURCE BIT(2) /* reserved */
/* bits 3-7 reserved for future dt properties */

/* Bits 8+: configure-time flags -- passed to gpio_pin_configure_dt() at runtime */
#define GPIO_DT_INPUT     BIT(8)
#define GPIO_DT_OUTPUT    BIT(9)
#define GPIO_DT_PULL_UP   BIT(10)
#define GPIO_DT_PULL_DOWN BIT(11)

struct gpio_api {
    esp_err_t (*pin_configure)(const struct device *port, gpio_num_t pin, uint32_t flags);
    esp_err_t (*pin_set_raw)(const struct device *port, gpio_num_t pin, int value);
    int (*pin_get_raw)(const struct device *port, gpio_num_t pin);
    esp_err_t (*pin_toggle)(const struct device *port, gpio_num_t pin);
};

struct gpio_dt_spec {
    const struct device *port;
    gpio_num_t pin;
    uint8_t dt_flags;
};

static inline esp_err_t gpio_pin_configure_dt(const struct gpio_dt_spec *spec, uint32_t extra_flags)
{
    __ASSERT(spec != NULL, "gpio_pin_configure_dt: NULL spec");
    __ASSERT(device_is_ready(spec->port), "gpio_pin_configure_dt: port not ready");
    const struct gpio_api *api = spec->port->api;
    return api->pin_configure(spec->port, spec->pin, (spec->dt_flags & ~GPIO_DT_ACTIVE_LOW) | extra_flags);
}

static inline esp_err_t gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value)
{
    __ASSERT(spec != NULL, "gpio_pin_set_dt: NULL spec");
    __ASSERT(device_is_ready(spec->port), "gpio_pin_set_dt: port not ready");
    const struct gpio_api *api = spec->port->api;
    if (spec->dt_flags & GPIO_DT_ACTIVE_LOW) {
        value = !value;
    }
    return api->pin_set_raw(spec->port, spec->pin, value);
}

static inline int gpio_pin_get_dt(const struct gpio_dt_spec *spec)
{
    __ASSERT(spec != NULL, "gpio_pin_get_dt: NULL spec");
    __ASSERT(device_is_ready(spec->port), "gpio_pin_get_dt: port not ready");
    const struct gpio_api *api = spec->port->api;
    int raw = api->pin_get_raw(spec->port, spec->pin);
    if (raw < 0) {
        return raw;
    }
    return (spec->dt_flags & GPIO_DT_ACTIVE_LOW) ? !raw : raw;
}

static inline esp_err_t gpio_pin_toggle_dt(const struct gpio_dt_spec *spec)
{
    __ASSERT(spec != NULL, "gpio_pin_toggle_dt: NULL spec");
    __ASSERT(device_is_ready(spec->port), "gpio_pin_toggle_dt: port not ready");
    const struct gpio_api *api = spec->port->api;
    return api->pin_toggle(spec->port, spec->pin);
}

esp_err_t gpio_esp32_init(const struct device *dev);

extern const struct gpio_api gpio_esp32_api;
