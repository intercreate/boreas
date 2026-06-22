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
#include "zephyr/sys/slist.h"
#include "zephyr/sys/util.h"

/* Bits 0-7: dt_flags — board-level properties (uint8_t in gpio_dt_spec).
 * Bit positions match upstream Zephyr dt-bindings/gpio/gpio.h. */
#define GPIO_ACTIVE_LOW  BIT(0)
/* Named zero: active-high is the absence of the active-low bit. Mirrors
 * upstream's GPIO_ACTIVE_HIGH so boards/devicetree can state intent explicitly. */
#define GPIO_ACTIVE_HIGH (0 << 0)
#define GPIO_OPEN_DRAIN  BIT(1) /* reserved */
#define GPIO_OPEN_SOURCE BIT(2) /* reserved */
#define GPIO_PULL_UP     BIT(4)
#define GPIO_PULL_DOWN   BIT(5)

/* Bits 16-20: configure-time direction & output-init flags.
 * Bit positions match upstream Zephyr include/zephyr/drivers/gpio.h. */
#define GPIO_INPUT               BIT(16)
#define GPIO_OUTPUT              BIT(17)
#define GPIO_OUTPUT_INIT_LOW     BIT(18)
#define GPIO_OUTPUT_INIT_HIGH    BIT(19)
#define GPIO_OUTPUT_INIT_LOGICAL BIT(20)

/** Configure as output, initial physical low. */
#define GPIO_OUTPUT_LOW      (GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW)
/** Configure as output, initial physical high. */
#define GPIO_OUTPUT_HIGH     (GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH)
/** Configure as output, initial logical inactive (respects active-low). */
#define GPIO_OUTPUT_INACTIVE (GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_LOGICAL)
/** Configure as output, initial logical active (respects active-low). */
#define GPIO_OUTPUT_ACTIVE   (GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH | GPIO_OUTPUT_INIT_LOGICAL)

/* Bits 21-24: interrupt trigger flags.
 * Bit positions match upstream Zephyr include/zephyr/drivers/gpio.h. */
#define GPIO_INT_DISABLE 0
#define GPIO_INT_ENABLE  BIT(21)
#define GPIO_INT_EDGE    BIT(22)
#define GPIO_INT_LOW_0   BIT(23)
#define GPIO_INT_HIGH_1  BIT(24)

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
	sys_snode_t node;
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
	uint32_t flags = (spec->dt_flags & ~GPIO_ACTIVE_LOW) | extra_flags;

	__ASSERT(!((flags & GPIO_OUTPUT_INIT_LOW) && (flags & GPIO_OUTPUT_INIT_HIGH)),
		 "GPIO_OUTPUT_INIT_LOW and GPIO_OUTPUT_INIT_HIGH are mutually exclusive");

	/* Logical init: when GPIO_OUTPUT_INIT_LOGICAL is set and the pin is
	 * active-low, swap INIT_LOW ↔ INIT_HIGH so the driver sees the
	 * correct physical level. Matches upstream Zephyr z_impl_gpio_pin_configure. */
	if ((flags & GPIO_OUTPUT_INIT_LOGICAL) &&
	    (flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) &&
	    (spec->dt_flags & GPIO_ACTIVE_LOW)) {
		flags ^= GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH;
	}
	flags &= ~GPIO_OUTPUT_INIT_LOGICAL;

	return api->pin_configure(spec->port, spec->pin, flags);
}

static inline esp_err_t gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value)
{
	__ASSERT(spec != NULL, "gpio_pin_set_dt: NULL spec");
	__ASSERT(device_is_ready(spec->port), "gpio_pin_set_dt: port not ready");
	const struct gpio_api *api = spec->port->api;
	if (spec->dt_flags & GPIO_ACTIVE_LOW) {
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
	return (spec->dt_flags & GPIO_ACTIVE_LOW) ? !raw : raw;
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
