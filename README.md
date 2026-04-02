# Boreas

Zephyr-compatible kernel APIs and device model for ESP-IDF.

Boreas provides a thin, tested abstraction layer that brings Zephyr RTOS API conventions to ESP-IDF projects running on FreeRTOS. Named for the Greek god of the north wind — a sibling to Zephyr (west wind).

## Components

| Component | Purpose | Dependencies |
|-----------|---------|-------------|
| **zkernel** | Kernel primitives: `k_timer`, `k_work`, `k_sem`, `k_mutex`, `k_msgq`, `k_event`, `k_thread`, `sys_slist`, `sys_dlist`, timeout API, init framework | ESP-IDF (freertos, esp_timer, log) |
| **zdevice** | Zephyr-inspired device model: `device_t`, `DEVICE_DEFINE`, I2C/SPI/GPIO bus specs, driver class vtables | zkernel, ESP-IDF (driver) |
| **zsys** | System services: per-module log levels, thread analyzer, subsystem watchdog, retry/backoff | zkernel |

## Usage

Add Boreas to your ESP-IDF project as a git submodule or local path:

```bash
# As submodule
git submodule add <repo-url> components/boreas

# In your top-level CMakeLists.txt
set(EXTRA_COMPONENT_DIRS components/boreas/components)
```

Then include headers:

```c
#include <zephyr/kernel.h>       // k_timer, k_work, k_sem, k_mutex, ...
#include <zephyr/sys/util.h>     // BIT, CLAMP, ROUND_UP, ...
#include <zephyr/sys/slist.h>    // sys_slist_*
#include <zephyr/init.h>         // SYS_INIT
#include <device_model.h>        // device_t, DEVICE_DEFINE
#include <i2c_dt.h>              // i2c_dt_spec, i2c_write_dt
```

## Testing

Tests run on ESP-IDF's linux target (no hardware required):

```bash
cd test
idf.py --preview set-target linux
idf.py build
./build/boreas_test.elf
```

Or on-device (ESP32-S3):

```bash
cd test
idf.py set-target esp32s3
idf.py build flash monitor
```

## License

Apache 2.0. Copyright 2026 Intercreate.

## Attribution

Kernel API surface derived from [Zephyr RTOS](https://github.com/zephyrproject-rtos/zephyr) (Apache 2.0).
Device model developed for ESP32-S3 embedded firmware.
