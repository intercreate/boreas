/**
 * @file amp_api.h
 * @brief Zephyr-inspired amplifier driver class interface.
 */
#pragma once

#include "device_model.h"

struct amp_api {
    esp_err_t (*set_gain_dB)(const struct device *dev, int8_t dB);
    esp_err_t (*get_status)(const struct device *dev, uint8_t *data, size_t len);
};

static inline esp_err_t amp_set_gain_dB(const struct device *dev, int8_t dB)
{
    __ASSERT(device_is_ready(dev), "amp_set_gain_dB: device not ready");
    const struct amp_api *api = dev->api;
    return api->set_gain_dB(dev, dB);
}

static inline esp_err_t amp_get_status(const struct device *dev, uint8_t *data, size_t len)
{
    __ASSERT(device_is_ready(dev), "amp_get_status: device not ready");
    const struct amp_api *api = dev->api;
    return api->get_status(dev, data, len);
}
