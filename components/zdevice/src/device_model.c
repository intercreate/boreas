#include "device_model.h"
#include "zsys/log.h"
#include "zephyr/sys/util.h"

LOG_MODULE_REGISTER(device_model, LOG_LEVEL_INF);

/* Global device registry -- populated by DEVICE_DEFINE constructors */
const struct device *_device_registry[CONFIG_DEVICE_REGISTRY_MAX];
size_t _device_count = 0;

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

const struct device *device_get_binding(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < _device_count; i++) {
        if (strcmp(_device_registry[i]->name, name) == 0) {
            return _device_registry[i];
        }
    }
    return NULL;
}

size_t device_get_count(void)
{
    return _device_count;
}

const struct device *device_get_by_index(size_t idx)
{
    if (idx >= _device_count) {
        return NULL;
    }
    return _device_registry[idx];
}
