#include "device_model.h"
#include "esp_log.h"
#include "zephyr/sys/util.h"

static const char *TAG = "device_model";

esp_err_t device_init(const struct device *dev)
{
    __ASSERT(dev != NULL, "device_init: NULL device");
    __ASSERT(dev->init != NULL, "device_init: no init function");
    __ASSERT(dev->ready != NULL, "device_init: no ready flag");

    ESP_LOGI(TAG, "init: %s", dev->name);
    esp_err_t ret = dev->init(dev);
    if (ret == ESP_OK) {
        *dev->ready = true;
    } else {
        ESP_LOGE(TAG, "init failed: %s (0x%x)", dev->name, ret);
    }
    return ret;
}
