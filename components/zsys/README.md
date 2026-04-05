# zsys

System services built on zkernel: logging, watchdog, thread analysis, retry/backoff.

## Logging

Zephyr-compatible structured logging with compile-time stripping, runtime level control, backend abstraction, and optional deferred (async) output.

### Basic usage

```c
#include "zsys/log.h"

LOG_MODULE_REGISTER(my_module, LOG_LEVEL_INF);

void my_function(void) {
    LOG_INF("started with %d items", count);
    LOG_WRN("battery at %d%%", level);
    LOG_ERR("sensor %d failed", id);
    LOG_DBG("raw value: 0x%04x", val);
}
```

One `LOG_MODULE_REGISTER` per `.c` file. To reference a module from another file:

```c
LOG_MODULE_DECLARE(my_module);
```

### How it works

```
LOG_INF("hello %d", 42)
  --> compile-time check: level <= CONFIG_ZSYS_LOG_MAX_LEVEL?
  --> runtime check: level <= module's current level?
  --> vsnprintf into stack-local struct log_msg
  --> dispatch to all registered backends (sync)
      or k_msgq_put into ring buffer (deferred)
```

When `CONFIG_ZSYS_LOG_MODULE=n`, macros fall back to `ESP_LOG*` directly -- zero overhead.

### Runtime level control

```c
zsys_log_set_level("my_module", LOG_LEVEL_DBG);  // programmatic
zsys_log_list_modules();                          // print all modules
```

Or via shell: `log level my_module 4`

### Deferred mode

Enable `CONFIG_ZSYS_LOG_MODE_DEFERRED=y` for non-blocking, ISR-safe logging. Messages go into a `k_msgq` ring buffer and a dedicated thread dispatches them to backends.

Cost: ~2KB stack + (buffer_count x ~128 bytes). Default: 2KB + 4KB = 6KB.

### Custom backends

```c
static void my_put(const struct log_backend *b, const struct log_msg *msg) {
    char buf[128];
    zsys_log_format_msg(msg, buf, sizeof(buf));
    uart_write(buf);
}

static const struct log_backend_api my_api = { .put = my_put };
LOG_BACKEND_DEFINE(my_backend, &my_api, NULL);
```

Backends are registered automatically via constructor. The default ESP backend outputs in standard ESP-IDF format.

### Panic mode

`zsys_log_panic()` is called automatically from `k_fatal_error()` via weak symbol. Drains the deferred queue synchronously and switches to direct output.

### Log levels

| Level | Value | Macro |
|-------|-------|-------|
| None | 0 | -- |
| Error | 1 | `LOG_ERR` |
| Warning | 2 | `LOG_WRN` |
| Info | 3 | `LOG_INF` |
| Debug | 4 | `LOG_DBG` |

## Watchdog

Per-subsystem software watchdog. A supervisor task checks that registered subsystems have fed their watchdog within the configured window.

```c
#include "zsys/watchdog.h"

struct zsys_watchdog_entry wd;
zsys_watchdog_register(&wd, "comms", K_SECONDS(30), my_timeout_cb);
zsys_watchdog_feed(&wd);
```

Requires `CONFIG_ZSYS_WATCHDOG=y`.

## Thread Analyzer

Runtime thread stack and CPU usage analysis.

```c
#include "zsys/thread_analyzer.h"

zsys_thread_analyzer_print();  // prints all tasks' stack/CPU stats
```

Requires `CONFIG_ZSYS_THREAD_ANALYZER=y`. Also available as shell command: `thread`.

## Retry/Backoff

Consistent retry logic with fixed, exponential, or jitter strategies.

```c
#include "zsys/retry.h"

struct retry_ctx ctx;
retry_ctx_init(&ctx, 5, 1000, 60000, RETRY_EXPONENTIAL);

while (!connected && !retry_exhausted(&ctx)) {
    attempt_connect();
    k_timeout_t delay = retry_next_delay(&ctx);
    k_msleep(delay.ms);
}
```

Strategies: `RETRY_FIXED`, `RETRY_EXPONENTIAL`, `RETRY_EXP_JITTER`.

Requires `CONFIG_ZSYS_RETRY=y` (default).

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_ZSYS_LOG_MODULE` | y | Enable logging subsystem |
| `CONFIG_ZSYS_LOG_MAX_LEVEL` | 4 | Compile-time max level (0-4) |
| `CONFIG_ZSYS_LOG_MODE_DEFERRED` | n | Deferred output via ring buffer + thread |
| `CONFIG_ZSYS_LOG_BUFFER_COUNT` | 32 | Deferred queue depth |
| `CONFIG_ZSYS_LOG_MSG_MAX_LEN` | 80 | Max text per message |
| `CONFIG_ZSYS_LOG_MAX_BACKENDS` | 4 | Max registered backends |
| `CONFIG_ZSYS_LOG_THREAD_STACK_SIZE` | 2048 | Deferred output thread stack |
| `CONFIG_ZSYS_LOG_THREAD_PRIORITY` | 2 | Deferred output thread priority |
| `CONFIG_ZSYS_THREAD_ANALYZER` | n | Enable thread analyzer |
| `CONFIG_ZSYS_WATCHDOG` | n | Enable software watchdog |
| `CONFIG_ZSYS_WATCHDOG_MAX_ENTRIES` | 8 | Max watchdog entries |
| `CONFIG_ZSYS_RETRY` | y | Enable retry/backoff primitive |
