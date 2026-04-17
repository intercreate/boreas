/**
 * @file device_model.h
 * @brief Zephyr-inspired device model for hardware abstraction.
 *
 * Provides a common struct device and lifecycle so that drivers are
 * decoupled from specific bus instances and pin assignments.
 *
 * Devices declared with DEVICE_DEFINE auto-register into a global
 * registry via constructor, enabling runtime discovery with
 * device_get_binding().
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_err.h"

struct device {
    const char *name;
    const struct device *bus; /* parent bus device, NULL for bus controllers */
    const void *api;          /* driver class vtable */
    const void *config;       /* driver-specific static config (const) */
    void *data;               /* driver-specific runtime state (mutable) */
    bool *ready;              /* set true by device_init() on success */
    esp_err_t (*init)(const struct device *dev);
};

/* Global device registry -- populated by DEVICE_DEFINE constructors */
extern const struct device *_device_registry[];
extern size_t _device_count;

/**
 * Initialize a device by calling its init function.
 * Sets dev->ready on success. Caller must ensure the parent bus
 * device is already initialized.
 */
esp_err_t device_init(const struct device *dev);

static inline bool device_is_ready(const struct device *dev)
{
    return (dev != NULL) && (dev->ready != NULL) && (*dev->ready);
}

/** Look up a device by name. Returns NULL if not found. */
const struct device *device_get_binding(const char *name);

/** Return the number of registered devices. */
size_t device_get_count(void);

/** Return device at index, or NULL if out of range. */
const struct device *device_get_by_index(size_t idx);

/**
 * Define and register a device instance.
 *
 * NOTE: Invoke this in `main/` (e.g. a `board_devices.c`) or another TU that
 * is linked whole into the final image. Registration relies on a
 * `__attribute__((constructor))`, which ESP-IDF's linker strips when a TU
 * inside a component static library has no other externally-referenced
 * symbols -- the device would silently not register.
 */
#define DEVICE_DEFINE(_name, _init, _api, _config, _data, _bus)    \
    static bool _name##_ready;                                      \
    static DRAM_ATTR const struct device _name = {                  \
        .name   = #_name,                                           \
        .bus    = (_bus),                                           \
        .api    = (_api),                                           \
        .config = (_config),                                       \
        .data   = (_data),                                         \
        .ready  = &_name##_ready,                                  \
        .init   = (_init),                                         \
    };                                                              \
    static void __attribute__((constructor))                        \
    _device_register_##_name(void) {                               \
        if (_device_count < CONFIG_DEVICE_REGISTRY_MAX) {          \
            _device_registry[_device_count++] = &_name;            \
        }                                                           \
    }
