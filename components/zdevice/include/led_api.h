/**
 * @file led_api.h
 * @brief Zephyr-inspired LED controller driver class interface.
 *
 * Operates at the raw channel level. Logical grouping (backlight, RGB indicator)
 * is a board-level concern handled by led_service.
 */
#pragma once

#include "device_model.h"
#include "esp_log.h"
#include "zephyr/sys/util.h"

struct led_driver_api {
    esp_err_t (*enable)(const struct device *dev);
    esp_err_t (*disable)(const struct device *dev);
    esp_err_t (*set_brightness)(const struct device *dev, uint32_t channel, uint8_t value);
    esp_err_t (*write_channels)(const struct device *dev, uint32_t start,
                                uint32_t count, const uint8_t *buf);
};

static inline esp_err_t led_on(const struct device *dev)
{
    const struct led_driver_api *api = dev->api;
    return api->enable(dev);
}

static inline esp_err_t led_off(const struct device *dev)
{
    const struct led_driver_api *api = dev->api;
    return api->disable(dev);
}

static inline esp_err_t led_set_brightness(const struct device *dev, uint32_t channel, uint8_t value)
{
    const struct led_driver_api *api = dev->api;
    return api->set_brightness(dev, channel, value);
}

static inline esp_err_t led_write_channels(const struct device *dev, uint32_t start,
                                           uint32_t count, const uint8_t *buf)
{
    const struct led_driver_api *api = dev->api;
    return api->write_channels(dev, start, count, buf);
}

static inline esp_err_t led_set_channel(const struct device *dev, uint32_t channel, uint8_t value)
{
    return led_write_channels(dev, channel, 1, &value);
}

#define LED_CHANNELS_MASK_MAX 32

/**
 * Set channels by bitmask: bits set get @p brightness, others get 0.
 * @param num_channels number of channels on the device (must be <= 32)
 */
static inline esp_err_t led_set_channels_masked(const struct device *dev,
                                                uint32_t num_channels,
                                                uint32_t mask, uint8_t brightness)
{
    __ASSERT(num_channels <= LED_CHANNELS_MASK_MAX,
             "led_set_channels_masked: num_channels exceeds mask width");
    uint8_t buf[LED_CHANNELS_MASK_MAX];
    for (uint32_t i = 0; i < num_channels; i++) {
        buf[i] = (mask & (1U << i)) ? brightness : 0;
    }
    return led_write_channels(dev, 0, num_channels, buf);
}
