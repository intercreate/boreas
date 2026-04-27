/**
 * @file gpio_dt.h
 * @brief GPIO controller device types and convenience functions.
 *
 * Provides Zephyr-compatible GPIO pin I/O, interrupt configuration,
 * and callback registration over ESP-IDF GPIO.
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

/* Bits 8-11: configure-time flags -- passed to gpio_pin_configure_dt() at runtime */
#define GPIO_DT_INPUT     BIT(8)
#define GPIO_DT_OUTPUT    BIT(9)
#define GPIO_DT_PULL_UP   BIT(10)
#define GPIO_DT_PULL_DOWN BIT(11)

/* Bits 13-16: interrupt trigger flags (Zephyr-compatible decomposition).
 * Passed to gpio_pin_interrupt_configure_dt(). */
#define GPIO_INT_DISABLE 0
#define GPIO_INT_ENABLE  BIT(13)
#define GPIO_INT_EDGE    BIT(14)
#define GPIO_INT_LOW_0   BIT(15)
#define GPIO_INT_HIGH_1  BIT(16)

#define GPIO_INT_EDGE_RISING  (GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_HIGH_1)
#define GPIO_INT_EDGE_FALLING (GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_LOW_0)
#define GPIO_INT_EDGE_BOTH    (GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)
#define GPIO_INT_LEVEL_LOW    (GPIO_INT_ENABLE | GPIO_INT_LOW_0)
#define GPIO_INT_LEVEL_HIGH   (GPIO_INT_ENABLE | GPIO_INT_HIGH_1)

/* Port-wide pin mask type. ESP32 has up to 48 GPIOs. */
typedef uint64_t gpio_port_pins_t;

/* Forward declaration */
struct gpio_callback;

typedef void (*gpio_callback_handler_t)(const struct device *port, struct gpio_callback *cb,
					gpio_port_pins_t pins);

struct gpio_callback {
	struct gpio_callback *next; /* singly-linked list (internal) */
	gpio_callback_handler_t handler;
	gpio_port_pins_t pin_mask;
};

struct gpio_api {
	esp_err_t (*pin_configure)(const struct device *port, gpio_num_t pin, uint32_t flags);
	esp_err_t (*pin_set_raw)(const struct device *port, gpio_num_t pin, int value);
	int (*pin_get_raw)(const struct device *port, gpio_num_t pin);
	esp_err_t (*pin_toggle)(const struct device *port, gpio_num_t pin);
	esp_err_t (*pin_interrupt_configure)(const struct device *port, gpio_num_t pin,
					     uint32_t flags);
	esp_err_t (*manage_callback)(const struct device *port, struct gpio_callback *cb, bool set);
};

struct gpio_dt_spec {
	const struct device *port;
	gpio_num_t pin;
	uint8_t dt_flags;
};

/* ----------------------------------------------------------------
 * Callback helpers
 * ---------------------------------------------------------------- */

static inline void gpio_init_callback(struct gpio_callback *cb, gpio_callback_handler_t handler,
				      gpio_port_pins_t pin_mask)
{
	__ASSERT(cb != NULL, "gpio_init_callback: NULL cb");
	cb->handler = handler;
	cb->pin_mask = pin_mask;
	cb->next = NULL;
}

static inline esp_err_t gpio_add_callback(const struct device *port, struct gpio_callback *cb)
{
	__ASSERT(port != NULL, "gpio_add_callback: NULL port");
	__ASSERT(device_is_ready(port), "gpio_add_callback: port not ready");
	const struct gpio_api *api = port->api;
	return api->manage_callback(port, cb, true);
}

static inline esp_err_t gpio_remove_callback(const struct device *port, struct gpio_callback *cb)
{
	__ASSERT(port != NULL, "gpio_remove_callback: NULL port");
	__ASSERT(device_is_ready(port), "gpio_remove_callback: port not ready");
	const struct gpio_api *api = port->api;
	return api->manage_callback(port, cb, false);
}

/* ----------------------------------------------------------------
 * Pin configuration and I/O
 * ---------------------------------------------------------------- */

static inline esp_err_t gpio_pin_configure_dt(const struct gpio_dt_spec *spec, uint32_t extra_flags)
{
	__ASSERT(spec != NULL, "gpio_pin_configure_dt: NULL spec");
	__ASSERT(device_is_ready(spec->port), "gpio_pin_configure_dt: port not ready");
	const struct gpio_api *api = spec->port->api;
	return api->pin_configure(spec->port, spec->pin,
				  (spec->dt_flags & ~GPIO_DT_ACTIVE_LOW) | extra_flags);
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

/* ----------------------------------------------------------------
 * Interrupt configuration
 * ---------------------------------------------------------------- */

static inline esp_err_t gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *spec,
							uint32_t flags)
{
	__ASSERT(spec != NULL, "gpio_pin_interrupt_configure_dt: NULL spec");
	__ASSERT(device_is_ready(spec->port), "gpio_pin_interrupt_configure_dt: port not ready");
	const struct gpio_api *api = spec->port->api;
	return api->pin_interrupt_configure(spec->port, spec->pin, flags);
}

/* ----------------------------------------------------------------
 * Port-wide operations
 * ---------------------------------------------------------------- */

static inline esp_err_t gpio_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	__ASSERT(port != NULL, "gpio_port_set_bits_raw: NULL port");
	__ASSERT(device_is_ready(port), "gpio_port_set_bits_raw: port not ready");
	const struct gpio_api *api = port->api;
	for (int i = 0; i < 64 && pins != 0; i++, pins >>= 1) {
		if (pins & 1) {
			esp_err_t ret = api->pin_set_raw(port, (gpio_num_t)i, 1);
			if (ret != ESP_OK) {
				return ret;
			}
		}
	}
	return ESP_OK;
}

static inline esp_err_t gpio_port_clear_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	__ASSERT(port != NULL, "gpio_port_clear_bits_raw: NULL port");
	__ASSERT(device_is_ready(port), "gpio_port_clear_bits_raw: port not ready");
	const struct gpio_api *api = port->api;
	for (int i = 0; i < 64 && pins != 0; i++, pins >>= 1) {
		if (pins & 1) {
			esp_err_t ret = api->pin_set_raw(port, (gpio_num_t)i, 0);
			if (ret != ESP_OK) {
				return ret;
			}
		}
	}
	return ESP_OK;
}

static inline esp_err_t gpio_port_get_raw(const struct device *port, gpio_port_pins_t *values)
{
	__ASSERT(port != NULL, "gpio_port_get_raw: NULL port");
	__ASSERT(device_is_ready(port), "gpio_port_get_raw: port not ready");
	const struct gpio_api *api = port->api;
	*values = 0;
	for (int i = 0; i < GPIO_NUM_MAX; i++) {
		int val = api->pin_get_raw(port, (gpio_num_t)i);
		if (val > 0) {
			*values |= ((uint64_t)1 << i);
		}
	}
	return ESP_OK;
}

static inline esp_err_t gpio_port_set_masked_raw(const struct device *port, gpio_port_pins_t mask,
						 gpio_port_pins_t value)
{
	__ASSERT(port != NULL, "gpio_port_set_masked_raw: NULL port");
	__ASSERT(device_is_ready(port), "gpio_port_set_masked_raw: port not ready");
	const struct gpio_api *api = port->api;
	for (int i = 0; i < 64 && mask != 0; i++, mask >>= 1, value >>= 1) {
		if (mask & 1) {
			esp_err_t ret = api->pin_set_raw(port, (gpio_num_t)i, value & 1);
			if (ret != ESP_OK) {
				return ret;
			}
		}
	}
	return ESP_OK;
}

static inline esp_err_t gpio_port_toggle_bits(const struct device *port, gpio_port_pins_t pins)
{
	__ASSERT(port != NULL, "gpio_port_toggle_bits: NULL port");
	__ASSERT(device_is_ready(port), "gpio_port_toggle_bits: port not ready");
	const struct gpio_api *api = port->api;
	for (int i = 0; i < 64 && pins != 0; i++, pins >>= 1) {
		if (pins & 1) {
			esp_err_t ret = api->pin_toggle(port, (gpio_num_t)i);
			if (ret != ESP_OK) {
				return ret;
			}
		}
	}
	return ESP_OK;
}

/* ----------------------------------------------------------------
 * ESP32 GPIO driver
 * ---------------------------------------------------------------- */

esp_err_t gpio_esp32_init(const struct device *dev);

extern const struct gpio_api gpio_esp32_api;
