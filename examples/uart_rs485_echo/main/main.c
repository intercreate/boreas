/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Boreas RS-485 echo example.
 *
 * Opens UART1 in RS-485 software-DE mode and runs a simple echo server:
 * any bytes received are sent back with an "echo: " prefix. Exercises
 * the full async RX + async TX path with the DE line driven via GPIO.
 *
 * Wiring (default):
 *   UART1 TX ----> RS-485 transceiver DI
 *   UART1 RX <---- RS-485 transceiver RO
 *   GPIO DE_PIN -> RS-485 transceiver DE (and /RE tied)
 *
 * For a wireless bench test, use two boards on the same bus or connect
 * TX-RX with DE floating and observe the DE pin on a scope.
 *
 * Switch to CONFIG hw_auto=true and DE_PIN=RTS pin to use ESP32 hardware
 * auto-DE via UART_MODE_RS485_HALF_DUPLEX.
 */

#include <string.h>

#include "driver/uart.h"

#include <boreas/device_model.h>
#include <boreas/gpio_dt.h>
#include <boreas/uart_dt.h>
#include <boreas/zephyr/kernel.h>
#include <boreas/zsys/log.h>

LOG_MODULE_REGISTER(rs485_echo, LOG_LEVEL_INF);

/* -------------------------------------------------------------------
 * Hardware description
 * ---------------------------------------------------------------- */

#define UART_TX_PIN 17
#define UART_RX_PIN 18
#define DE_PIN      4  /* DE/~RE tied to a single GPIO */

DEVICE_DEFINE(gpio0, gpio_esp32_init, &gpio_esp32_api, NULL, NULL, NULL);

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
        .flow_ctrl = UART_CFG_FLOW_CTRL_RS485,
    },
    .rs485 = {
        .de = { .port = &gpio0, .pin = DE_PIN, .dt_flags = 0 },
        .re = { .port = NULL },         /* DE and /RE tied */
        .de_assert_us   = 5,            /* DE-setup time for typical xcvr */
        .de_deassert_us = 5,            /* DE-hold after last stop bit */
        .hw_auto        = false,        /* software toggle */
    },
};

DEVICE_DEFINE(uart1, uart_esp32_init, &uart_esp32_api,
              &uart1_config, &uart1_data, NULL);

/* -------------------------------------------------------------------
 * Echo state
 * ---------------------------------------------------------------- */

#define RX_BUF_SIZE 128

static uint8_t rx_buf[RX_BUF_SIZE];

/* Reply prefix; sized so prefix + up to RX_BUF_SIZE bytes fits. */
static uint8_t tx_frame[6 + RX_BUF_SIZE];

static struct k_sem tx_avail;  /* given on UART_TX_DONE */

static void on_rx_chunk(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    size_t prefix = 6;
    memcpy(tx_frame, "echo: ", prefix);
    size_t copy = (len > RX_BUF_SIZE) ? RX_BUF_SIZE : len;
    memcpy(tx_frame + prefix, data, copy);

    /* Wait for any prior TX to complete so tx_frame isn't rewritten
     * while the previous tx is still draining through the ring. */
    k_sem_take(&tx_avail, K_FOREVER);

    int rc = uart_tx(&uart1, tx_frame, prefix + copy, 0);
    if (rc != 0) {
        LOG_ERR("uart_tx failed: %d", rc);
        k_sem_give(&tx_avail);  /* restore token -- won't get TX_DONE */
    }
}

static void async_cb(const struct device *dev, struct uart_event *evt,
                     void *user_data)
{
    (void)dev;
    (void)user_data;

    switch (evt->type) {
    case UART_TX_DONE:
    case UART_TX_ABORTED:
        k_sem_give(&tx_avail);
        break;
    case UART_RX_RDY:
        on_rx_chunk(evt->data.rx.buf + evt->data.rx.offset,
                    evt->data.rx.len);
        break;
    case UART_RX_BUF_REQUEST:
        /* Keep feeding the same buffer -- simple ring behavior. When
         * the current buffer fills we'll get a BUF_REQUEST; providing
         * the same pointer means RX_BUF_RELEASED will fire for it,
         * and RX continues into the re-provided buffer. */
        uart_rx_buf_rsp(&uart1, rx_buf, sizeof(rx_buf));
        break;
    case UART_RX_BUF_RELEASED:
        /* Ignored -- rx_buf is static. */
        break;
    case UART_RX_DISABLED:
        LOG_WRN("RX disabled");
        break;
    case UART_RX_STOPPED:
        LOG_ERR("RX stopped, reason=0x%x",
                (unsigned)evt->data.rx_stop.reason);
        /* Attempt recovery: re-enable RX. */
        uart_rx_enable(&uart1, rx_buf, sizeof(rx_buf), 0);
        break;
    }
}

/* -------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------- */

void app_main(void)
{
    LOG_INF("=== Boreas RS-485 echo example ===");

    if (device_init(&gpio0) != ESP_OK) {
        LOG_ERR("gpio0 init failed");
        return;
    }
    if (device_init(&uart1) != ESP_OK) {
        LOG_ERR("uart1 init failed");
        return;
    }

    k_sem_init(&tx_avail, 1, 1);

    if (uart_callback_set(&uart1, async_cb, NULL) != 0) {
        LOG_ERR("callback_set failed");
        return;
    }
    if (uart_rx_enable(&uart1, rx_buf, sizeof(rx_buf), 0) != 0) {
        LOG_ERR("rx_enable failed");
        return;
    }

    LOG_INF("Listening on UART%d: tx=%d rx=%d DE=%d baud=%d",
            UART_NUM_1, UART_TX_PIN, UART_RX_PIN, DE_PIN, 115200);

    while (1) {
        k_msleep(10000);
    }
}
