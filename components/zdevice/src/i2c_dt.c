#include "i2c_dt.h"
#include "zsys/log.h"
#include "zephyr/sys/util.h"
#include <string.h>

LOG_MODULE_REGISTER(i2c_dt, LOG_LEVEL_INF);

#define I2C_BURST_WRITE_MAX 32

esp_err_t i2c_bus_init(const struct device *dev)
{
    const struct i2c_bus_config *cfg = dev->config;
    struct i2c_bus_data *data = dev->data;

    data->device_count = 0;

    /* If the bus already exists (created by another subsystem), reuse it */
    if (i2c_master_get_bus_handle(cfg->port, &data->bus_handle) == ESP_OK) {
        LOG_INF("reusing existing I2C bus on port %d", cfg->port);
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = cfg->port,
        .scl_io_num = cfg->scl_gpio,
        .sda_io_num = cfg->sda_gpio,
        .glitch_ignore_cnt = cfg->glitch_ignore_cnt,
        .flags.enable_internal_pullup = cfg->enable_internal_pullup,
    };
    return i2c_new_master_bus(&bus_cfg, &data->bus_handle);
}

static i2c_master_dev_handle_t i2c_dt_resolve(const struct i2c_dt_spec *spec)
{
    struct i2c_bus_data *bus = spec->bus->data;

    for (int i = 0; i < bus->device_count; i++) {
        if (bus->devices[i].addr == spec->addr) {
            return bus->devices[i].handle;
        }
    }
    return NULL;
}

esp_err_t i2c_dt_attach(const struct i2c_dt_spec *spec)
{
    struct i2c_bus_data *bus = spec->bus->data;

    __ASSERT(i2c_dt_resolve(spec) == NULL, "i2c_dt_attach: address already attached");

    if (bus->device_count >= I2C_DT_MAX_DEVICES) {
        LOG_ERR("bus full: cannot attach addr 0x%02x", spec->addr);
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = spec->addr,
        .scl_speed_hz = spec->scl_speed_hz,
    };

    i2c_master_dev_handle_t handle;
    esp_err_t ret = i2c_master_bus_add_device(bus->bus_handle, &dev_cfg, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    bus->devices[bus->device_count].addr = spec->addr;
    bus->devices[bus->device_count].handle = handle;
    bus->device_count++;
    return ESP_OK;
}

esp_err_t i2c_write_dt(const struct i2c_dt_spec *spec, const uint8_t *buf, size_t len)
{
    i2c_master_dev_handle_t handle = i2c_dt_resolve(spec);
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(handle, buf, len, I2C_DT_TIMEOUT_MS);
}

esp_err_t i2c_read_dt(const struct i2c_dt_spec *spec, uint8_t *buf, size_t len)
{
    i2c_master_dev_handle_t handle = i2c_dt_resolve(spec);
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_receive(handle, buf, len, I2C_DT_TIMEOUT_MS);
}

esp_err_t i2c_write_read_dt(const struct i2c_dt_spec *spec, const uint8_t *write_buf, size_t num_write,
                            uint8_t *read_buf, size_t num_read)
{
    i2c_master_dev_handle_t handle = i2c_dt_resolve(spec);
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(handle, write_buf, num_write, read_buf, num_read, I2C_DT_TIMEOUT_MS);
}

esp_err_t i2c_reg_write_byte_dt(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {reg, value};
    return i2c_write_dt(spec, tx, 2);
}

esp_err_t i2c_reg_read_byte_dt(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t *value)
{
    return i2c_write_read_dt(spec, &reg, 1, value, 1);
}

esp_err_t i2c_burst_write_dt(const struct i2c_dt_spec *spec, uint8_t reg, const uint8_t *buf, size_t len)
{
    if (len > I2C_BURST_WRITE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[1 + I2C_BURST_WRITE_MAX];
    tx[0] = reg;
    if (len > 0) {
        memcpy(&tx[1], buf, len);
    }
    return i2c_write_dt(spec, tx, 1 + len);
}

esp_err_t i2c_burst_read_dt(const struct i2c_dt_spec *spec, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_write_read_dt(spec, &reg, 1, buf, len);
}
