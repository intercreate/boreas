#include "gpio_dt.h"
#include "zsys/log.h"
#include "freertos/FreeRTOS.h"

LOG_MODULE_REGISTER(gpio_esp32, LOG_LEVEL_INF);

/* ----------------------------------------------------------------
 * Driver-private data (singleton -- one GPIO controller on ESP32)
 * ---------------------------------------------------------------- */

struct gpio_esp32_data {
    struct gpio_callback *callbacks; /* singly-linked list head */
    portMUX_TYPE lock;               /* protects callback list */
};

static struct gpio_esp32_data gpio0_data = {
    .callbacks = NULL,
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

/* File-scoped pointer to the port device -- set during init so the ISR
 * can find the device without per-pin arg packing. Safe because ESP32
 * has a single GPIO controller. */
static const struct device *gpio_esp32_port;

/* ----------------------------------------------------------------
 * ISR dispatcher -- runs in IRAM
 * ---------------------------------------------------------------- */

static void IRAM_ATTR gpio_esp32_isr(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(uintptr_t)arg;
    gpio_port_pins_t pin_bit = (uint64_t)1 << pin;

    /* Walk callback list -- no lock needed in ISR because list mutations
     * happen under portENTER_CRITICAL which masks interrupts on this core */
    struct gpio_callback *cb = gpio0_data.callbacks;
    while (cb != NULL) {
        if (cb->pin_mask & pin_bit) {
            cb->handler(gpio_esp32_port, cb, pin_bit);
        }
        cb = cb->next;
    }
}

/* ----------------------------------------------------------------
 * Basic pin I/O
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * Interrupt configuration
 * ---------------------------------------------------------------- */

static esp_err_t gpio_esp32_pin_interrupt_configure(const struct device *port,
                                                     gpio_num_t pin,
                                                     uint32_t flags)
{
    (void)port;

    if (!(flags & GPIO_INT_ENABLE)) {
        gpio_isr_handler_remove(pin);
        return gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
    }

    gpio_int_type_t intr_type;
    if (flags & GPIO_INT_EDGE) {
        bool low = flags & GPIO_INT_LOW_0;
        bool high = flags & GPIO_INT_HIGH_1;
        if (low && high) {
            intr_type = GPIO_INTR_ANYEDGE;
        } else if (high) {
            intr_type = GPIO_INTR_POSEDGE;
        } else if (low) {
            intr_type = GPIO_INTR_NEGEDGE;
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (flags & GPIO_INT_HIGH_1) {
        intr_type = GPIO_INTR_HIGH_LEVEL;
    } else if (flags & GPIO_INT_LOW_0) {
        intr_type = GPIO_INTR_LOW_LEVEL;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = gpio_set_intr_type(pin, intr_type);
    if (ret != ESP_OK) {
        return ret;
    }

    return gpio_isr_handler_add(pin, gpio_esp32_isr, (void *)(uintptr_t)pin);
}

/* ----------------------------------------------------------------
 * Callback list management
 * ---------------------------------------------------------------- */

static esp_err_t gpio_esp32_manage_callback(const struct device *port,
                                             struct gpio_callback *cb,
                                             bool set)
{
    (void)port;

    portENTER_CRITICAL(&gpio0_data.lock);

    if (set) {
        /* Add to head of list (check for duplicates) */
        struct gpio_callback *cur = gpio0_data.callbacks;
        while (cur != NULL) {
            if (cur == cb) {
                portEXIT_CRITICAL(&gpio0_data.lock);
                return ESP_OK; /* already registered */
            }
            cur = cur->next;
        }
        cb->next = gpio0_data.callbacks;
        gpio0_data.callbacks = cb;
    } else {
        /* Remove from list */
        struct gpio_callback **pp = &gpio0_data.callbacks;
        while (*pp != NULL) {
            if (*pp == cb) {
                *pp = cb->next;
                cb->next = NULL;
                break;
            }
            pp = &(*pp)->next;
        }
    }

    portEXIT_CRITICAL(&gpio0_data.lock);
    return ESP_OK;
}

/* ----------------------------------------------------------------
 * Init + vtable
 * ---------------------------------------------------------------- */

esp_err_t gpio_esp32_init(const struct device *dev)
{
    /* Store port device pointer for ISR dispatcher */
    gpio_esp32_port = dev;

    /* Install the shared GPIO ISR dispatcher. All devices that use
     * gpio_isr_handler_add() depend on this -- doing it here guarantees
     * the service is ready before any peripheral device_init() runs. */
    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    LOG_INF("initialized");
    return ESP_OK;
}

const struct gpio_api gpio_esp32_api = {
    .pin_configure = gpio_esp32_pin_configure,
    .pin_set_raw = gpio_esp32_pin_set_raw,
    .pin_get_raw = gpio_esp32_pin_get_raw,
    .pin_toggle = gpio_esp32_pin_toggle,
    .pin_interrupt_configure = gpio_esp32_pin_interrupt_configure,
    .manage_callback = gpio_esp32_manage_callback,
};

_Static_assert(sizeof(gpio_esp32_api) == sizeof(struct gpio_api),
               "gpio_esp32_api size mismatch -- struct gpio_api may have new fields");
