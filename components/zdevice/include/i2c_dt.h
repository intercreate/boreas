/**
 * @file i2c_dt.h
 * @brief I2C bus device types and convenience functions.
 *
 * ESP-IDF device handles are managed internally by the bus device.
 */
#pragma once

#include "device_model.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define I2C_DT_TIMEOUT_MS  1000
#define I2C_DT_MAX_DEVICES 8

struct i2c_bus_config {
    i2c_port_t port;
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    uint8_t glitch_ignore_cnt;
    bool enable_internal_pullup;
};

struct i2c_bus_data {
    i2c_master_bus_handle_t bus_handle;
    struct {
        uint16_t addr;
        i2c_master_dev_handle_t handle;
    } devices[I2C_DT_MAX_DEVICES];
    uint8_t device_count;
};

struct i2c_dt_spec {
    const struct device *bus;
    uint16_t addr;         /* 7-bit I2C address */
    uint32_t scl_speed_hz;
};

esp_err_t i2c_bus_init(const struct device *dev);
esp_err_t i2c_dt_attach(const struct i2c_dt_spec *spec);

esp_err_t i2c_write_dt(const struct i2c_dt_spec *spec, const uint8_t *buf, size_t len);
esp_err_t i2c_read_dt(const struct i2c_dt_spec *spec, uint8_t *buf, size_t len);
esp_err_t i2c_write_read_dt(const struct i2c_dt_spec *spec, const uint8_t *write_buf, size_t num_write,
                            uint8_t *read_buf, size_t num_read);

esp_err_t i2c_reg_write_byte_dt(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t value);
esp_err_t i2c_reg_read_byte_dt(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t *value);
esp_err_t i2c_burst_write_dt(const struct i2c_dt_spec *spec, uint8_t reg, const uint8_t *buf, size_t len);
esp_err_t i2c_burst_read_dt(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t *buf, size_t len);
