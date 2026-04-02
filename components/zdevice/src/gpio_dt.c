#include "gpio_dt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define TAG "gpio_esp32"

static esp_err_t gpio_esp32_pin_configure(const struct device *port, gpio_num_t pin, uint32_t flags)
{
    (void)port;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = (flags & GPIO_DT_OUTPUT) ? GPIO_MODE_INPUT_OUTPUT : GPIO_MODE_INPUT,
        .pull_up_en = (flags & GPIO_DT_PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (flags & GPIO_DT_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
}

static esp_err_t gpio_esp32_pin_set_raw(const struct device *port, gpio_num_t pin, int value)
{
    (void)port;
    return gpio_set_level(pin, value);
}

static int gpio_esp32_pin_get_raw(const struct device *port, gpio_num_t pin)
{
    (void)port;
    return gpio_get_level(pin);
}

static DRAM_ATTR portMUX_TYPE gpio_toggle_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t gpio_esp32_pin_toggle(const struct device *port, gpio_num_t pin)
{
    (void)port;
    portENTER_CRITICAL(&gpio_toggle_lock);
    int current = gpio_get_level(pin);
    esp_err_t ret = gpio_set_level(pin, !current);
    portEXIT_CRITICAL(&gpio_toggle_lock);
    return ret;
}

esp_err_t gpio_esp32_init(const struct device *dev)
{
    (void)dev;

    /* Install the shared GPIO ISR dispatcher. All devices that use
     * gpio_isr_handler_add() depend on this -- doing it here guarantees
     * the service is ready before any peripheral device_init() runs. */
    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}

const struct gpio_api gpio_esp32_api = {
    .pin_configure = gpio_esp32_pin_configure,
    .pin_set_raw = gpio_esp32_pin_set_raw,
    .pin_get_raw = gpio_esp32_pin_get_raw,
    .pin_toggle = gpio_esp32_pin_toggle,
};

_Static_assert(sizeof(gpio_esp32_api) == sizeof(struct gpio_api),
               "gpio_esp32_api size mismatch -- struct gpio_api may have new fields");
