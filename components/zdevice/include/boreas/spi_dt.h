/**
 * @file spi_dt.h
 */
#pragma once

#include "device_model.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#define SPI_DT_MAX_DEVICES 4

struct spi_bus_config {
	spi_host_device_t host;
	gpio_num_t mosi_gpio;
	gpio_num_t miso_gpio;
	gpio_num_t sclk_gpio;
	int max_transfer_sz;
};

struct spi_bus_data {
	struct {
		gpio_num_t cs_gpio;
		spi_device_handle_t handle;
	} devices[SPI_DT_MAX_DEVICES];
	uint8_t device_count;
};

struct spi_dt_spec {
	const struct device *bus;
	gpio_num_t cs_gpio;
	uint32_t clock_speed_hz;
	uint8_t mode;
	uint8_t command_bits;
	uint8_t address_bits;
};

esp_err_t spi_bus_init(const struct device *dev);

esp_err_t spi_dt_attach(const struct spi_dt_spec *spec);

esp_err_t spi_write_dt(const struct spi_dt_spec *spec, uint16_t cmd, uint64_t addr,
		       const uint8_t *buf, size_t len);

esp_err_t spi_read_dt(const struct spi_dt_spec *spec, uint16_t cmd, uint64_t addr, uint8_t *buf,
		      size_t len);

esp_err_t spi_transceive_dt(const struct spi_dt_spec *spec, uint16_t cmd, uint64_t addr,
			    const uint8_t *tx_buf, uint8_t *rx_buf, size_t len);
