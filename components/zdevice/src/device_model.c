#include "device_model.h"
#include "zsys/log.h"
#include "zephyr/sys/util.h"

LOG_MODULE_REGISTER(device_model, LOG_LEVEL_INF);

esp_err_t device_init(const struct device *dev)
{
    __ASSERT(dev != NULL, "device_init: NULL device");
    __ASSERT(dev->init != NULL, "device_init: no init function");
    __ASSERT(dev->ready != NULL, "device_init: no ready flag");

    LOG_INF("init: %s", dev->name);
    esp_err_t ret = dev->init(dev);
    if (ret == ESP_OK) {
        *dev->ready = true;
    } else {
        LOG_ERR("init failed: %s (0x%x)", dev->name, ret);
    }
    return ret;
}
