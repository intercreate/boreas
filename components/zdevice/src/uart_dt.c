#include "uart_dt.h"

#include <errno.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "zsys/log.h"

LOG_MODULE_REGISTER(uart_esp32, LOG_LEVEL_INF);

/* Step 1: polling-only skeleton.
 *
 * Backed by ESP-IDF driver/uart.h. No event queue, no IRQ callbacks,
 * no async TX/RX. Those vtable slots are left NULL; their helpers
 * return -ENOSYS. RS-485 support lands in a later step.
 */

static esp_err_t apply_config(uart_port_t port, const struct uart_config *cfg)
{
    uart_word_length_t wl;
    switch (cfg->data_bits) {
    case UART_CFG_DATA_BITS_5: wl = UART_DATA_5_BITS; break;
    case UART_CFG_DATA_BITS_6: wl = UART_DATA_6_BITS; break;
    case UART_CFG_DATA_BITS_7: wl = UART_DATA_7_BITS; break;
    case UART_CFG_DATA_BITS_8: wl = UART_DATA_8_BITS; break;
    default: return ESP_ERR_INVALID_ARG;
    }

    uart_parity_t par;
    switch (cfg->parity) {
    case UART_CFG_PARITY_NONE: par = UART_PARITY_DISABLE; break;
    case UART_CFG_PARITY_EVEN: par = UART_PARITY_EVEN;    break;
    case UART_CFG_PARITY_ODD:  par = UART_PARITY_ODD;     break;
    default: return ESP_ERR_INVALID_ARG;
    }

    uart_stop_bits_t sb;
    switch (cfg->stop_bits) {
    case UART_CFG_STOP_BITS_1:   sb = UART_STOP_BITS_1;   break;
    case UART_CFG_STOP_BITS_1_5: sb = UART_STOP_BITS_1_5; break;
    case UART_CFG_STOP_BITS_2:   sb = UART_STOP_BITS_2;   break;
    default: return ESP_ERR_INVALID_ARG;
    }

    uart_hw_flowcontrol_t fc;
    switch (cfg->flow_ctrl) {
    case UART_CFG_FLOW_CTRL_NONE:    fc = UART_HW_FLOWCTRL_DISABLE; break;
    case UART_CFG_FLOW_CTRL_RTS_CTS: fc = UART_HW_FLOWCTRL_CTS_RTS; break;
    case UART_CFG_FLOW_CTRL_RS485:   fc = UART_HW_FLOWCTRL_DISABLE; break; /* handled via uart_set_mode in step 4 */
    default: return ESP_ERR_INVALID_ARG;
    }

    uart_config_t ic = {
        .baud_rate  = (int)cfg->baudrate,
        .data_bits  = wl,
        .parity     = par,
        .stop_bits  = sb,
        .flow_ctrl  = fc,
        .source_clk = UART_SCLK_DEFAULT,
    };
    return uart_param_config(port, &ic);
}

static int uart_esp32_configure(const struct device *dev,
                                const struct uart_config *cfg)
{
    const struct uart_esp32_config *c = dev->config;
    esp_err_t err = apply_config(c->port, cfg);
    return (err == ESP_OK) ? 0 : -EIO;
}

static int uart_esp32_config_get(const struct device *dev,
                                 struct uart_config *cfg)
{
    const struct uart_esp32_config *c = dev->config;
    uart_port_t port = c->port;

    uint32_t baud = 0;
    if (uart_get_baudrate(port, &baud) != ESP_OK) return -EIO;

    uart_word_length_t wl;
    if (uart_get_word_length(port, &wl) != ESP_OK) return -EIO;

    uart_parity_t par;
    if (uart_get_parity(port, &par) != ESP_OK) return -EIO;

    uart_stop_bits_t sb;
    if (uart_get_stop_bits(port, &sb) != ESP_OK) return -EIO;

    uart_hw_flowcontrol_t fc;
    if (uart_get_hw_flow_ctrl(port, &fc) != ESP_OK) return -EIO;

    cfg->baudrate = baud;

    switch (wl) {
    case UART_DATA_5_BITS: cfg->data_bits = UART_CFG_DATA_BITS_5; break;
    case UART_DATA_6_BITS: cfg->data_bits = UART_CFG_DATA_BITS_6; break;
    case UART_DATA_7_BITS: cfg->data_bits = UART_CFG_DATA_BITS_7; break;
    case UART_DATA_8_BITS: cfg->data_bits = UART_CFG_DATA_BITS_8; break;
    default: return -EIO;
    }

    switch (par) {
    case UART_PARITY_DISABLE: cfg->parity = UART_CFG_PARITY_NONE; break;
    case UART_PARITY_EVEN:    cfg->parity = UART_CFG_PARITY_EVEN; break;
    case UART_PARITY_ODD:     cfg->parity = UART_CFG_PARITY_ODD;  break;
    default: return -EIO;
    }

    switch (sb) {
    case UART_STOP_BITS_1:   cfg->stop_bits = UART_CFG_STOP_BITS_1;   break;
    case UART_STOP_BITS_1_5: cfg->stop_bits = UART_CFG_STOP_BITS_1_5; break;
    case UART_STOP_BITS_2:   cfg->stop_bits = UART_CFG_STOP_BITS_2;   break;
    default: return -EIO;
    }

    cfg->flow_ctrl = (fc == UART_HW_FLOWCTRL_CTS_RTS) ? UART_CFG_FLOW_CTRL_RTS_CTS
                                                      : UART_CFG_FLOW_CTRL_NONE;
    return 0;
}

static int uart_esp32_err_check(const struct device *dev)
{
    struct uart_esp32_data *d = dev->data;
    portENTER_CRITICAL(&d->lock);
    uint32_t flags = d->err_flags;
    d->err_flags = 0;
    portEXIT_CRITICAL(&d->lock);
    return (int)flags;
}

/* ----------------------------------------------------------------
 * Polling I/O
 *
 * poll_in is non-blocking: returns 0 on byte read, -1 when none available.
 * poll_out is blocking: writes one byte synchronously.
 * ---------------------------------------------------------------- */

static int uart_esp32_poll_in(const struct device *dev, unsigned char *c)
{
    const struct uart_esp32_config *cfg = dev->config;
    int n = uart_read_bytes(cfg->port, c, 1, 0);
    return (n == 1) ? 0 : -1;
}

static void uart_esp32_poll_out(const struct device *dev, unsigned char c)
{
    const struct uart_esp32_config *cfg = dev->config;
    uart_write_bytes(cfg->port, &c, 1);
    uart_wait_tx_done(cfg->port, portMAX_DELAY);
}

/* ----------------------------------------------------------------
 * Init
 * ---------------------------------------------------------------- */

esp_err_t uart_esp32_init(const struct device *dev)
{
    const struct uart_esp32_config *cfg = dev->config;
    struct uart_esp32_data *d = dev->data;

    portMUX_TYPE init_lock = portMUX_INITIALIZER_UNLOCKED;
    d->lock = init_lock;
    d->err_flags = 0;

    if (!d->driver_installed) {
        esp_err_t err = uart_driver_install(cfg->port,
                                            (int)cfg->rx_buf_size,
                                            (int)cfg->tx_buf_size,
                                            0, NULL,
                                            cfg->intr_alloc_flags);
        if (err != ESP_OK) {
            LOG_ERR("uart_driver_install(port=%d) failed: %d",
                    cfg->port, err);
            return err;
        }
        d->driver_installed = true;
    }

    esp_err_t err = apply_config(cfg->port, &cfg->default_cfg);
    if (err != ESP_OK) {
        LOG_ERR("param_config failed: %d", err);
        return err;
    }

    err = uart_set_pin(cfg->port, cfg->tx_pin, cfg->rx_pin,
                       cfg->rts_pin, cfg->cts_pin);
    if (err != ESP_OK) {
        LOG_ERR("set_pin failed: %d", err);
        return err;
    }

    LOG_INF("port=%d tx=%d rx=%d baud=%u",
            cfg->port, cfg->tx_pin, cfg->rx_pin,
            (unsigned)cfg->default_cfg.baudrate);
    return ESP_OK;
}

const struct uart_api uart_esp32_api = {
    .configure   = uart_esp32_configure,
    .config_get  = uart_esp32_config_get,
    .err_check   = uart_esp32_err_check,
    .poll_in     = uart_esp32_poll_in,
    .poll_out    = uart_esp32_poll_out,
    /* IRQ and async slots left NULL -- helpers return -ENOSYS */
};
