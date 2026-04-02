# Blink - Device Model Example

Blinks an LED using the Boreas device model and kernel timer.

## What it shows

- **`DEVICE_DEFINE`** -- static device declaration for the GPIO controller
- **`gpio_dt_spec`** -- pin + flags (active-low support) bound to a device
- **`gpio_pin_configure_dt()` / `gpio_pin_toggle_dt()`** -- hardware-abstract GPIO ops
- **`k_timer`** -- periodic timer driving the blink
- **Separation of concerns** -- hardware description (pin, controller) is separate from application logic (timer, toggle)

## Hardware

Default: GPIO 2

Change `BLINK_GPIO` in `main.c` for your board. Set `GPIO_DT_ACTIVE_LOW` in the `led_spec.dt_flags` if your LED is active-low.

## Build and run

```bash
cd examples/blink_device_model
source ../../.env  # or your IDF setup
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodem2101 flash monitor
```

## Expected output

```
I (265) blink: === Boreas Blink -- Device Model Example ===
I (267) gpio_esp32: initialized
I (270) blink: Blinking GPIO 48 at 2 Hz. Ctrl+] to exit.
I (770) blink: toggle
I (1270) blink: toggle
I (1770) blink: toggle
...
```
