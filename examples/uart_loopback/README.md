# Boreas UART examples

Two examples share this README:

- **[`uart_loopback/`](../uart_loopback)** — self-test of polling / IRQ /
  async modes using the ESP32's internal TX-RX loopback. **No wiring
  required.**
- **[`uart_rs485_echo/`](../uart_rs485_echo)** — echo server over RS-485
  using the async API with software-driven DE/RE. **Needs a transceiver.**

Both target the ESP32-S3 and use UART1 at 115200 8N1.

---

## Build & flash

From the repo root:

```bash
source .env                          # sets IDF_PATH, activates venv
cd examples/uart_loopback            # or examples/uart_rs485_echo
idf.py -p /dev/cu.usbmodem<N> flash monitor
```

`Ctrl-]` exits the monitor. To find the right serial device:

```bash
ls /dev/cu.usbmodem* /dev/cu.usbserial-* 2>/dev/null
```

If `idf.py` isn't found, `source .env` wasn't run; if `cu.usbmodem*`
doesn't appear, the board isn't in bootloader USB mode (try holding
BOOT while tapping RESET on DevKitC-style boards).

---

## `uart_loopback` — zero wiring

Flash and watch the log. Expected output:

```
=== Boreas UART loopback example ===
phase1 polling rx='POLL'
phase2 irq rx='IRQ-MODE' (rc=0)
phase3 async rx='ASYNC-MODE!' total=11 (tx_rc=0 rx_rc=0)
=== Result: 3/3 PASS ===
```

The example calls `uart_set_loop_back(UART_NUM_1, true)` after
`device_init`, which routes TX to RX internally at the UART matrix.
`TX_PIN=17` / `RX_PIN=18` are set in config but do not need external
connections. To test with a real wire, remove the `uart_set_loop_back`
call and tie GPIO17 to GPIO18 with a jumper.

### What you need

| Part | Notes |
|------|-------|
| ESP32-S3 dev board | DevKitC-1, Feather, XIAO ESP32-S3 — any S3 variant |
| USB cable | USB-C on most newer S3 boards |

---

## `uart_rs485_echo` — needs a transceiver

RS-485 is a differential half-duplex bus; the ESP32 UART pins are
single-ended TTL. You need an RS-485 transceiver chip to bridge the
two. The example drives the transceiver's DE/~RE pin from a GPIO
(default `GPIO4`) and asserts it around each TX burst via the async
API's TX-done path.

### Minimal bench setup (one board + scope)

```
ESP32-S3                     RS-485 transceiver (MAX3485 / SP3485 / etc.)
 3V3     ─────────────────── VCC
 GND     ─────────────────── GND
 GPIO17  (TX) ────────────── DI   (driver input)
 GPIO18  (RX) ────────────── RO   (receiver output)
 GPIO4   (DE) ─┬──────────── DE   (driver enable, active high)
               └──────────── /RE  (receiver enable, active low -- tied)
                             A, B ── (floating, or to the bus)
```

Probe the DE pin on a logic analyzer / scope alongside the TX pin.
You should see DE rise ~5 µs before TX starts toggling, hold through
the whole byte stream, and fall ~5 µs after the last stop bit. Those
setup/hold times come from `de_assert_us` / `de_deassert_us` in the
example's `uart_esp32_config`.

### Two-node setup (actual round-trip test)

Run `uart_rs485_echo` on one node, send bytes from a second node. The
firmware replies with `echo: <your bytes>`.

The second node can be:

1. **Another ESP32 dev board + transceiver** — simplest; wire A↔A, B↔B
   on the differential pair.
2. **USB-to-RS485 dongle on your Mac** — plug the dongle into USB, wire
   its A/B to the transceiver's A/B. Then:
   ```bash
   screen /dev/cu.usbserial-<dongle> 115200
   ```
   Type a character, press Enter — you'll see `echo: <char>` echoed
   back. `Ctrl-A K` to exit screen.

For buses longer than about a meter, terminate with **120 Ω across
A-B at each end**. On a short bench cable, skip the resistors.

### What you need

| Part | Notes |
|------|-------|
| ESP32-S3 dev board | Same as above |
| USB cable | |
| RS-485 transceiver breakout | Sparkfun BOB-10124 (MAX485), Adafruit SP3485, or bare MAX3485 on a protoboard. SP3485 is 3.3 V native — no level-shifting concerns. |
| 5× jumper wires | 3V3, GND, TX→DI, RX→RO, DE-GPIO→DE (tied to /RE) |
| Scope or logic analyzer | Optional but useful — confirms DE timing without a second node |
| Second node (pick one) | Another dev board + transceiver, **or** a USB-RS485 dongle (FTDI USB-RS485-WE, DSD TECH SH-U11, Waveshare USB-to-RS485). |
| 2× 120 Ω resistors | Termination, only for buses > ~1 m |

### Switching to hardware auto-DE

The example uses software DE (portable, works on any GPIO, programmable
delays). To use the ESP32's built-in auto-DE instead:

1. Wire DE to the UART's RTS pin (UART1 default: `GPIO19` on most S3
   boards, check your datasheet).
2. In `main.c`, set `.rts_pin = <RTS GPIO>` and
   `.rs485.hw_auto = true`.
3. `de_assert_us` / `de_deassert_us` are ignored in HW mode
   (delays are fixed at 0 or 1 bit-time).

Hardware auto-DE has lower turnaround jitter but forces DE onto the
RTS pin and removes programmable timing.

---

## Troubleshooting

- **`phase1 polling` fails** on `uart_loopback`: loopback mode not
  taking effect. Confirm the board is ESP32-S3 (`idf.py --preview
  set-target esp32s3 && idf.py build` if target mismatch), and that
  `uart_set_loop_back` returned `ESP_OK` at startup (check monitor
  output for the `=== Boreas ===` banner).

- **`phase3 async` times out**: TX ring underflow or event task not
  running. Increase `tx_buf_size` / `evt_queue_size` in the config,
  or bump `CONFIG_UART_EVENT_TASK_PRIO` if a higher-priority task
  is starving it.

- **No echo on RS-485**: first verify DE toggles on a scope. Then
  swap A and B (polarity convention varies by vendor). Then check
  termination — unterminated buses can reflect enough to mangle
  framing at high baud.

- **`LOG_WRN: RX stopped, reason=0x1`**: `UART_ERROR_OVERRUN` — RX
  ring overflowed. Increase `rx_buf_size`, lower baud, or drain the
  buffer faster in the callback.
