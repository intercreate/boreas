/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Tests for GPIO output-init flag logic and glitch-free configure.
 * Uses a mock gpio_api (requires driver/gpio.h, non-Linux targets only).
 */

#include "unity.h"
#include "device_model.h"
#include "gpio_dt.h"

/* ----------------------------------------------------------------
 * Mock GPIO driver -- captures flags passed to pin_configure
 * ---------------------------------------------------------------- */

static uint32_t last_configure_flags;
static gpio_num_t last_configure_pin;
static int last_set_raw_value;
static gpio_num_t last_set_raw_pin;
static int set_raw_call_count;
static int configure_call_count;

static esp_err_t mock_pin_configure(const struct device *port, gpio_num_t pin, uint32_t flags)
{
	(void)port;
	last_configure_pin = pin;
	last_configure_flags = flags;
	configure_call_count++;
	return ESP_OK;
}

static esp_err_t mock_pin_set_raw(const struct device *port, gpio_num_t pin, int value)
{
	(void)port;
	last_set_raw_pin = pin;
	last_set_raw_value = value;
	set_raw_call_count++;
	return ESP_OK;
}

static int mock_pin_get_raw(const struct device *port, gpio_num_t pin)
{
	(void)port;
	(void)pin;
	return 0;
}

static esp_err_t mock_pin_toggle(const struct device *port, gpio_num_t pin)
{
	(void)port;
	(void)pin;
	return ESP_OK;
}

static esp_err_t mock_pin_interrupt_configure(const struct device *port, gpio_num_t pin,
					      uint32_t flags)
{
	(void)port;
	(void)pin;
	(void)flags;
	return ESP_OK;
}

static esp_err_t mock_manage_callback(const struct device *port, struct gpio_callback *cb, bool set)
{
	(void)port;
	(void)cb;
	(void)set;
	return ESP_OK;
}

static const struct gpio_api mock_gpio_api = {
	.pin_configure = mock_pin_configure,
	.pin_set_raw = mock_pin_set_raw,
	.pin_get_raw = mock_pin_get_raw,
	.pin_toggle = mock_pin_toggle,
	.pin_interrupt_configure = mock_pin_interrupt_configure,
	.manage_callback = mock_manage_callback,
};

static esp_err_t mock_gpio_init(const struct device *dev)
{
	(void)dev;
	return ESP_OK;
}

DEVICE_DEFINE(mock_gpio, mock_gpio_init, &mock_gpio_api, NULL, NULL, NULL);

static void reset_mock(void)
{
	device_init(&mock_gpio);
	last_configure_flags = 0;
	last_configure_pin = 0;
	last_set_raw_value = -1;
	last_set_raw_pin = 0;
	set_raw_call_count = 0;
	configure_call_count = 0;
}

/* ----------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------- */

static void test_gpio_output_basic(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {.port = &mock_gpio, .pin = 5, .dt_flags = 0};

	gpio_pin_configure_dt(&spec, GPIO_OUTPUT);

	TEST_ASSERT_EQUAL(5, last_configure_pin);
	TEST_ASSERT_BITS(GPIO_OUTPUT, GPIO_OUTPUT, last_configure_flags);
	TEST_ASSERT_EQUAL(1, configure_call_count);
}

static void test_gpio_output_init_low(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {.port = &mock_gpio, .pin = 7, .dt_flags = 0};

	gpio_pin_configure_dt(&spec, GPIO_OUTPUT_LOW);

	TEST_ASSERT_BITS(GPIO_OUTPUT, GPIO_OUTPUT, last_configure_flags);
	TEST_ASSERT_BITS(GPIO_OUTPUT_INIT_LOW, GPIO_OUTPUT_INIT_LOW, last_configure_flags);
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_HIGH, last_configure_flags);
}

static void test_gpio_output_init_high(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {.port = &mock_gpio, .pin = 7, .dt_flags = 0};

	gpio_pin_configure_dt(&spec, GPIO_OUTPUT_HIGH);

	TEST_ASSERT_BITS(GPIO_OUTPUT, GPIO_OUTPUT, last_configure_flags);
	TEST_ASSERT_BITS(GPIO_OUTPUT_INIT_HIGH, GPIO_OUTPUT_INIT_HIGH, last_configure_flags);
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_LOW, last_configure_flags);
}

static void test_gpio_output_inactive_active_high(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {.port = &mock_gpio, .pin = 3, .dt_flags = 0};

	gpio_pin_configure_dt(&spec, GPIO_OUTPUT_INACTIVE);

	/* Active-high: inactive = physical low */
	TEST_ASSERT_BITS(GPIO_OUTPUT_INIT_LOW, GPIO_OUTPUT_INIT_LOW, last_configure_flags);
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_HIGH, last_configure_flags);
	/* INIT_LOGICAL stripped before driver call */
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_LOGICAL, last_configure_flags);
}

static void test_gpio_output_inactive_active_low(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {
		.port = &mock_gpio, .pin = 3, .dt_flags = GPIO_ACTIVE_LOW};

	gpio_pin_configure_dt(&spec, GPIO_OUTPUT_INACTIVE);

	/* Active-low: inactive = physical HIGH (XOR swapped) */
	TEST_ASSERT_BITS(GPIO_OUTPUT_INIT_HIGH, GPIO_OUTPUT_INIT_HIGH, last_configure_flags);
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_LOW, last_configure_flags);
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_LOGICAL, last_configure_flags);
}

static void test_gpio_output_active_active_low(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {
		.port = &mock_gpio, .pin = 3, .dt_flags = GPIO_ACTIVE_LOW};

	gpio_pin_configure_dt(&spec, GPIO_OUTPUT_ACTIVE);

	/* Active-low: active = physical LOW (XOR swapped) */
	TEST_ASSERT_BITS(GPIO_OUTPUT_INIT_LOW, GPIO_OUTPUT_INIT_LOW, last_configure_flags);
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_HIGH, last_configure_flags);
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_LOGICAL, last_configure_flags);
}

static void test_gpio_output_active_active_high(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {.port = &mock_gpio, .pin = 3, .dt_flags = 0};

	gpio_pin_configure_dt(&spec, GPIO_OUTPUT_ACTIVE);

	/* Active-high: active = physical HIGH (no swap) */
	TEST_ASSERT_BITS(GPIO_OUTPUT_INIT_HIGH, GPIO_OUTPUT_INIT_HIGH, last_configure_flags);
	TEST_ASSERT_BITS_LOW(GPIO_OUTPUT_INIT_LOW, last_configure_flags);
}

static void test_gpio_active_low_stripped_from_driver_flags(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {
		.port = &mock_gpio, .pin = 1, .dt_flags = GPIO_ACTIVE_LOW};

	gpio_pin_configure_dt(&spec, GPIO_OUTPUT);

	/* ACTIVE_LOW must not reach the driver */
	TEST_ASSERT_BITS_LOW(GPIO_ACTIVE_LOW, last_configure_flags);
	TEST_ASSERT_BITS(GPIO_OUTPUT, GPIO_OUTPUT, last_configure_flags);
}

static void test_gpio_set_dt_inverts_for_active_low(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {
		.port = &mock_gpio, .pin = 4, .dt_flags = GPIO_ACTIVE_LOW};

	gpio_pin_set_dt(&spec, 1);
	TEST_ASSERT_EQUAL(0, last_set_raw_value);

	gpio_pin_set_dt(&spec, 0);
	TEST_ASSERT_EQUAL(1, last_set_raw_value);
}

static void test_gpio_set_dt_no_invert_active_high(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {.port = &mock_gpio, .pin = 4, .dt_flags = 0};

	gpio_pin_set_dt(&spec, 1);
	TEST_ASSERT_EQUAL(1, last_set_raw_value);

	gpio_pin_set_dt(&spec, 0);
	TEST_ASSERT_EQUAL(0, last_set_raw_value);
}

static void test_gpio_get_dt_inverts_for_active_low(void)
{
	reset_mock();
	const struct gpio_dt_spec spec = {
		.port = &mock_gpio, .pin = 4, .dt_flags = GPIO_ACTIVE_LOW};

	/* mock_pin_get_raw returns 0 → active-low inverts to 1 */
	int val = gpio_pin_get_dt(&spec);
	TEST_ASSERT_EQUAL(1, val);
}

/* ----------------------------------------------------------------
 * Test group
 * ---------------------------------------------------------------- */

void test_gpio_flags_group(void)
{
	RUN_TEST(test_gpio_output_basic);
	RUN_TEST(test_gpio_output_init_low);
	RUN_TEST(test_gpio_output_init_high);
	RUN_TEST(test_gpio_output_inactive_active_high);
	RUN_TEST(test_gpio_output_inactive_active_low);
	RUN_TEST(test_gpio_output_active_active_low);
	RUN_TEST(test_gpio_output_active_active_high);
	RUN_TEST(test_gpio_active_low_stripped_from_driver_flags);
	RUN_TEST(test_gpio_set_dt_inverts_for_active_low);
	RUN_TEST(test_gpio_set_dt_no_invert_active_high);
	RUN_TEST(test_gpio_get_dt_inverts_for_active_low);
}
