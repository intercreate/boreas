# Boreas

[![CI](https://github.com/intercreate/boreas/actions/workflows/ci.yml/badge.svg)](https://github.com/intercreate/boreas/actions/workflows/ci.yml)

Zephyr-compatible kernel APIs and device model for ESP-IDF.

Boreas provides a thin, tested abstraction layer that brings Zephyr RTOS API conventions to ESP-IDF projects running on FreeRTOS. Named for the Greek god of the north wind -- a sibling to Zephyr (west wind).

## Components

| Component | Purpose | Dependencies |
|-----------|---------|-------------|
| **zkernel** | Kernel primitives: `k_timer`, `k_work`, `k_sem`, `k_mutex`, `k_msgq`, `k_event`, `k_thread`, `sys_slist`, `sys_dlist`, timeout API, init framework | ESP-IDF (freertos, esp_timer, log) |
| **zdevice** | Zephyr-inspired device model: `device_t`, `DEVICE_DEFINE`, I2C/SPI/GPIO bus specs, driver class vtables | zkernel, ESP-IDF (driver) |
| **zsys** | System services: per-module log levels, thread analyzer, subsystem watchdog, retry/backoff | zkernel |

## Integration

Boreas is a collection of ESP-IDF components. Integration is four steps: add the
source, point ESP-IDF at it, apply the required config, then depend on the
components you use.

### 1. Add Boreas to your project

As a git submodule (it can live anywhere; `components/boreas` is a common
choice):

```bash
git submodule add https://github.com/intercreate/boreas.git components/boreas
git submodule update --init --recursive
```

Anyone who later clones your project fetches Boreas with
`git submodule update --init --recursive` (or `git clone --recursive`).

### 2. Wire it into your top-level `CMakeLists.txt`

Point `EXTRA_COMPONENT_DIRS` at Boreas's inner `components/` directory, and add
`sdkconfig.boreas` to `SDKCONFIG_DEFAULTS`. Both must be set **before** the
`project.cmake` include:

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components/boreas/components)
# Append to your existing defaults list — don't overwrite it:
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;components/boreas/sdkconfig.boreas")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_app)
```

### 3. Regenerate your configuration once

ESP-IDF only reads `SDKCONFIG_DEFAULTS` when generating a fresh `sdkconfig`, so a
stale one must be removed for Boreas's defaults to take effect:

```bash
rm -rf build sdkconfig && idf.py set-target <target>   # e.g. esp32s3
```

`sdkconfig.boreas` carries the one setting Boreas cannot apply from a component
(`CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES=2`; rationale is inline in the
file). If it is missing, the build stops with a compile-time `#error` that names
the file — that is the reminder to finish steps 2–3, not a bug.

### 4. Depend on the components you use

In the `CMakeLists.txt` of each component that calls Boreas (e.g. `main`), list
the Boreas components it uses in `REQUIRES` (or `PRIV_REQUIRES`):

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES zkernel zdevice zsys   # add zshell if you use the shell
)
```

Then include headers under the `boreas/` prefix:

```c
#include <boreas/zephyr/kernel.h>    // k_timer, k_work, k_sem, k_mutex, ...
#include <boreas/zephyr/sys/util.h>  // BIT, CLAMP, ROUND_UP, ...
#include <boreas/zephyr/sys/slist.h> // sys_slist_*
#include <boreas/zephyr/init.h>      // SYS_INIT
#include <boreas/device_model.h>     // device_t, DEVICE_DEFINE
#include <boreas/i2c_dt.h>           // i2c_dt_spec, i2c_write_dt
#include <boreas/zsys/log.h>         // LOG_MODULE_REGISTER, LOG_INF, ...
```

See `examples/` for complete, buildable projects using this layout.

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

## Documentation

- Per-component usage in each component's `README.md` (`components/<name>/README.md`).
- `docs/linker-section-registration.md` — how `SYS_INIT`, `LOG_MODULE_REGISTER`,
  `LOG_BACKEND_DEFINE`, and `SHELL_CMD_REGISTER` work under ESP-IDF's
  archive-stripping rules, including the constraint that callsites must live in
  `main/` (or any TU with an externally-referenced symbol).

## License

Apache 2.0. Copyright 2026 Intercreate.

## Attribution

Kernel API surface derived from [Zephyr RTOS](https://github.com/zephyrproject-rtos/zephyr) (Apache 2.0).
Device model developed for ESP32-S3 embedded firmware.
