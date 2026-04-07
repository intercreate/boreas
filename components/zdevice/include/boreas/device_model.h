/**
 * @file device_model.h
 * @brief Zephyr-inspired device model for hardware abstraction.
 *
 * Provides a common struct device and lifecycle so that drivers are
 * decoupled from specific bus instances and pin assignments.
 */
#pragma once

#include <stdbool.h>
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

#define DEVICE_DEFINE(_name, _init, _api, _config, _data, _bus) \
    static bool _name##_ready;                                   \
    static const struct device _name = {                         \
        .name   = #_name,                                        \
        .bus    = (_bus),                                         \
        .api    = (_api),                                        \
        .config = (_config),                                     \
        .data   = (_data),                                       \
        .ready  = &_name##_ready,                                \
        .init   = (_init),                                       \
    }
