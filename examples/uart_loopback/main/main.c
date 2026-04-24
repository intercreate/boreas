/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas UART loopback example.
 *
 * Exercises all three API modes against UART1 with the ESP32 internal
 * loopback enabled, so TX bytes appear on RX without any physical
 * wiring. Prints PASS/FAIL for each phase.
 *
 *   Phase 1 -- Polling: uart_poll_out() then uart_poll_in()
 *   Phase 2 -- Interrupt-driven: uart_fifo_fill / uart_fifo_read via IRQ cb
 *   Phase 3 -- Async: uart_tx / uart_rx_enable with UART_RX_RDY events
 *   Phase 4 -- Async idle-timeout: short message into an oversized buffer,
 *              asserting UART_RX_RDY fires on HW RX idle-timeout rather
 *              than buffer-full. Validates uart_rx_enable(timeout_us) is
 *              wired to IDF's uart_set_rx_timeout.
 *
 * To switch to a real wire, remove the uart_set_loop_back() call below
 * and tie UART1 TX->RX externally. TX_PIN / RX_PIN are routable to any
 * GPIO on the ESP32-S3 via the GPIO matrix.
 */

#include <string.h>

#include "driver/uart.h"

#include <boreas/device_model.h>
#include <boreas/uart_dt.h>
#include <boreas/zephyr/kernel.h>
#include <boreas/zsys/log.h>

LOG_MODULE_REGISTER(uart_loopback, LOG_LEVEL_INF);

/* -------------------------------------------------------------------
 * Hardware description
 * ---------------------------------------------------------------- */

#define UART_TX_PIN 17
#define UART_RX_PIN 18

static struct uart_esp32_data uart1_data;

static const struct uart_esp32_config uart1_config = {
    .port            = UART_NUM_1,
    .tx_pin          = UART_TX_PIN,
    .rx_pin          = UART_RX_PIN,
    .rts_pin         = UART_PIN_NO_CHANGE,
    .cts_pin         = UART_PIN_NO_CHANGE,
    .tx_buf_size     = 512,
    .rx_buf_size     = 512,
    .evt_queue_size  = 16,
    .intr_alloc_flags = 0,
    .default_cfg = {
        .baudrate  = 115200,
        .parity    = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    },
};

DEVICE_DEFINE(uart1, uart_esp32_init, &uart_esp32_api,
              &uart1_config, &uart1_data, NULL);

/* -------------------------------------------------------------------
 * Phase 1 -- polling
 * ---------------------------------------------------------------- */

static bool phase_polling(void)
{
    static const char tx[] = "POLL";
    char rx[sizeof(tx)] = {0};

    for (size_t i = 0; i < sizeof(tx) - 1; i++) {
        uart_poll_out(&uart1, (unsigned char)tx[i]);
    }

    for (size_t i = 0; i < sizeof(tx) - 1; i++) {
        int deadline = 100;
        while (deadline-- > 0) {
            unsigned char c;
            if (uart_poll_in(&uart1, &c) == 0) {
                rx[i] = (char)c;
                break;
            }
            k_msleep(1);
        }
    }

    LOG_INF("phase1 polling rx='%s'", rx);
    return memcmp(tx, rx, sizeof(tx) - 1) == 0;
}

/* -------------------------------------------------------------------
 * Phase 2 -- interrupt-driven
 * ---------------------------------------------------------------- */

static const char irq_tx[] = "IRQ-MODE";
static volatile size_t irq_tx_idx;
static char   irq_rx_buf[16];
static volatile size_t irq_rx_idx;
static struct k_sem irq_done;

static void irq_cb(const struct device *dev, void *user_data)
{
    (void)user_data;

    if (uart_irq_rx_ready(dev)) {
        uint8_t chunk[8];
        int n = uart_fifo_read(dev, chunk, sizeof(chunk));
        for (int i = 0; i < n && irq_rx_idx < sizeof(irq_rx_buf) - 1; i++) {
            irq_rx_buf[irq_rx_idx++] = (char)chunk[i];
        }
        if (irq_rx_idx >= sizeof(irq_tx) - 1) {
            k_sem_give(&irq_done);
        }
    }

    if (uart_irq_tx_ready(dev) && irq_tx_idx < sizeof(irq_tx) - 1) {
        int remaining = (int)(sizeof(irq_tx) - 1 - irq_tx_idx);
        int n = uart_fifo_fill(dev, (const uint8_t *)&irq_tx[irq_tx_idx],
                               remaining);
        irq_tx_idx += (size_t)n;
        if (irq_tx_idx >= sizeof(irq_tx) - 1) {
            uart_irq_tx_disable(dev);
        }
    }
}

static bool phase_irq(void)
{
    irq_tx_idx = 0;
    irq_rx_idx = 0;
    memset(irq_rx_buf, 0, sizeof(irq_rx_buf));
    k_sem_init(&irq_done, 0, 1);

    uart_irq_callback_user_data_set(&uart1, irq_cb, NULL);
    uart_irq_rx_enable(&uart1);
    uart_irq_tx_enable(&uart1);

    int rc = k_sem_take(&irq_done, K_MSEC(1000));
    uart_irq_rx_disable(&uart1);
    uart_irq_tx_disable(&uart1);
    uart_irq_callback_user_data_set(&uart1, NULL, NULL);

    LOG_INF("phase2 irq rx='%s' (rc=%d)", irq_rx_buf, rc);
    return rc == 0 && memcmp(irq_tx, irq_rx_buf, sizeof(irq_tx) - 1) == 0;
}

/* -------------------------------------------------------------------
 * Phase 3 -- async
 * ---------------------------------------------------------------- */

static const char async_tx[] = "ASYNC-MODE!";
static uint8_t   async_rx_buf[64];
static volatile size_t async_rx_total;
static struct k_sem async_rx_done;
static struct k_sem async_tx_done;

static void async_cb(const struct device *dev, struct uart_event *evt,
                     void *user_data)
{
    (void)dev;
    (void)user_data;

    switch (evt->type) {
    case UART_TX_DONE:
        k_sem_give(&async_tx_done);
        break;
    case UART_RX_RDY:
        async_rx_total += evt->data.rx.len;
        break;
    case UART_RX_DISABLED:
        k_sem_give(&async_rx_done);
        break;
    case UART_RX_BUF_REQUEST:
    case UART_RX_BUF_RELEASED:
        /* Single-buffer mode -- ignore; buffer fill triggers disable */
        break;
    case UART_RX_STOPPED:
        LOG_WRN("async rx stopped, reason=0x%x", (unsigned)evt->data.rx_stop.reason);
        k_sem_give(&async_rx_done);
        break;
    case UART_TX_ABORTED:
        k_sem_give(&async_tx_done);
        break;
    }
}

static bool phase_async(void)
{
    async_rx_total = 0;
    memset(async_rx_buf, 0, sizeof(async_rx_buf));
    k_sem_init(&async_rx_done, 0, 1);
    k_sem_init(&async_tx_done, 0, 1);

    uart_callback_set(&uart1, async_cb, NULL);

    /* Enable RX into a buffer just big enough for one expected message. */
    const size_t expect = sizeof(async_tx) - 1;
    if (uart_rx_enable(&uart1, async_rx_buf, expect, 0) != 0) {
        LOG_ERR("rx_enable failed");
        return false;
    }

    if (uart_tx(&uart1, (const uint8_t *)async_tx, expect, 0) != 0) {
        LOG_ERR("tx kick failed");
        return false;
    }

    int rc_tx = k_sem_take(&async_tx_done, K_MSEC(500));
    int rc_rx = k_sem_take(&async_rx_done, K_MSEC(500));

    LOG_INF("phase3 async rx='%.*s' total=%u (tx_rc=%d rx_rc=%d)",
            (int)async_rx_total, (char *)async_rx_buf,
            (unsigned)async_rx_total, rc_tx, rc_rx);

    return rc_tx == 0 && rc_rx == 0 &&
           async_rx_total == expect &&
           memcmp(async_tx, async_rx_buf, expect) == 0;
}

/* -------------------------------------------------------------------
 * Phase 4 -- async idle-timeout
 *
 * Send a short message into a buffer much larger than the message,
 * with a nonzero RX idle-timeout. The only way UART_RX_RDY can fire
 * here is via the HW idle-timeout interrupt (the buffer never fills).
 *
 * Before the uart_rx_enable(timeout_us) -> uart_set_rx_timeout wiring,
 * this phase times out with no event. After the wiring, it returns
 * within ~one timeout period of the last transmitted byte.
 * ---------------------------------------------------------------- */

#define PHASE4_TIMEOUT_US   5000   /* 5 ms of idle after last byte */
#define PHASE4_WAIT_MS      200    /* generous window for event to fire */

static const char   idle_tx[] = "IDLE";
static uint8_t      idle_rx_buf[64];       /* much larger than message */
static volatile size_t idle_rx_total;
static volatile bool   idle_rx_ready_seen;
static struct k_sem idle_rx_event;
static struct k_sem idle_tx_done;

static void idle_cb(const struct device *dev, struct uart_event *evt,
                    void *user_data)
{
    (void)dev;
    (void)user_data;

    switch (evt->type) {
    case UART_TX_DONE:
        k_sem_give(&idle_tx_done);
        break;
    case UART_RX_RDY:
        idle_rx_total += evt->data.rx.len;
        idle_rx_ready_seen = true;
        k_sem_give(&idle_rx_event);
        break;
    case UART_RX_BUF_REQUEST:
    case UART_RX_BUF_RELEASED:
    case UART_RX_DISABLED:
        /* Not expected to drive the test; buffer is oversized so we
         * should see RX_RDY from the idle-timeout before any of these. */
        break;
    case UART_RX_STOPPED:
        LOG_WRN("phase4 rx stopped, reason=0x%x",
                (unsigned)evt->data.rx_stop.reason);
        break;
    case UART_TX_ABORTED:
        k_sem_give(&idle_tx_done);
        break;
    }
}

static bool phase_idle_timeout(void)
{
    idle_rx_total = 0;
    idle_rx_ready_seen = false;
    memset(idle_rx_buf, 0, sizeof(idle_rx_buf));
    k_sem_init(&idle_rx_event, 0, 1);
    k_sem_init(&idle_tx_done, 0, 1);

    uart_callback_set(&uart1, idle_cb, NULL);

    /* Oversized buffer + nonzero timeout -- RX_RDY can only come from
     * the HW idle-timeout interrupt. */
    if (uart_rx_enable(&uart1, idle_rx_buf, sizeof(idle_rx_buf),
                       PHASE4_TIMEOUT_US) != 0) {
        LOG_ERR("rx_enable failed");
        return false;
    }

    const size_t expect = sizeof(idle_tx) - 1;
    if (uart_tx(&uart1, (const uint8_t *)idle_tx, expect, 0) != 0) {
        LOG_ERR("tx kick failed");
        (void)uart_rx_disable(&uart1);
        return false;
    }

    int rc_tx = k_sem_take(&idle_tx_done, K_MSEC(PHASE4_WAIT_MS));
    int rc_rx = k_sem_take(&idle_rx_event, K_MSEC(PHASE4_WAIT_MS));

    (void)uart_rx_disable(&uart1);

    LOG_INF("phase4 idle-timeout rx='%.*s' total=%u "
            "(tx_rc=%d rx_rc=%d ready_seen=%d)",
            (int)idle_rx_total, (char *)idle_rx_buf,
            (unsigned)idle_rx_total, rc_tx, rc_rx,
            (int)idle_rx_ready_seen);

    return rc_tx == 0 && rc_rx == 0 &&
           idle_rx_ready_seen &&
           idle_rx_total == expect &&
           memcmp(idle_tx, idle_rx_buf, expect) == 0;
}

/* -------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------- */

void app_main(void)
{
    LOG_INF("=== Boreas UART loopback example ===");

    esp_err_t err = device_init(&uart1);
    if (err != ESP_OK) {
        LOG_ERR("uart1 device_init failed: %s", esp_err_to_name(err));
        return;
    }

    /* Enable internal loopback on UART1 so TX -> RX without wiring. */
    if (uart_set_loop_back(UART_NUM_1, true) != ESP_OK) {
        LOG_ERR("uart_set_loop_back failed");
        return;
    }

    /* Enabling loopback after uart_set_pin can latch a spurious byte on RX
     * from the TX-pin state transition. Let the line settle, then flush. */
    k_msleep(5);
    uart_flush_input(UART_NUM_1);

    int pass = 0, total = 4;

    pass += phase_polling()       ? 1 : 0;
    pass += phase_irq()           ? 1 : 0;
    pass += phase_async()         ? 1 : 0;
    pass += phase_idle_timeout()  ? 1 : 0;

    LOG_INF("=== Result: %d/%d PASS ===", pass, total);

    while (1) {
        k_msleep(10000);
    }
}
