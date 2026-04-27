/**
 * @file uart_dt.h
 * @brief UART controller device types and convenience functions.
 *
 * Zephyr-compatible UART API over ESP-IDF's driver/uart.h. Three operating
 * modes are exposed: polling (always available), interrupt-driven, and async.
 * Per-mode vtable slots are NULL when the feature is not compiled in;
 * helpers return -ENOSYS in that case, matching Zephyr's contract.
 *
 * RS-485 half-duplex flow control is a flow_ctrl value, not a separate API.
 * See struct uart_rs485_config for DE/RE pin wiring and timing.
 */
#pragma once

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "device_model.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gpio_dt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * Config enums -- values match Zephyr's uart.h so user code ports cleanly.
 * ---------------------------------------------------------------- */

#define UART_CFG_PARITY_NONE 0
#define UART_CFG_PARITY_ODD  1
#define UART_CFG_PARITY_EVEN 2

#define UART_CFG_STOP_BITS_1   1
#define UART_CFG_STOP_BITS_1_5 2
#define UART_CFG_STOP_BITS_2   3

#define UART_CFG_DATA_BITS_5 0
#define UART_CFG_DATA_BITS_6 1
#define UART_CFG_DATA_BITS_7 2
#define UART_CFG_DATA_BITS_8 3

#define UART_CFG_FLOW_CTRL_NONE    0
#define UART_CFG_FLOW_CTRL_RTS_CTS 1
#define UART_CFG_FLOW_CTRL_DTR_DSR 2
#define UART_CFG_FLOW_CTRL_RS485   3

/* err_check return bits -- values match Zephyr uart_rx_stop_reason */
#define UART_ERROR_OVERRUN   (1u << 0)
#define UART_ERROR_PARITY    (1u << 1)
#define UART_ERROR_FRAMING   (1u << 2)
#define UART_ERROR_BREAK     (1u << 3)
#define UART_ERROR_COLLISION (1u << 4)
#define UART_ERROR_NOISE     (1u << 5)

/* Async event types -- values match Zephyr uart_event_type */
enum uart_event_type {
	UART_TX_DONE = 0,
	UART_TX_ABORTED,
	UART_RX_RDY,
	UART_RX_BUF_REQUEST,
	UART_RX_BUF_RELEASED,
	UART_RX_DISABLED,
	UART_RX_STOPPED,
};

struct uart_config {
	uint32_t baudrate;
	uint8_t parity;
	uint8_t stop_bits;
	uint8_t data_bits;
	uint8_t flow_ctrl;
};

/* ----------------------------------------------------------------
 * RS-485 instance config
 *
 * Two modes, selected by .hw_auto:
 *   hw_auto=false: software toggle of .de (and optionally .re) around TX.
 *                  Works on any pin. Uses ESP-IDF uart_wait_tx_done() to
 *                  key deassert off shift-register drain, not FIFO empty.
 *   hw_auto=true : ESP32 UART's built-in auto-DE via uart_set_mode(
 *                  UART_MODE_RS485_HALF_DUPLEX). DE must be the UART's RTS
 *                  pin. Pre/post delays are limited to 0 or 1 bit-time.
 * ---------------------------------------------------------------- */

struct uart_rs485_config {
	struct gpio_dt_spec de;  /* driver-enable; .port NULL = unused */
	struct gpio_dt_spec re;  /* receiver-enable; .port NULL = tied to DE */
	uint16_t de_assert_us;   /* setup time, software mode only */
	uint16_t de_deassert_us; /* hold time, software mode only */
	bool hw_auto;
};

/* ----------------------------------------------------------------
 * Async event payload
 * ---------------------------------------------------------------- */

struct uart_event {
	enum uart_event_type type;
	union {
		struct {
			const uint8_t *buf;
			size_t len;
		} tx;
		struct {
			uint8_t *buf;
			size_t offset;
			size_t len;
		} rx;
		struct {
			uint8_t *buf;
		} rx_buf;
		struct {
			uint32_t reason;
			uint8_t *data;
			size_t len;
		} rx_stop;
	} data;
};

/* ----------------------------------------------------------------
 * Callback signatures
 * ---------------------------------------------------------------- */

typedef void (*uart_irq_callback_t)(const struct device *dev, void *user_data);
typedef void (*uart_callback_t)(const struct device *dev, struct uart_event *evt, void *user_data);

/* ----------------------------------------------------------------
 * Driver vtable
 * ---------------------------------------------------------------- */

struct uart_api {
	/* common */
	int (*configure)(const struct device *dev, const struct uart_config *cfg);
	int (*config_get)(const struct device *dev, struct uart_config *cfg);
	int (*err_check)(const struct device *dev);

	/* polling */
	int (*poll_in)(const struct device *dev, unsigned char *c);
	void (*poll_out)(const struct device *dev, unsigned char c);

	/* interrupt-driven (NULL when CONFIG_UART_INTERRUPT_DRIVEN=n) */
	void (*irq_callback_set)(const struct device *dev, uart_irq_callback_t cb, void *user_data);
	void (*irq_tx_enable)(const struct device *dev);
	void (*irq_tx_disable)(const struct device *dev);
	int (*irq_tx_ready)(const struct device *dev);
	int (*irq_tx_complete)(const struct device *dev);
	void (*irq_rx_enable)(const struct device *dev);
	void (*irq_rx_disable)(const struct device *dev);
	int (*irq_rx_ready)(const struct device *dev);
	int (*fifo_fill)(const struct device *dev, const uint8_t *buf, int len);
	int (*fifo_read)(const struct device *dev, uint8_t *buf, int len);

	/* async (NULL when CONFIG_UART_ASYNC_API=n) */
	int (*callback_set)(const struct device *dev, uart_callback_t cb, void *user_data);
	int (*tx)(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout_us);
	int (*tx_abort)(const struct device *dev);
	int (*rx_enable)(const struct device *dev, uint8_t *buf, size_t len, int32_t timeout_us);
	int (*rx_buf_rsp)(const struct device *dev, uint8_t *buf, size_t len);
	int (*rx_disable)(const struct device *dev);
};

/* ----------------------------------------------------------------
 * ESP32 driver instance -- config (static) and data (runtime)
 * ---------------------------------------------------------------- */

struct uart_esp32_config {
	uart_port_t port; /* UART_NUM_0..UART_NUM_MAX-1 */
	int tx_pin;       /* -1 = unused */
	int rx_pin;
	int rts_pin;
	int cts_pin;
	size_t tx_buf_size; /* 0 = no TX ring buffer (poll-only) */
	size_t rx_buf_size; /* must be >= UART_HW_FIFO_LEN */
	int evt_queue_size; /* 0 = no event queue (poll-only) */
	int intr_alloc_flags;
	struct uart_config default_cfg;
	struct uart_rs485_config rs485;
};

struct uart_esp32_data {
	bool driver_installed;
	QueueHandle_t evt_queue; /* IDF event queue; NULL in poll-only */
	TaskHandle_t evt_task;
	uint32_t err_flags; /* latched, cleared by err_check */
	portMUX_TYPE lock;
	uint8_t current_flow_ctrl; /* tracks RS-485 across configure() */
	/* IRQ-mode callback (populated by irq_callback_set) */
	uart_irq_callback_t irq_cb;
	void *irq_user_data;
	bool irq_rx_enabled;
	bool irq_tx_enabled;
	/* Async-mode callback + buffer state */
	uart_callback_t async_cb;
	void *async_user_data;
	/* RX side */
	bool rx_enabled;
	uint8_t *rx_buf;
	size_t rx_buf_len;
	size_t rx_buf_offset; /* next write position in rx_buf */
	uint8_t *rx_buf_next;
	size_t rx_buf_next_len;
	/* TX side */
	bool tx_in_flight;
	const uint8_t *tx_buf; /* for UART_TX_DONE event payload */
	size_t tx_len;
	SemaphoreHandle_t tx_start_sem; /* kick tx-done watcher task */
	TaskHandle_t tx_task;
};

extern const struct uart_api uart_esp32_api;
esp_err_t uart_esp32_init(const struct device *dev);

/* ----------------------------------------------------------------
 * Inline helpers -- Zephyr-compatible call shape.
 *
 * Helpers for modes that aren't compiled in return -ENOSYS.
 * ---------------------------------------------------------------- */

static inline int uart_configure(const struct device *dev, const struct uart_config *cfg)
{
	const struct uart_api *api = dev->api;

	if (api->configure == NULL) {
		return -ENOSYS;
	}
	return api->configure(dev, cfg);
}

static inline int uart_config_get(const struct device *dev, struct uart_config *cfg)
{
	const struct uart_api *api = dev->api;

	if (api->config_get == NULL) {
		return -ENOSYS;
	}
	return api->config_get(dev, cfg);
}

static inline int uart_err_check(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->err_check == NULL) {
		return -ENOSYS;
	}
	return api->err_check(dev);
}

static inline int uart_poll_in(const struct device *dev, unsigned char *c)
{
	const struct uart_api *api = dev->api;

	if (api->poll_in == NULL) {
		return -ENOSYS;
	}
	return api->poll_in(dev, c);
}

static inline void uart_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_api *api = dev->api;

	if (api->poll_out != NULL) {
		api->poll_out(dev, c);
	}
}

/* Interrupt-driven helpers */

static inline void uart_irq_callback_user_data_set(const struct device *dev, uart_irq_callback_t cb,
						   void *user_data)
{
	const struct uart_api *api = dev->api;

	if (api->irq_callback_set != NULL) {
		api->irq_callback_set(dev, cb, user_data);
	}
}

static inline void uart_irq_tx_enable(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->irq_tx_enable != NULL) {
		api->irq_tx_enable(dev);
	}
}

static inline void uart_irq_tx_disable(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->irq_tx_disable != NULL) {
		api->irq_tx_disable(dev);
	}
}

static inline int uart_irq_tx_ready(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->irq_tx_ready == NULL) {
		return -ENOSYS;
	}
	return api->irq_tx_ready(dev);
}

static inline int uart_irq_tx_complete(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->irq_tx_complete == NULL) {
		return -ENOSYS;
	}
	return api->irq_tx_complete(dev);
}

static inline void uart_irq_rx_enable(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->irq_rx_enable != NULL) {
		api->irq_rx_enable(dev);
	}
}

static inline void uart_irq_rx_disable(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->irq_rx_disable != NULL) {
		api->irq_rx_disable(dev);
	}
}

static inline int uart_irq_rx_ready(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->irq_rx_ready == NULL) {
		return -ENOSYS;
	}
	return api->irq_rx_ready(dev);
}

static inline int uart_fifo_fill(const struct device *dev, const uint8_t *buf, int len)
{
	const struct uart_api *api = dev->api;

	if (api->fifo_fill == NULL) {
		return -ENOSYS;
	}
	return api->fifo_fill(dev, buf, len);
}

static inline int uart_fifo_read(const struct device *dev, uint8_t *buf, int len)
{
	const struct uart_api *api = dev->api;

	if (api->fifo_read == NULL) {
		return -ENOSYS;
	}
	return api->fifo_read(dev, buf, len);
}

/* Async helpers */

static inline int uart_callback_set(const struct device *dev, uart_callback_t cb, void *user_data)
{
	const struct uart_api *api = dev->api;

	if (api->callback_set == NULL) {
		return -ENOSYS;
	}
	return api->callback_set(dev, cb, user_data);
}

static inline int uart_tx(const struct device *dev, const uint8_t *buf, size_t len,
			  int32_t timeout_us)
{
	const struct uart_api *api = dev->api;

	if (api->tx == NULL) {
		return -ENOSYS;
	}
	return api->tx(dev, buf, len, timeout_us);
}

static inline int uart_tx_abort(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->tx_abort == NULL) {
		return -ENOSYS;
	}
	return api->tx_abort(dev);
}

static inline int uart_rx_enable(const struct device *dev, uint8_t *buf, size_t len,
				 int32_t timeout_us)
{
	const struct uart_api *api = dev->api;

	if (api->rx_enable == NULL) {
		return -ENOSYS;
	}
	return api->rx_enable(dev, buf, len, timeout_us);
}

static inline int uart_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	const struct uart_api *api = dev->api;

	if (api->rx_buf_rsp == NULL) {
		return -ENOSYS;
	}
	return api->rx_buf_rsp(dev, buf, len);
}

static inline int uart_rx_disable(const struct device *dev)
{
	const struct uart_api *api = dev->api;

	if (api->rx_disable == NULL) {
		return -ENOSYS;
	}
	return api->rx_disable(dev);
}

#ifdef __cplusplus
}
#endif
