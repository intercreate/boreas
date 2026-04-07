/**
 * @file button_input_api.h
 * @brief Zephyr-inspired button/keypad input driver class interface.
 *
 */
#pragma once

#include <sys/queue.h>
#include "device_model.h"

/**
 * Callback invoked when button state changes.
 *
 * @param dev       The button input device that generated the event
 * @param state     Current raw state (bit per input; 1 = released, 0 = pressed)
 * @param changed   XOR mask of bits that changed since last report
 * @param user_data Opaque context from subscriber registration
 */
typedef void (*button_input_cb_t)(const struct device *dev,
                                  uint32_t state, uint32_t changed,
                                  void *user_data);

struct button_input_entry {
    SLIST_ENTRY(button_input_entry) node;
    button_input_cb_t cb;
    void *user_data;
};

struct button_input_api {
    esp_err_t (*subscribe)(const struct device *dev,
                           struct button_input_entry *entry);
    esp_err_t (*get_state)(const struct device *dev, uint32_t *state);
};

static inline esp_err_t button_input_subscribe(const struct device *dev,
                                               struct button_input_entry *entry)
{
    __ASSERT(device_is_ready(dev), "button_input_subscribe: device not ready");
    const struct button_input_api *api = dev->api;
    return api->subscribe(dev, entry);
}

static inline esp_err_t button_input_get_state(const struct device *dev,
                                               uint32_t *state)
{
    __ASSERT(device_is_ready(dev), "button_input_get_state: device not ready");
    const struct button_input_api *api = dev->api;
    return api->get_state(dev, state);
}
