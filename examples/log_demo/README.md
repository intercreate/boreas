# Logging Demo

Demonstrates the Boreas logging subsystem (`zsys/log`) -- Zephyr-compatible structured logging over ESP-IDF.

## What it shows

- **`LOG_MODULE_REGISTER`** -- per-module registration with a default log level
- **`LOG_ERR/WRN/INF/DBG`** -- four severity levels, compile-time strippable
- **Runtime level control** -- `zsys_log_set_level()` to raise/lower the floor at runtime
- **Custom backend** -- `LOG_BACKEND_DEFINE` to route messages to any transport
- **Module listing** -- `zsys_log_get_module_count()` / `zsys_log_get_module_info()` to enumerate registered modules

## How it works

```
LOG_INF("hello %d", 42)
    |
    v
Compile-time check: level <= CONFIG_ZSYS_LOG_MAX_LEVEL?
    |  (stripped entirely if too verbose)
    v
Runtime check: level <= module's current level?
    |  (suppressed silently if filtered)
    v
vsnprintf into stack-local struct log_msg
    |
    v
Dispatch to all registered backends
    |-- ESP backend (default): printf in ESP-IDF format
    |-- Counter backend (this demo): increment per-level counters
    `-- (your backend): LOG_BACKEND_DEFINE(name, &api, ctx)
```

In **synchronous mode** (default), backends are called inline. In **deferred mode** (`CONFIG_ZSYS_LOG_MODE_DEFERRED=y`), messages go into a `k_msgq` ring buffer and a dedicated thread dispatches them -- ISR-safe.

## Build and flash

```bash
cd examples/log_demo
source ../../.env      # or your IDF setup
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

## Expected output

```
I (265) log_demo: === Boreas Logging Demo ===
I (266) log_demo: --- Log Levels ---
E (266) log_demo: Error: sensor 3 not responding
W (267) log_demo: Warning: battery at 15%
I (267) log_demo: Info: system started with 1 modules
D (268) log_demo: Debug: internal state x=42 y=7
I (370) log_demo: --- Runtime Level Control ---
I (370) log_demo: Current level: DBG (all messages visible)
W (371) log_demo: This WRN message passes the filter
E (371) log_demo: This ERR message passes the filter
I (372) log_demo: Level restored to DBG -- all messages visible again
I (473) log_demo: --- Registered Modules ---
I (473) log_demo:   [0] log_demo          level=4
I (574) log_demo: --- Custom Backend (counter) ---
I (574) log_demo: Messages counted: ERR=2 WRN=2 INF=12 DBG=1
I (575) log_demo: === Demo complete ===
```

## Custom backend example

```c
#include "zsys/log.h"

static void my_put(const struct log_backend *b, const struct log_msg *msg)
{
    char buf[128];
    zsys_log_format_msg(msg, buf, sizeof(buf));
    my_transport_write(buf);   /* UART, RTT, network, file, etc. */
}

static const struct log_backend_api my_api = { .put = my_put };
LOG_BACKEND_DEFINE(my_backend, &my_api, NULL);
```

The backend is registered automatically at startup via `__attribute__((constructor))` -- no manual wiring needed.

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_ZSYS_LOG_MODULE` | n | Enable the logging subsystem |
| `CONFIG_ZSYS_LOG_MAX_LEVEL` | 4 (DBG) | Compile-time maximum level (0=off, 1=ERR, 2=WRN, 3=INF, 4=DBG) |
| `CONFIG_ZSYS_LOG_MODE_DEFERRED` | n | Enable deferred mode (ring buffer + output thread) |
| `CONFIG_ZSYS_LOG_BUFFER_COUNT` | 32 | Message queue depth (deferred mode) |
| `CONFIG_ZSYS_LOG_MSG_MAX_LEN` | 80 | Max text length per message |
| `CONFIG_ZSYS_LOG_MAX_BACKENDS` | 4 | Maximum number of backends |

When `CONFIG_ZSYS_LOG_MODULE` is disabled, `LOG_*` macros fall back to `ESP_LOG*` with zero overhead.
