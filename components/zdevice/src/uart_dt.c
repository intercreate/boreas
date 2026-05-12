#include "uart_dt.h"

#include <errno.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "zsys/log.h"

LOG_MODULE_REGISTER(uart_esp32, LOG_LEVEL_INF);

/* Zephyr-compatible UART driver over ESP-IDF driver/uart.h.
 *
 * Supports polling, interrupt-driven, and async modes (gated by Kconfig),
 * plus RS-485 half-duplex flow control via either software DE/RE GPIO
 * toggling or ESP32 hardware auto-DE.
 *
 * When evt_queue_size > 0, a per-instance
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
 * IRQ-callback context note: the registered irq_cb may run either in
 * the event task (on RX or error events) or in the caller's context
 * (the one-shot kick from irq_tx_enable()). Treat it as "some task
 * context" -- do not assume a specific thread.
 *
 * RS-485: two modes, selected by rs485.hw_auto.
 *   SW mode: uart_esp32_init() configures rs485.de (and rs485.re if split)
 *            as outputs and parks them in the receive state. The async
 *            TX path brackets uart_write_bytes() with DE assert + setup
 *            delay up front, and DE deassert + hold delay after
 *            uart_wait_tx_done() (which keys off shift-register drain,
 *            not FIFO empty). IRQ and polling TX do NOT toggle DE; for
 *            those modes the application must manage DE itself (polling)
 *            or use the async API (recommended).
 *   HW mode: uart_set_mode(UART_MODE_RS485_HALF_DUPLEX) -- the ESP32 UART
 *            drives RTS as DE automatically. DE must be the UART's RTS
 *            pin; pre/post delays are 0 or 1 bit-time only.
 */

#ifndef CONFIG_UART_EVENT_TASK_STACK
#define CONFIG_UART_EVENT_TASK_STACK 2048
#endif
#ifndef CONFIG_UART_EVENT_TASK_PRIO
#define CONFIG_UART_EVENT_TASK_PRIO 10
#endif

#if defined(CONFIG_UART_RS485)

/* RS-485 DE/RE helpers (software mode only).
 *
 * If rs485.re.port is NULL, DE and RE are assumed tied together (most
 * common -- single pin, active high for TX). When separate, they are
 * driven opposite: DE=1 RE=1 during TX (RE disabled, active low), DE=0
 * RE=0 during RX.
 */
static bool rs485_sw_active(const struct uart_esp32_config *cfg)
{
	return cfg->default_cfg.flow_ctrl == UART_CFG_FLOW_CTRL_RS485 && !cfg->rs485.hw_auto &&
	       cfg->rs485.de.port != NULL;
}

static void rs485_park_rx(const struct uart_esp32_config *cfg)
{
	if (!rs485_sw_active(cfg)) {
		return;
	}
	gpio_pin_set_dt(&cfg->rs485.de, 0);
	if (cfg->rs485.re.port != NULL) {
		gpio_pin_set_dt(&cfg->rs485.re, 0);
	}
}

static void rs485_assert_tx(const struct uart_esp32_config *cfg)
{
	if (!rs485_sw_active(cfg)) {
		return;
	}
	if (cfg->rs485.re.port != NULL) {
		gpio_pin_set_dt(&cfg->rs485.re, 1);
	}
	gpio_pin_set_dt(&cfg->rs485.de, 1);
	if (cfg->rs485.de_assert_us > 0) {
		esp_rom_delay_us(cfg->rs485.de_assert_us);
	}
}

static void rs485_deassert_tx(const struct uart_esp32_config *cfg)
{
	if (!rs485_sw_active(cfg)) {
		return;
	}
	if (cfg->rs485.de_deassert_us > 0) {
		esp_rom_delay_us(cfg->rs485.de_deassert_us);
	}
	gpio_pin_set_dt(&cfg->rs485.de, 0);
	if (cfg->rs485.re.port != NULL) {
		gpio_pin_set_dt(&cfg->rs485.re, 0);
	}
}

/* Enable RS-485 mode on the port. Caller has already decided the
 * mode applies (from default_cfg at init, or from a runtime configure()). */
static esp_err_t rs485_enable(const struct uart_esp32_config *cfg)
{
	esp_err_t err;

	if (cfg->rs485.hw_auto) {
		return uart_set_mode(cfg->port, UART_MODE_RS485_HALF_DUPLEX);
	}
	if (cfg->rs485.de.port == NULL) {
		LOG_ERR("RS-485 SW mode requires rs485.de.port");
		return ESP_ERR_INVALID_ARG;
	}
	err = gpio_pin_configure_dt(&cfg->rs485.de, GPIO_OUTPUT);
	if (err != ESP_OK) {
		return err;
	}
	if (cfg->rs485.re.port != NULL) {
		err = gpio_pin_configure_dt(&cfg->rs485.re, GPIO_OUTPUT);
		if (err != ESP_OK) {
			return err;
		}
	}
	rs485_park_rx(cfg);
	return ESP_OK;
}

#else /* !CONFIG_UART_RS485 */

static inline void rs485_assert_tx(const struct uart_esp32_config *cfg)
{
	(void)cfg;
}
static inline void rs485_deassert_tx(const struct uart_esp32_config *cfg)
{
	(void)cfg;
}
static inline void rs485_park_rx(const struct uart_esp32_config *cfg)
{
	(void)cfg;
}
static inline esp_err_t rs485_enable(const struct uart_esp32_config *cfg)
{
	(void)cfg;
	LOG_ERR("flow_ctrl=RS485 requested but CONFIG_UART_RS485=n");
	return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_UART_RS485 */

static esp_err_t apply_config(uart_port_t port, const struct uart_config *cfg)
{
	uart_word_length_t wl;
	uart_parity_t par;
	uart_stop_bits_t sb;
	uart_hw_flowcontrol_t fc;
	uart_config_t ic;

	switch (cfg->data_bits) {
	case UART_CFG_DATA_BITS_5:
		wl = UART_DATA_5_BITS;
		break;
	case UART_CFG_DATA_BITS_6:
		wl = UART_DATA_6_BITS;
		break;
	case UART_CFG_DATA_BITS_7:
		wl = UART_DATA_7_BITS;
		break;
	case UART_CFG_DATA_BITS_8:
		wl = UART_DATA_8_BITS;
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}

	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		par = UART_PARITY_DISABLE;
		break;
	case UART_CFG_PARITY_EVEN:
		par = UART_PARITY_EVEN;
		break;
	case UART_CFG_PARITY_ODD:
		par = UART_PARITY_ODD;
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}

	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_1:
		sb = UART_STOP_BITS_1;
		break;
	case UART_CFG_STOP_BITS_1_5:
		sb = UART_STOP_BITS_1_5;
		break;
	case UART_CFG_STOP_BITS_2:
		sb = UART_STOP_BITS_2;
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}

	switch (cfg->flow_ctrl) {
	case UART_CFG_FLOW_CTRL_NONE:
		fc = UART_HW_FLOWCTRL_DISABLE;
		break;
	case UART_CFG_FLOW_CTRL_RTS_CTS:
		fc = UART_HW_FLOWCTRL_CTS_RTS;
		break;
	case UART_CFG_FLOW_CTRL_RS485:
		/* RS-485 is a separate mode, see rs485_enable. */
		fc = UART_HW_FLOWCTRL_DISABLE;
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}

	ic = (uart_config_t){
		.baud_rate = (int)cfg->baudrate,
		.data_bits = wl,
		.parity = par,
		.stop_bits = sb,
		.flow_ctrl = fc,
		.source_clk = UART_SCLK_DEFAULT,
	};
	return uart_param_config(port, &ic);
}

static int uart_esp32_configure(const struct device *dev, const struct uart_config *cfg)
{
	const struct uart_esp32_config *c = dev->config;
	struct uart_esp32_data *d = dev->data;
	esp_err_t err;
	uint8_t prev;
	uint8_t next;

	err = apply_config(c->port, cfg);
	if (err != ESP_OK) {
		return -EIO;
	}

	/* Handle RS-485 mode transitions so flow_ctrl is effectful at
	 * runtime, not just at device_init. Entering RS-485 wires up DE/RE
	 * (or HW auto-DE); leaving it parks DE in RX and returns the UART
	 * to standard mode. */
	prev = d->current_flow_ctrl;
	next = cfg->flow_ctrl;

	if (next == UART_CFG_FLOW_CTRL_RS485 && prev != UART_CFG_FLOW_CTRL_RS485) {
		err = rs485_enable(c);
		if (err != ESP_OK) {
			return -EIO;
		}
	} else if (prev == UART_CFG_FLOW_CTRL_RS485 && next != UART_CFG_FLOW_CTRL_RS485) {
		uart_set_mode(c->port, UART_MODE_UART);
		rs485_park_rx(c);
	}

	d->current_flow_ctrl = next;
	return 0;
}

static int uart_esp32_config_get(const struct device *dev, struct uart_config *cfg)
{
	const struct uart_esp32_config *c = dev->config;
	struct uart_esp32_data *d = dev->data;
	uart_port_t port = c->port;
	uint32_t baud = 0;
	uart_word_length_t wl;
	uart_parity_t par;
	uart_stop_bits_t sb;
	uart_hw_flowcontrol_t fc;

	if (uart_get_baudrate(port, &baud) != ESP_OK) {
		return -EIO;
	}
	if (uart_get_word_length(port, &wl) != ESP_OK) {
		return -EIO;
	}
	if (uart_get_parity(port, &par) != ESP_OK) {
		return -EIO;
	}
	if (uart_get_stop_bits(port, &sb) != ESP_OK) {
		return -EIO;
	}
	if (uart_get_hw_flow_ctrl(port, &fc) != ESP_OK) {
		return -EIO;
	}

	cfg->baudrate = baud;

	switch (wl) {
	case UART_DATA_5_BITS:
		cfg->data_bits = UART_CFG_DATA_BITS_5;
		break;
	case UART_DATA_6_BITS:
		cfg->data_bits = UART_CFG_DATA_BITS_6;
		break;
	case UART_DATA_7_BITS:
		cfg->data_bits = UART_CFG_DATA_BITS_7;
		break;
	case UART_DATA_8_BITS:
		cfg->data_bits = UART_CFG_DATA_BITS_8;
		break;
	default:
		return -EIO;
	}

	switch (par) {
	case UART_PARITY_DISABLE:
		cfg->parity = UART_CFG_PARITY_NONE;
		break;
	case UART_PARITY_EVEN:
		cfg->parity = UART_CFG_PARITY_EVEN;
		break;
	case UART_PARITY_ODD:
		cfg->parity = UART_CFG_PARITY_ODD;
		break;
	default:
		return -EIO;
	}

	switch (sb) {
	case UART_STOP_BITS_1:
		cfg->stop_bits = UART_CFG_STOP_BITS_1;
		break;
	case UART_STOP_BITS_1_5:
		cfg->stop_bits = UART_CFG_STOP_BITS_1_5;
		break;
	case UART_STOP_BITS_2:
		cfg->stop_bits = UART_CFG_STOP_BITS_2;
		break;
	default:
		return -EIO;
	}

	/* flow_ctrl: RS-485 isn't observable through uart_get_hw_flow_ctrl --
	 * IDF models it as a "mode" via uart_set_mode, orthogonal to the
	 * flow-control register. We track the configured value in
	 * d->current_flow_ctrl, updated by configure(). */
	if (d->current_flow_ctrl == UART_CFG_FLOW_CTRL_RS485) {
		cfg->flow_ctrl = UART_CFG_FLOW_CTRL_RS485;
	} else if (fc == UART_HW_FLOWCTRL_CTS_RTS) {
		cfg->flow_ctrl = UART_CFG_FLOW_CTRL_RTS_CTS;
	} else {
		cfg->flow_ctrl = UART_CFG_FLOW_CTRL_NONE;
	}
	return 0;
}

static int uart_esp32_err_check(const struct device *dev)
{
	struct uart_esp32_data *d = dev->data;
	uint32_t flags;

	portENTER_CRITICAL(&d->lock);
	flags = d->err_flags;
	d->err_flags = 0;
	portEXIT_CRITICAL(&d->lock);
	return (int)flags;
}

/* ----------------------------------------------------------------
 * Event task + error latching (shared by IRQ and async paths)
 *
 * The event task runs in task context -- not ISR -- so registered
 * callbacks are free to use blocking or non-ISR-safe APIs internally.
 * ---------------------------------------------------------------- */

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)

#if defined(CONFIG_UART_ASYNC_API)
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
#endif

static void latch_err_event(struct uart_esp32_data *d, uart_event_type_t t)
{
	uint32_t set = 0;

	switch (t) {
	case UART_FIFO_OVF:
	case UART_BUFFER_FULL:
		set = UART_ERROR_OVERRUN;
		break;
	case UART_BREAK:
		set = UART_ERROR_BREAK;
		break;
	case UART_PARITY_ERR:
		set = UART_ERROR_PARITY;
		break;
	case UART_FRAME_ERR:
		set = UART_ERROR_FRAMING;
		break;
	default:
		return;
	}
	portENTER_CRITICAL(&d->lock);
	d->err_flags |= set;
	portEXIT_CRITICAL(&d->lock);
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
				uint32_t reason;

				/* Package + clear the latched flags under lock so the
				 * same bits aren't also reported by a later err_check. */
				portENTER_CRITICAL(&d->lock);
				reason = d->err_flags;
				d->err_flags = 0;
				portEXIT_CRITICAL(&d->lock);
				async_signal_stop(dev, reason);
			} else if (evt.type == UART_DATA && d->rx_enabled) {
				async_dispatch_rx_data(dev, evt.size);
			}
			continue;
		}
#endif
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
		if (d->irq_cb != NULL && (d->irq_rx_enabled || d->irq_tx_enabled)) {
			d->irq_cb(dev, d->irq_user_data);
		}
#endif
	}
}

#endif /* INTERRUPT_DRIVEN || ASYNC_API */

/* ----------------------------------------------------------------
 * Interrupt-driven API
 *
 * Follows Zephyr's semantic contract: the callback inspects
 * irq_rx_ready / irq_tx_complete and drains/fills via fifo_read /
 * fifo_fill.
 * ---------------------------------------------------------------- */

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)

static void uart_esp32_irq_callback_set(const struct device *dev, uart_irq_callback_t cb,
					void *user_data)
{
	struct uart_esp32_data *d = dev->data;
	d->irq_cb = cb;
	d->irq_user_data = user_data;
}

static void uart_esp32_irq_tx_enable(const struct device *dev)
{
	struct uart_esp32_data *d = dev->data;

	d->irq_tx_enabled = true;
	/* Kick -- app callback is expected to fill as much as possible now.
	 * Subsequent TX-ready signaling comes via the ring buffer, not the
	 * event queue. See file-header TX-IRQ limitation note. */
	if (d->irq_cb != NULL) {
		d->irq_cb(dev, d->irq_user_data);
	}
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
	esp_err_t err;

	/* With no TX ring, fall back to "always ready" and let fifo_fill
	 * report actual bytes written. */
	if (cfg->tx_buf_size == 0) {
		return 1;
	}
	err = uart_get_tx_buffer_free_size(cfg->port, &free);
	if (err != ESP_OK) {
		return 0;
	}
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

	if (uart_get_buffered_data_len(cfg->port, &len) != ESP_OK) {
		return 0;
	}
	return (len > 0) ? 1 : 0;
}

static int uart_esp32_fifo_fill(const struct device *dev, const uint8_t *buf, int len)
{
	const struct uart_esp32_config *cfg = dev->config;
	int n;

	if (cfg->tx_buf_size == 0) {
		/* Bypass ring -- HW FIFO only, immediate count. */
		n = uart_tx_chars(cfg->port, (const char *)buf, len);
	} else {
		/* Ring-backed; may be short if the ring is full. */
		n = uart_write_bytes(cfg->port, (const char *)buf, (size_t)len);
	}
	return (n < 0) ? 0 : n;
}

static int uart_esp32_fifo_read(const struct device *dev, uint8_t *buf, int len)
{
	const struct uart_esp32_config *cfg = dev->config;
	int n = uart_read_bytes(cfg->port, buf, (size_t)len, 0);

	return (n < 0) ? 0 : n;
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

	if (d->async_cb != NULL) {
		d->async_cb(dev, evt, d->async_user_data);
	}
}

static void async_signal_stop(const struct device *dev, uint32_t reason)
{
	struct uart_esp32_data *d = dev->data;
	uint8_t *buf;
	size_t off;
	struct uart_event evt;
	struct uart_event dis;

	if (!d->rx_enabled) {
		return;
	}

	/* Snapshot state + tear down BEFORE emitting, so if the user's
	 * callback calls rx_disable/rx_enable reentrantly, our own emits
	 * won't double-fire and won't step on the user's new state. */
	buf = d->rx_buf;
	off = d->rx_buf_offset;
	d->rx_buf = NULL;
	d->rx_buf_len = 0;
	d->rx_buf_offset = 0;
	d->rx_enabled = false;

	evt = (struct uart_event){
		.type = UART_RX_STOPPED,
		.data.rx_stop = {.reason = reason, .data = buf, .len = off},
	};
	async_emit(dev, &evt);

	if (buf != NULL) {
		struct uart_event rel = {
			.type = UART_RX_BUF_RELEASED,
			.data.rx_buf.buf = buf,
		};
		async_emit(dev, &rel);
	}

	dis = (struct uart_event){.type = UART_RX_DISABLED};
	async_emit(dev, &dis);
}

static void async_dispatch_rx_data(const struct device *dev, size_t avail)
{
	const struct uart_esp32_config *cfg = dev->config;
	struct uart_esp32_data *d = dev->data;

	while (avail > 0 && d->rx_enabled && d->rx_buf != NULL) {
		size_t room = d->rx_buf_len - d->rx_buf_offset;
		size_t want = (avail < room) ? avail : room;
		uint8_t *old;
		struct uart_event ev;
		struct uart_event req;
		int n;

		n = uart_read_bytes(cfg->port, d->rx_buf + d->rx_buf_offset, want, 0);
		if (n <= 0) {
			break;
		}

		ev = (struct uart_event){
			.type = UART_RX_RDY,
			.data.rx =
				{
					.buf = d->rx_buf,
					.offset = d->rx_buf_offset,
					.len = (size_t)n,
				},
		};
		async_emit(dev, &ev);

		d->rx_buf_offset += (size_t)n;
		avail -= (size_t)n;

		if (d->rx_buf_offset < d->rx_buf_len) {
			continue;
		}

		/* Current buffer full: snapshot it BEFORE emitting, so that if
		 * the user calls rx_disable inside the REQUEST callback we
		 * don't observe d->rx_buf == NULL and misreport. */
		old = d->rx_buf;

		req = (struct uart_event){.type = UART_RX_BUF_REQUEST};
		async_emit(dev, &req);

		/* User's request callback may have:
		 *   a) called rx_buf_rsp     -> rx_buf_next is set, stay enabled
		 *   b) called rx_disable     -> rx_enabled is false, do nothing
		 *   c) done neither          -> release buffer + disable
		 */
		if (!d->rx_enabled) {
			/* User disabled mid-callback; rx_disable already emitted
			 * RELEASED+DISABLED for the old buffer. */
			break;
		}

		if (d->rx_buf_next != NULL) {
			struct uart_event rel;

			d->rx_buf = d->rx_buf_next;
			d->rx_buf_len = d->rx_buf_next_len;
			d->rx_buf_offset = 0;
			d->rx_buf_next = NULL;
			d->rx_buf_next_len = 0;

			rel = (struct uart_event){
				.type = UART_RX_BUF_RELEASED,
				.data.rx_buf.buf = old,
			};
			async_emit(dev, &rel);
		} else {
			struct uart_event rel;
			struct uart_event dis;

			/* No next buffer -- tear down BEFORE emit so reentrant
			 * rx_enable sees a clean slate. */
			d->rx_buf = NULL;
			d->rx_buf_len = 0;
			d->rx_buf_offset = 0;
			d->rx_enabled = false;

			rel = (struct uart_event){
				.type = UART_RX_BUF_RELEASED,
				.data.rx_buf.buf = old,
			};
			async_emit(dev, &rel);
			dis = (struct uart_event){.type = UART_RX_DISABLED};
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
		const uint8_t *buf;
		size_t len;
		struct uart_event evt;

		if (xSemaphoreTake(d->tx_start_sem, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		uart_wait_tx_done(cfg->port, portMAX_DELAY);
		rs485_deassert_tx(cfg);

		buf = d->tx_buf;
		len = d->tx_len;
		d->tx_buf = NULL;
		d->tx_len = 0;
		d->tx_in_flight = false;

		evt = (struct uart_event){
			.type = UART_TX_DONE,
			.data.tx = {.buf = buf, .len = len},
		};
		async_emit(dev, &evt);
	}
}

static int uart_esp32_callback_set(const struct device *dev, uart_callback_t cb, void *user_data)
{
	struct uart_esp32_data *d = dev->data;
	d->async_cb = cb;
	d->async_user_data = user_data;
	return 0;
}

static int uart_esp32_tx(const struct device *dev, const uint8_t *buf, size_t len,
			 int32_t timeout_us)
{
	const struct uart_esp32_config *cfg = dev->config;
	struct uart_esp32_data *d = dev->data;
	int n;

	(void)timeout_us; /* caller-specified timeout not honored yet */

	if (d->tx_in_flight) {
		return -EBUSY;
	}
	if (d->tx_start_sem == NULL || d->tx_task == NULL) {
		return -ENOSYS;
	}

	d->tx_buf = buf;
	d->tx_len = len;
	d->tx_in_flight = true;

	rs485_assert_tx(cfg);

	/* uart_write_bytes blocks until all bytes are queued into the TX ring
	 * (not the HW FIFO), so a short return is a hard error. Treat anything
	 * other than exact-length success as failure rather than emitting a
	 * misleading UART_TX_DONE for bytes that were dropped. */
	n = uart_write_bytes(cfg->port, (const char *)buf, len);
	if (n != (int)len) {
		rs485_deassert_tx(cfg);
		d->tx_in_flight = false;
		d->tx_buf = NULL;
		d->tx_len = 0;
		return -EIO;
	}
	/* Kick the tx-done watcher. */
	xSemaphoreGive(d->tx_start_sem);
	return 0;
}

static int uart_esp32_tx_abort(const struct device *dev)
{
	(void)dev;
	/* Mid-flight abort of uart_write_bytes is not supported by IDF. */
	return -ENOSYS;
}

static int uart_esp32_rx_enable(const struct device *dev, uint8_t *buf, size_t len,
				int32_t timeout_us)
{
	const struct uart_esp32_config *cfg = dev->config;
	struct uart_esp32_data *d = dev->data;
	uart_port_t port = cfg->port;

	if (d->rx_enabled) {
		return -EBUSY;
	}
	if (buf == NULL || len == 0) {
		return -EINVAL;
	}

	/* Translate Zephyr-style timeout_us into IDF's RX idle-timeout
	 * threshold (measured in UART symbol times).
	 *
	 *   timeout_us <= 0  -> pass 0 (disables the HW idle-timeout
	 *                        interrupt; UART_RX_RDY will then only fire
	 *                        on buffer-full or error).
	 *   timeout_us  > 0  -> ceil(timeout_us / one_symbol_us), clamped
	 *                        to [1, 126]. 126 is the max documented by
	 *                        IDF v5.x driver/uart.h for uart_set_rx_timeout
	 *                        across ESP32 variants.
	 *
	 * Symbol length is computed from the live UART config (not the
	 * default_cfg snapshot) so the threshold tracks runtime
	 * uart_configure() changes.
	 */
	uint8_t tout_symbols = 0;
	if (timeout_us > 0) {
		uint32_t baud = 0;
		uart_word_length_t wl;
		uart_parity_t par;
		uart_stop_bits_t sb;

		if (uart_get_baudrate(port, &baud) != ESP_OK || baud == 0) {
			return -EIO;
		}
		if (uart_get_word_length(port, &wl) != ESP_OK ||
		    uart_get_parity(port, &par) != ESP_OK ||
		    uart_get_stop_bits(port, &sb) != ESP_OK) {
			return -EIO;
		}

		/* IDF UART_DATA_{5,6,7,8}_BITS enum is 0..3 -> +5 = actual bits.
		 * Frame length is tracked in half-bit units so UART_STOP_BITS_1_5
		 * is represented exactly. */
		unsigned data_bits = 5u + (unsigned)wl;
		unsigned parity_bit = (par == UART_PARITY_DISABLE) ? 0u : 1u;
		unsigned stop_bits_x2 = (sb == UART_STOP_BITS_2)     ? 4u
					: (sb == UART_STOP_BITS_1_5) ? 3u
								     : 2u;
		unsigned symbol_bits_x2 = (1u + data_bits + parity_bit) * 2u + stop_bits_x2;

		/* ceil(timeout_us * baud / ((symbol_bits_x2 / 2) * 1e6)), expressed
		 * as ceil(timeout_us * baud * 2 / (symbol_bits_x2 * 1e6)) to keep
		 * the computation integral. 64-bit for headroom at multi-megabaud. */
		uint64_t num = (uint64_t)timeout_us * (uint64_t)baud * 2u;
		uint64_t den = (uint64_t)symbol_bits_x2 * 1000000u;
		uint64_t syms = (num + den - 1) / den;

		if (syms < 1) {
			syms = 1;
		}
		if (syms > 126) {
			syms = 126;
		}
		tout_symbols = (uint8_t)syms;
	}

	if (uart_set_rx_timeout(port, tout_symbols) != ESP_OK) {
		return -EIO;
	}

	d->rx_buf = buf;
	d->rx_buf_len = len;
	d->rx_buf_offset = 0;
	d->rx_buf_next = NULL;
	d->rx_buf_next_len = 0;
	d->rx_enabled = true;

	return 0;
}

static int uart_esp32_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	struct uart_esp32_data *d = dev->data;

	if (buf == NULL || len == 0) {
		return -EINVAL;
	}
	if (!d->rx_enabled) {
		return -ENOSYS; /* RX not active */
	}
	/* Re-supplying the currently-active buffer would make the eventual
	 * UART_RX_BUF_RELEASED for it ambiguous (driver is still writing to
	 * it). Require a distinct buffer. */
	if (buf == d->rx_buf) {
		return -EINVAL;
	}
	if (d->rx_buf_next != NULL) {
		return -EBUSY;
	}
	d->rx_buf_next = buf;
	d->rx_buf_next_len = len;
	return 0;
}

static int uart_esp32_rx_disable(const struct device *dev)
{
	struct uart_esp32_data *d = dev->data;
	uint8_t *old;
	struct uart_event dis;

	if (!d->rx_enabled) {
		return -EFAULT; /* already disabled (Zephyr convention) */
	}

	/* Snapshot + tear down before emitting -- see async_signal_stop. */
	old = d->rx_buf;
	d->rx_enabled = false;
	d->rx_buf = NULL;
	d->rx_buf_len = 0;
	d->rx_buf_offset = 0;
	d->rx_buf_next = NULL;
	d->rx_buf_next_len = 0;

	if (old != NULL) {
		struct uart_event rel = {
			.type = UART_RX_BUF_RELEASED,
			.data.rx_buf.buf = old,
		};
		async_emit(dev, &rel);
	}
	dis = (struct uart_event){.type = UART_RX_DISABLED};
	async_emit(dev, &dis);
	return 0;
}

#endif /* CONFIG_UART_ASYNC_API */

/* ----------------------------------------------------------------
 * Polling I/O
 *
 * poll_in is non-blocking: returns 0 on byte read, -1 when none available.
 * poll_out hands the byte to the TX ring (or HW FIFO) and returns; it
 * does NOT wait for the shift register to drain -- matching Zephyr's
 * contract and avoiding a per-byte wait. Callers that need line-idle
 * confirmation before doing something else (e.g., toggling a direction
 * pin) should use the async API.
 * ---------------------------------------------------------------- */

static int uart_esp32_poll_in(const struct device *dev, unsigned char *c)
{
	const struct uart_esp32_config *cfg = dev->config;
	size_t avail = 0;
	int n;

	/* With an event queue installed, uart_read_bytes(.., ticks=0) takes an
	 * internal semaphore that can lag behind ring state -- the event task
	 * consumes UART_DATA notifications, so a byte may be in the ring with
	 * the read-semaphore still not given. Check ring length directly for
	 * a reliable non-blocking poll, then read with a 1-tick wait since
	 * we've already confirmed data is present. */
	if (uart_get_buffered_data_len(cfg->port, &avail) != ESP_OK || avail == 0) {
		return -1;
	}
	n = uart_read_bytes(cfg->port, c, 1, 1);
	return (n == 1) ? 0 : -1;
}

static void uart_esp32_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_esp32_config *cfg = dev->config;
	uart_write_bytes(cfg->port, (const char *)&c, 1);
}

/* ----------------------------------------------------------------
 * Init
 * ---------------------------------------------------------------- */

esp_err_t uart_esp32_init(const struct device *dev)
{
	const struct uart_esp32_config *cfg = dev->config;
	struct uart_esp32_data *d = dev->data;
	const portMUX_TYPE init_lock = portMUX_INITIALIZER_UNLOCKED;
	esp_err_t err;

	d->lock = init_lock;
	d->err_flags = 0;
	d->current_flow_ctrl = cfg->default_cfg.flow_ctrl;

	if (!d->driver_installed) {
		QueueHandle_t *qp = NULL;
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
		if (cfg->evt_queue_size > 0) {
			qp = &d->evt_queue;
		}
#endif
		err = uart_driver_install(cfg->port, (int)cfg->rx_buf_size, (int)cfg->tx_buf_size,
					  cfg->evt_queue_size, qp, cfg->intr_alloc_flags);
		if (err != ESP_OK) {
			LOG_ERR("uart_driver_install(port=%d) failed: %d", cfg->port, err);
			return err;
		}
		d->driver_installed = true;
	}

	err = apply_config(cfg->port, &cfg->default_cfg);
	if (err != ESP_OK) {
		LOG_ERR("param_config failed: %d", err);
		return err;
	}

	err = uart_set_pin(cfg->port, cfg->tx_pin, cfg->rx_pin, cfg->rts_pin, cfg->cts_pin);
	if (err != ESP_OK) {
		LOG_ERR("set_pin failed: %d", err);
		return err;
	}

	if (cfg->default_cfg.flow_ctrl == UART_CFG_FLOW_CTRL_RS485) {
		err = rs485_enable(cfg);
		if (err != ESP_OK) {
			LOG_ERR("rs485_enable failed: %d", err);
			return err;
		}
	}

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
	if (d->evt_queue != NULL && d->evt_task == NULL) {
		BaseType_t ok =
			xTaskCreate(uart_esp32_event_task, "uart_evt", CONFIG_UART_EVENT_TASK_STACK,
				    (void *)dev, CONFIG_UART_EVENT_TASK_PRIO, &d->evt_task);
		if (ok != pdPASS) {
			LOG_ERR("event task spawn failed for port=%d", cfg->port);
			return ESP_ERR_NO_MEM;
		}
	}
#endif

#if defined(CONFIG_UART_ASYNC_API)
	if (d->tx_start_sem == NULL) {
		d->tx_start_sem = xSemaphoreCreateBinary();
		if (d->tx_start_sem == NULL) {
			return ESP_ERR_NO_MEM;
		}
	}
	if (d->tx_task == NULL) {
		BaseType_t ok =
			xTaskCreate(uart_esp32_tx_task, "uart_tx", CONFIG_UART_EVENT_TASK_STACK,
				    (void *)dev, CONFIG_UART_EVENT_TASK_PRIO, &d->tx_task);
		if (ok != pdPASS) {
			LOG_ERR("tx task spawn failed for port=%d", cfg->port);
			return ESP_ERR_NO_MEM;
		}
	}
#endif

	LOG_INF("port=%d tx=%d rx=%d baud=%u", cfg->port, cfg->tx_pin, cfg->rx_pin,
		(unsigned)cfg->default_cfg.baudrate);
	return ESP_OK;
}

const struct uart_api uart_esp32_api = {
	.configure = uart_esp32_configure,
	.config_get = uart_esp32_config_get,
	.err_check = uart_esp32_err_check,
	.poll_in = uart_esp32_poll_in,
	.poll_out = uart_esp32_poll_out,
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	.irq_callback_set = uart_esp32_irq_callback_set,
	.irq_tx_enable = uart_esp32_irq_tx_enable,
	.irq_tx_disable = uart_esp32_irq_tx_disable,
	.irq_tx_ready = uart_esp32_irq_tx_ready,
	.irq_tx_complete = uart_esp32_irq_tx_complete,
	.irq_rx_enable = uart_esp32_irq_rx_enable,
	.irq_rx_disable = uart_esp32_irq_rx_disable,
	.irq_rx_ready = uart_esp32_irq_rx_ready,
	.fifo_fill = uart_esp32_fifo_fill,
	.fifo_read = uart_esp32_fifo_read,
#endif
#if defined(CONFIG_UART_ASYNC_API)
	.callback_set = uart_esp32_callback_set,
	.tx = uart_esp32_tx,
	.tx_abort = uart_esp32_tx_abort,
	.rx_enable = uart_esp32_rx_enable,
	.rx_buf_rsp = uart_esp32_rx_buf_rsp,
	.rx_disable = uart_esp32_rx_disable,
#endif
};
