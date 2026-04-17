# zdevice

Lightweight device model inspired by Zephyr's driver patterns, designed for ESP-IDF. No devicetree -- devices are declared in C code with `DEVICE_DEFINE` and bus specs (`gpio_dt_spec`, `i2c_dt_spec`, `spi_dt_spec`).

## Device core

```c
#include "device_model.h"

// Define a device with an init function and API vtable
DEVICE_DEFINE(my_sensor, sensor_init, &sensor_api, &sensor_config, &sensor_data, NULL);

// Initialize (call in correct parent-first order)
esp_err_t err = device_init(&my_sensor);

// Check readiness
if (device_is_ready(&my_sensor)) { ... }
```

`DEVICE_DEFINE` creates a static `struct device` with an auto-generated ready flag. `device_init()` calls the init function and sets the ready flag on success.

> **Where to put `DEVICE_DEFINE`:** instantiate devices in `main/` (e.g. a `board_devices.c` -- the DTS-equivalent) or an equivalently whole-archived TU. Registration uses `__attribute__((constructor))`, which ESP-IDF's linker strips from component static libraries when the object has no other externally-referenced symbols. Centralizing device definitions in `main/` avoids this and mirrors Zephyr's "board defines devices, drivers don't instantiate" pattern.

## GPIO (`gpio_dt_spec`)

Requires `CONFIG_DEVICE_MODEL_GPIO=y`.

```c
#include "gpio_dt.h"

// Define GPIO controller device
DEVICE_DEFINE(gpio0, gpio_esp32_init, &gpio_esp32_api, NULL, NULL, NULL);

// Pin spec -- ties a pin to a controller with flags
static const struct gpio_dt_spec led = {
    .port = &gpio0,
    .pin = GPIO_NUM_2,
    .dt_flags = 0,  // or GPIO_DT_ACTIVE_LOW
};

device_init(&gpio0);
gpio_pin_configure_dt(&led, GPIO_DT_OUTPUT);
gpio_pin_set_dt(&led, 1);      // respects ACTIVE_LOW
gpio_pin_toggle_dt(&led);      // atomic (spinlock-protected)
int val = gpio_pin_get_dt(&led);  // respects ACTIVE_LOW
```

**Flags:** `GPIO_DT_INPUT`, `GPIO_DT_OUTPUT`, `GPIO_DT_PULL_UP`, `GPIO_DT_PULL_DOWN`, `GPIO_DT_ACTIVE_LOW`.

**Not yet implemented:** GPIO interrupts/callbacks (Phase 8).

## I2C (`i2c_dt_spec`)

Requires `CONFIG_DEVICE_MODEL_I2C=y`.

```c
#include "i2c_dt.h"

// Bus device (one per I2C port)
static struct i2c_bus_data i2c0_data;
static const struct i2c_bus_config i2c0_cfg = {
    .port = I2C_NUM_0,
    .sda_gpio = GPIO_NUM_21,
    .scl_gpio = GPIO_NUM_22,
    .glitch_ignore_cnt = 7,
    .enable_internal_pullup = true,
};
DEVICE_DEFINE(i2c0, i2c_bus_init, NULL, &i2c0_cfg, &i2c0_data, NULL);

// Peripheral spec
static const struct i2c_dt_spec sensor = {
    .bus = &i2c0,
    .addr = 0x68,
    .scl_speed_hz = 400000,
};

device_init(&i2c0);
i2c_dt_attach(&sensor);

// I/O
i2c_write_dt(&sensor, buf, len);
i2c_read_dt(&sensor, buf, len);
i2c_write_read_dt(&sensor, tx, tx_len, rx, rx_len);

// Register helpers
i2c_reg_write_byte_dt(&sensor, 0x1A, 0x03);
uint8_t val;
i2c_reg_read_byte_dt(&sensor, 0x75, &val);

// Burst
i2c_burst_write_dt(&sensor, 0x00, data, 16);  // max 32 bytes
i2c_burst_read_dt(&sensor, 0x00, data, 6);
```

Bus reuse: if an I2C port is already initialized elsewhere, `i2c_bus_init` attaches to the existing bus handle.

## SPI (`spi_dt_spec`)

Requires `CONFIG_DEVICE_MODEL_SPI=y`.

```c
#include "spi_dt.h"

// Bus device
static struct spi_bus_data spi2_data;
static const struct spi_bus_config spi2_cfg = {
    .host = SPI2_HOST,
    .mosi_gpio = GPIO_NUM_11,
    .miso_gpio = GPIO_NUM_13,
    .sclk_gpio = GPIO_NUM_12,
    .max_transfer_sz = 4096,
};
DEVICE_DEFINE(spi2, spi_bus_init, NULL, &spi2_cfg, &spi2_data, NULL);

// Peripheral spec
static const struct spi_dt_spec display = {
    .bus = &spi2,
    .cs_gpio = GPIO_NUM_10,
    .clock_speed_hz = 10000000,
    .mode = 0,
    .command_bits = 8,
    .address_bits = 0,
};

device_init(&spi2);
spi_dt_attach(&display);

spi_write_dt(&display, cmd, data, len);
spi_read_dt(&display, cmd, data, len);
spi_transceive_dt(&display, cmd, tx, rx, len);
```

Up to 4 devices per SPI bus, identified by CS pin.

## Error codes

All device APIs return `esp_err_t` (not negative errno). This is intentional -- the device model wraps ESP-IDF drivers directly, and the consumers are ESP-IDF application code that already thinks in `esp_err_t`. Converting to errno would add a translation layer at every call with no practical benefit. The kernel primitives (k_sem, k_mutex, etc.) use negative errno because they wrap FreeRTOS APIs that don't have their own error type, and Zephyr application code expects errno. The device model may switch to errno if Zephyr driver consumer porting becomes a goal.

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_DEVICE_MODEL_GPIO` | n | Enable GPIO controller driver |
| `CONFIG_DEVICE_MODEL_I2C` | n | Enable I2C bus driver |
| `CONFIG_DEVICE_MODEL_SPI` | n | Enable SPI bus driver |
