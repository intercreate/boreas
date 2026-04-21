#include "uart_dt.h"

#include <errno.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "zsys/log.h"

LOG_MODULE_REGISTER(uart_esp32, LOG_LEVEL_INF);

/* Steps 1-3: polling + interrupt-driven + async API.
 *
 * Backed by ESP-IDF driver/uart.h. When evt_queue_size > 0, a per-instance
 * event task drains the IDF UART event queue, latches any error events
 * into data->err_flags, and dispatches to whichever mode is active:
 *
 *   IRQ mode  (irq_cb set): runs the registered irq_cb in task context.
 *   Async mode (async_cb set): reads data into the user's RX buffer and
 *                              emits UART_RX_* events; TX completion is
 *                              signaled by a second task (uart_esp32_tx_task).
 *
 * The two modes are mutually exclusive -- whichever callback is set most
 * recently takes effect. If both are set, async takes precedence.
 *
 * TX-IRQ limitation: ESP-IDF does not fire a UART_TX_DONE event into the
 * event queue when the TX ring drains. irq_tx_enable() kicks the callback
 * once; if the callback cannot drain everything, rely on the tx_buf_size
 * ring to absorb the burst, or switch to async mode which signals
 * UART_TX_DONE from a dedicated waiter task after uart_wait_tx_done().
 *
 * RS-485 support lands in step 4.
 */

#ifndef CONFIG_UART_EVENT_TASK_STACK
#define CONFIG_UART_EVENT_TASK_STACK 2048
#endif
#ifndef CONFIG_UART_EVENT_TASK_PRIO
#define CONFIG_UART_EVENT_TASK_PRIO 10
#endif

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
 * Interrupt-driven API
 *
 * The event task runs in task context -- not ISR -- so the IRQ callback
 * is free to use blocking or non-ISR-safe APIs internally. It still
 * follows Zephyr's semantic contract: the callback inspects irq_rx_ready
 * / irq_tx_complete and drains/fills via fifo_read / fifo_fill.
 * ---------------------------------------------------------------- */

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)

static void latch_err_event(struct uart_esp32_data *d, uart_event_type_t t)
{
    uint32_t set = 0;
    switch (t) {
    case UART_FIFO_OVF:    set |= UART_ERROR_OVERRUN; break;
    case UART_BUFFER_FULL: set |= UART_ERROR_OVERRUN; break;
    case UART_BREAK:       set |= UART_BREAK;         break;
    case UART_PARITY_ERR:  set |= UART_ERROR_PARITY;  break;
    case UART_FRAME_ERR:   set |= UART_ERROR_FRAMING; break;
    default: return;
    }
    portENTER_CRITICAL(&d->lock);
    d->err_flags |= set;
    portEXIT_CRITICAL(&d->lock);
}

static bool type_is_err(uart_event_type_t t)
{
    switch (t) {
    case UART_FIFO_OVF:
    case UART_BUFFER_FULL:
    case UART_BREAK:
    case UART_PARITY_ERR:
    case UART_FRAME_ERR:
        return true;
    default:
        return false;
    }
}

#if defined(CONFIG_UART_ASYNC_API)
static void async_dispatch_rx_data(const struct device *dev, size_t avail);
static void async_signal_stop(const struct device *dev, uint32_t reason);
#endif

static void uart_esp32_event_task(void *arg)
{
    const struct device *dev = (const struct device *)arg;
    struct uart_esp32_data *d = dev->data;
    uart_event_t evt;

    for (;;) {
        if (xQueueReceive(d->evt_queue, &evt, portMAX_DELAY) != pdPASS) {
            continue;
        }

        latch_err_event(d, evt.type);

#if defined(CONFIG_UART_ASYNC_API)
        if (d->async_cb != NULL) {
            if (type_is_err(evt.type) && d->rx_enabled) {
                async_signal_stop(dev, d->err_flags);
            } else if (evt.type == UART_DATA && d->rx_enabled) {
                async_dispatch_rx_data(dev, evt.size);
            }
            continue;
        }
#endif
        if (d->irq_cb != NULL &&
            (d->irq_rx_enabled || d->irq_tx_enabled)) {
            d->irq_cb(dev, d->irq_user_data);
        }
    }
}

static void uart_esp32_irq_callback_set(const struct device *dev,
                                        uart_irq_callback_t cb,
                                        void *user_data)
{
    struct uart_esp32_data *d = dev->data;
    d->irq_cb        = cb;
    d->irq_user_data = user_data;
}

static void uart_esp32_irq_tx_enable(const struct device *dev)
{
    struct uart_esp32_data *d = dev->data;
    d->irq_tx_enabled = true;
    /* Kick -- app callback is expected to fill as much as possible now.
     * Subsequent TX-ready signaling comes via the ring buffer, not the
     * event queue. See file-header TX-IRQ limitation note. */
    if (d->irq_cb) d->irq_cb(dev, d->irq_user_data);
}

static void uart_esp32_irq_tx_disable(const struct device *dev)
{
    struct uart_esp32_data *d = dev->data;
    d->irq_tx_enabled = false;
}

static int uart_esp32_irq_tx_ready(const struct device *dev)
{
    const struct uart_esp32_config *cfg = dev->config;
    size_t free = 0;
    /* With no TX ring, fall back to "always ready" and let fifo_fill
     * report actual bytes written. */
    if (cfg->tx_buf_size == 0) return 1;
    esp_err_t err = uart_get_tx_buffer_free_size(cfg->port, &free);
    if (err != ESP_OK) return 0;
    return (free > 0) ? 1 : 0;
}

static int uart_esp32_irq_tx_complete(const struct device *dev)
{
    const struct uart_esp32_config *cfg = dev->config;
    return (uart_wait_tx_done(cfg->port, 0) == ESP_OK) ? 1 : 0;
}

static void uart_esp32_irq_rx_enable(const struct device *dev)
{
    struct uart_esp32_data *d = dev->data;
    d->irq_rx_enabled = true;
}

static void uart_esp32_irq_rx_disable(const struct device *dev)
{
    struct uart_esp32_data *d = dev->data;
    d->irq_rx_enabled = false;
}

static int uart_esp32_irq_rx_ready(const struct device *dev)
{
    const struct uart_esp32_config *cfg = dev->config;
    size_t len = 0;
    if (uart_get_buffered_data_len(cfg->port, &len) != ESP_OK) return 0;
    return (len > 0) ? 1 : 0;
}

static int uart_esp32_fifo_fill(const struct device *dev,
                                const uint8_t *buf, int len)
{
    const struct uart_esp32_config *cfg = dev->config;
    int n;
    if (cfg->tx_buf_size == 0) {
        /* Bypass ring -- HW FIFO only, immediate count */
        n = uart_tx_chars(cfg->port, (const char *)buf, len);
    } else {
        /* Ring-backed; returns bytes written, may be short if ring is full */
        n = uart_write_bytes(cfg->port, buf, (size_t)len);
    }
    return n < 0 ? 0 : n;
}

static int uart_esp32_fifo_read(const struct device *dev,
                                uint8_t *buf, int len)
{
    const struct uart_esp32_config *cfg = dev->config;
    int n = uart_read_bytes(cfg->port, buf, (size_t)len, 0);
    return n < 0 ? 0 : n;
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

/* ----------------------------------------------------------------
 * Async API
 *
 * Event task (shared with IRQ path) performs RX double-buffer handoff:
 *   UART_DATA  -> read into rx_buf, emit UART_RX_RDY;
 *                 when buf fills, emit UART_RX_BUF_REQUEST;
 *                 if user called rx_buf_rsp, swap + emit UART_RX_BUF_RELEASED
 *                 else stop + emit UART_RX_DISABLED.
 *   err event  -> emit UART_RX_STOPPED (carries latched err_flags).
 *
 * TX path uses a dedicated task (tx_task) that blocks on tx_start_sem,
 * calls uart_wait_tx_done() after uart_write_bytes() has queued the data,
 * then emits UART_TX_DONE. This adds one task per UART instance but is
 * the only way to signal completion reliably -- IDF does not deliver
 * TX-done through the event queue.
 * ---------------------------------------------------------------- */

#if defined(CONFIG_UART_ASYNC_API)

static void async_emit(const struct device *dev, struct uart_event *evt)
{
    const struct uart_esp32_data *d = dev->data;
    if (d->async_cb) d->async_cb(dev, evt, d->async_user_data);
}

static void async_signal_stop(const struct device *dev, uint32_t reason)
{
    struct uart_esp32_data *d = dev->data;
    if (!d->rx_enabled) return;
    struct uart_event evt = {
        .type = UART_RX_STOPPED,
        .data.rx_stop = {
            .reason = reason,
            .data   = d->rx_buf,
            .len    = d->rx_buf_offset,
        },
    };
    async_emit(dev, &evt);
    /* Also release the buffer and report disabled so user reclaims it */
    struct uart_event rel = {
        .type = UART_RX_BUF_RELEASED,
        .data.rx_buf.buf = d->rx_buf,
    };
    async_emit(dev, &rel);
    d->rx_buf        = NULL;
    d->rx_buf_len    = 0;
    d->rx_buf_offset = 0;
    d->rx_enabled    = false;
    struct uart_event dis = { .type = UART_RX_DISABLED };
    async_emit(dev, &dis);
}

static void async_dispatch_rx_data(const struct device *dev, size_t avail)
{
    const struct uart_esp32_config *cfg = dev->config;
    struct uart_esp32_data *d = dev->data;

    while (avail > 0 && d->rx_enabled && d->rx_buf != NULL) {
        size_t room = d->rx_buf_len - d->rx_buf_offset;
        size_t want = (avail < room) ? avail : room;
        int n = uart_read_bytes(cfg->port,
                                d->rx_buf + d->rx_buf_offset,
                                want, 0);
        if (n <= 0) break;

        struct uart_event ev = {
            .type = UART_RX_RDY,
            .data.rx = {
                .buf    = d->rx_buf,
                .offset = d->rx_buf_offset,
                .len    = (size_t)n,
            },
        };
        async_emit(dev, &ev);

        d->rx_buf_offset += (size_t)n;
        avail -= (size_t)n;

        if (d->rx_buf_offset < d->rx_buf_len) continue;

        /* Current buffer full: request next from user */
        struct uart_event req = {
            .type = UART_RX_BUF_REQUEST,
        };
        async_emit(dev, &req);

        /* User may have provided next buffer via rx_buf_rsp */
        uint8_t *old     = d->rx_buf;
        if (d->rx_buf_next != NULL) {
            d->rx_buf        = d->rx_buf_next;
            d->rx_buf_len    = d->rx_buf_next_len;
            d->rx_buf_offset = 0;
            d->rx_buf_next   = NULL;
            d->rx_buf_next_len = 0;

            struct uart_event rel = {
                .type = UART_RX_BUF_RELEASED,
                .data.rx_buf.buf = old,
            };
            async_emit(dev, &rel);
        } else {
            /* No next buffer -- release old, disable RX */
            struct uart_event rel = {
                .type = UART_RX_BUF_RELEASED,
                .data.rx_buf.buf = old,
            };
            async_emit(dev, &rel);
            d->rx_buf        = NULL;
            d->rx_buf_len    = 0;
            d->rx_buf_offset = 0;
            d->rx_enabled    = false;
            struct uart_event dis = { .type = UART_RX_DISABLED };
            async_emit(dev, &dis);
            break;
        }
    }
}

static void uart_esp32_tx_task(void *arg)
{
    const struct device *dev = (const struct device *)arg;
    const struct uart_esp32_config *cfg = dev->config;
    struct uart_esp32_data *d = dev->data;

    for (;;) {
        if (xSemaphoreTake(d->tx_start_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uart_wait_tx_done(cfg->port, portMAX_DELAY);

        const uint8_t *buf = d->tx_buf;
        size_t         len = d->tx_len;
        d->tx_buf       = NULL;
        d->tx_len       = 0;
        d->tx_in_flight = false;

        struct uart_event evt = {
            .type = UART_TX_DONE,
            .data.tx = { .buf = buf, .len = len },
        };
        async_emit(dev, &evt);
    }
}

static int uart_esp32_callback_set(const struct device *dev,
                                   uart_callback_t cb, void *user_data)
{
    struct uart_esp32_data *d = dev->data;
    d->async_cb        = cb;
    d->async_user_data = user_data;
    return 0;
}

static int uart_esp32_tx(const struct device *dev, const uint8_t *buf,
                         size_t len, int32_t timeout_us)
{
    (void)timeout_us; /* caller-specified timeout not honored yet */
    const struct uart_esp32_config *cfg = dev->config;
    struct uart_esp32_data *d = dev->data;

    if (d->tx_in_flight) return -EBUSY;
    if (d->tx_start_sem == NULL || d->tx_task == NULL) return -ENOSYS;

    d->tx_buf       = buf;
    d->tx_len       = len;
    d->tx_in_flight = true;

    int n = uart_write_bytes(cfg->port, buf, len);
    if (n < 0) {
        d->tx_in_flight = false;
        d->tx_buf       = NULL;
        d->tx_len       = 0;
        return -EIO;
    }
    /* Kick the tx-done watcher */
    xSemaphoreGive(d->tx_start_sem);
    return 0;
}

static int uart_esp32_tx_abort(const struct device *dev)
{
    (void)dev;
    /* Mid-flight abort of uart_write_bytes is not supported by IDF. */
    return -ENOSYS;
}

static int uart_esp32_rx_enable(const struct device *dev, uint8_t *buf,
                                size_t len, int32_t timeout_us)
{
    struct uart_esp32_data *d = dev->data;

    if (d->rx_enabled) return -EBUSY;
    if (buf == NULL || len == 0) return -EINVAL;

    d->rx_buf        = buf;
    d->rx_buf_len    = len;
    d->rx_buf_offset = 0;
    d->rx_buf_next   = NULL;
    d->rx_buf_next_len = 0;
    d->rx_enabled    = true;

    /* IDF rx timeout is in baud-rate-tick units; skip translation for now --
     * a future revision can convert timeout_us using the configured baud. */
    (void)timeout_us;

    return 0;
}

static int uart_esp32_rx_buf_rsp(const struct device *dev, uint8_t *buf,
                                 size_t len)
{
    struct uart_esp32_data *d = dev->data;
    if (!d->rx_enabled) return -EACCES;
    if (d->rx_buf_next != NULL) return -EBUSY;
    d->rx_buf_next     = buf;
    d->rx_buf_next_len = len;
    return 0;
}

static int uart_esp32_rx_disable(const struct device *dev)
{
    struct uart_esp32_data *d = dev->data;
    if (!d->rx_enabled) return -EFAULT;

    uint8_t *old = d->rx_buf;
    d->rx_enabled    = false;
    d->rx_buf        = NULL;
    d->rx_buf_len    = 0;
    d->rx_buf_offset = 0;

    if (old != NULL) {
        struct uart_event rel = {
            .type = UART_RX_BUF_RELEASED,
            .data.rx_buf.buf = old,
        };
        async_emit(dev, &rel);
    }
    struct uart_event dis = { .type = UART_RX_DISABLED };
    async_emit(dev, &dis);
    return 0;
}

#endif /* CONFIG_UART_ASYNC_API */

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
        QueueHandle_t *qp = NULL;
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
        if (cfg->evt_queue_size > 0) qp = &d->evt_queue;
#endif
        esp_err_t err = uart_driver_install(cfg->port,
                                            (int)cfg->rx_buf_size,
                                            (int)cfg->tx_buf_size,
                                            cfg->evt_queue_size, qp,
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

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
    if (d->evt_queue != NULL && d->evt_task == NULL) {
        BaseType_t ok = xTaskCreate(uart_esp32_event_task,
                                    "uart_evt",
                                    CONFIG_UART_EVENT_TASK_STACK,
                                    (void *)dev,
                                    CONFIG_UART_EVENT_TASK_PRIO,
                                    &d->evt_task);
        if (ok != pdPASS) {
            LOG_ERR("event task spawn failed for port=%d", cfg->port);
            return ESP_ERR_NO_MEM;
        }
    }
#endif

#if defined(CONFIG_UART_ASYNC_API)
    if (d->tx_start_sem == NULL) {
        d->tx_start_sem = xSemaphoreCreateBinary();
        if (d->tx_start_sem == NULL) return ESP_ERR_NO_MEM;
    }
    if (d->tx_task == NULL) {
        BaseType_t ok = xTaskCreate(uart_esp32_tx_task,
                                    "uart_tx",
                                    CONFIG_UART_EVENT_TASK_STACK,
                                    (void *)dev,
                                    CONFIG_UART_EVENT_TASK_PRIO,
                                    &d->tx_task);
        if (ok != pdPASS) {
            LOG_ERR("tx task spawn failed for port=%d", cfg->port);
            return ESP_ERR_NO_MEM;
        }
    }
#endif

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
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
    .irq_callback_set = uart_esp32_irq_callback_set,
    .irq_tx_enable    = uart_esp32_irq_tx_enable,
    .irq_tx_disable   = uart_esp32_irq_tx_disable,
    .irq_tx_ready     = uart_esp32_irq_tx_ready,
    .irq_tx_complete  = uart_esp32_irq_tx_complete,
    .irq_rx_enable    = uart_esp32_irq_rx_enable,
    .irq_rx_disable   = uart_esp32_irq_rx_disable,
    .irq_rx_ready     = uart_esp32_irq_rx_ready,
    .fifo_fill        = uart_esp32_fifo_fill,
    .fifo_read        = uart_esp32_fifo_read,
#endif
#if defined(CONFIG_UART_ASYNC_API)
    .callback_set = uart_esp32_callback_set,
    .tx           = uart_esp32_tx,
    .tx_abort     = uart_esp32_tx_abort,
    .rx_enable    = uart_esp32_rx_enable,
    .rx_buf_rsp   = uart_esp32_rx_buf_rsp,
    .rx_disable   = uart_esp32_rx_disable,
#endif
};
