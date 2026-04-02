#include "spi_dt.h"
#include "esp_log.h"
#include <string.h>

#define TAG "spi_dt"

esp_err_t spi_bus_init(const struct device *dev)
{
    const struct spi_bus_config *cfg = dev->config;
    struct spi_bus_data *data = dev->data;

    data->device_count = 0;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = cfg->mosi_gpio,
        .miso_io_num = cfg->miso_gpio,
        .sclk_io_num = cfg->sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = cfg->max_transfer_sz,
    };

    return spi_bus_initialize(cfg->host, &bus_cfg, SPI_DMA_CH_AUTO);
}

static spi_device_handle_t spi_dt_resolve(const struct spi_dt_spec *spec)
{
    struct spi_bus_data *bus = spec->bus->data;

    for (int i = 0; i < bus->device_count; i++) {
        if (bus->devices[i].cs_gpio == spec->cs_gpio) {
            return bus->devices[i].handle;
        }
    }
    return NULL;
}

esp_err_t spi_dt_attach(const struct spi_dt_spec *spec)
{
    const struct spi_bus_config *bus_cfg = spec->bus->config;
    struct spi_bus_data *bus = spec->bus->data;

    if (bus->device_count >= SPI_DT_MAX_DEVICES) {
        ESP_LOGE(TAG, "bus full: cannot attach cs=%d", spec->cs_gpio);
        return ESP_ERR_NO_MEM;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = spec->clock_speed_hz,
        .mode = spec->mode,
        .spics_io_num = spec->cs_gpio,
        .queue_size = 1,
        .command_bits = spec->command_bits,
        .address_bits = spec->address_bits,
    };

    spi_device_handle_t handle;
    esp_err_t ret = spi_bus_add_device(bus_cfg->host, &dev_cfg, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    bus->devices[bus->device_count].cs_gpio = spec->cs_gpio;
    bus->devices[bus->device_count].handle = handle;
    bus->device_count++;
    return ESP_OK;
}

esp_err_t spi_write_dt(const struct spi_dt_spec *spec, uint16_t cmd, uint64_t addr, const uint8_t *buf, size_t len)
{
    spi_device_handle_t handle = spi_dt_resolve(spec);
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t txn = {
        .cmd = cmd,
        .addr = addr,
        .length = len * 8,
        .tx_buffer = buf,
    };
    return spi_device_transmit(handle, &txn);
}

esp_err_t spi_read_dt(const struct spi_dt_spec *spec, uint16_t cmd, uint64_t addr, uint8_t *buf, size_t len)
{
    spi_device_handle_t handle = spi_dt_resolve(spec);
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t txn = {
        .cmd = cmd,
        .addr = addr,
        .length = len * 8, /* full-duplex: length sets clock cycles, tx_buffer=NULL clocks zeros */
        .rx_buffer = buf,
    };
    return spi_device_transmit(handle, &txn);
}

esp_err_t spi_transceive_dt(const struct spi_dt_spec *spec, uint16_t cmd, uint64_t addr, const uint8_t *tx_buf,
                            uint8_t *rx_buf, size_t len)
{
    spi_device_handle_t handle = spi_dt_resolve(spec);
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t txn = {
        .cmd = cmd,
        .addr = addr,
        .length = len * 8,
        .rxlength = len * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    return spi_device_transmit(handle, &txn);
}
