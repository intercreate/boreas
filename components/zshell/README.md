# zshell

Zephyr-style interactive shell with hierarchical commands, tab completion, command history, and VT100 terminal support. Runs over ESP-IDF's VFS console layer (UART by default; USB-SERIAL-JTAG or USB-CDC when configured).

The VFS layer also supports USB-SERIAL-JTAG (on chips that have the peripheral: ESP32-S3/C3/C5/C6/P4; not on classic ESP32 or S2). The shell transport is unchanged -- see "Console over USB-Serial-JTAG" below.

## Quick start

```c
#include "zshell/shell.h"

static struct shell my_shell = { .name = "app" };

void app_main(void) {
    shell_init(&my_shell, &shell_transport_stdio, "boreas> ");
}
```

Enable in sdkconfig:

```
CONFIG_ZSHELL=y
```

## Registering commands

```c
#include "zshell/shell.h"

static int cmd_status(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "uptime: %lld ms", k_uptime_get());
    return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "resetting...");
    esp_restart();
    return 0;
}

// Subcommand set
SHELL_STATIC_SUBCMD_SET_CREATE(app_subcmds,
    SHELL_CMD(status, NULL, "Show status", cmd_status)
    SHELL_CMD(reset,  NULL, "Reset device", cmd_reset)
);

// Root command -- picked up by shell_init(), no manual wiring
SHELL_CMD_REGISTER(app, &app_subcmds, "Application commands", NULL);
```

Commands are emplaced into the `.shell_root_cmds` linker section on ESP targets (iterated by `shell_init()`), or registered via a constructor fallback on the macOS host test target. Either way they're sorted alphabetically before the shell thread starts.

**Library note:** Linker scripts do not pull archive members — only unresolved-symbol references do. If you call `SHELL_CMD_REGISTER()` from inside a library component, the object file may still be stripped if nothing else in it is externally referenced. Put user-facing commands in `main/`, or make sure the enclosing TU exposes another referenced symbol (the built-in commands do this by exporting a `shell_builtins_*_register()` function). See `docs/linker-section-registration.md`.

## Output functions

All output is mutex-protected and safe to call from any thread.

```c
shell_print(sh, "normal output: %d", val);
shell_info(sh, "info (green)");
shell_warn(sh, "warning (yellow)");
shell_error(sh, "error (red)");
shell_write(sh, raw_data, len);
```

## Built-in commands

| Command | Requires | Description |
|---------|----------|-------------|
| `help [cmd]` | -- | List commands or show help for a specific command |
| `clear` | -- | Clear terminal (VT100) |
| `echo [on\|off]` | -- | Toggle character echo |
| `kernel uptime` | `CONFIG_ZSHELL_CMD_KERNEL` | System uptime in ms |
| `kernel heap` | `CONFIG_ZSHELL_CMD_KERNEL` | Free and minimum free heap |
| `log list` | `CONFIG_ZSHELL_CMD_LOG` | List log modules and levels |
| `log level <mod> <0-4>` | `CONFIG_ZSHELL_CMD_LOG` | Set module log level |
| `thread` | `CONFIG_ZSHELL_CMD_THREAD` | Thread stack/CPU statistics |

## Key bindings

| Key | Action |
|-----|--------|
| Tab | Complete command or show options |
| Up/Down | Navigate command history |
| Left/Right | Move cursor |
| Home/End | Start/end of line |
| Backspace | Delete before cursor |
| Delete | Delete at cursor |
| Ctrl-A | Start of line |
| Ctrl-E | End of line |
| Ctrl-K | Kill to end of line |
| Ctrl-W | Delete word backward |
| Ctrl-C | Cancel line |
| Ctrl-L | Clear screen |

## Transport abstraction

The shell is transport-agnostic. `shell_transport_stdio` is the default (VFS stdio). Custom transports implement:

```c
struct shell_transport_api {
    int (*init)(const struct shell_transport *t, void *ctx);
    int (*uninit)(const struct shell_transport *t);
    int (*write)(const struct shell_transport *t, const void *data, size_t len, size_t *cnt);
    int (*read)(const struct shell_transport *t, void *data, size_t len, size_t *cnt);
};
```

## Console over USB-Serial-JTAG

ESP-IDF's built-in `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` makes stdout flow over the native USB peripheral, but the default VFS binding is **output-only** -- `fgetc(stdin)` returns `EOF` forever and the shell sees no keystrokes. You need to install the USB-SERIAL-JTAG RX driver and rebind VFS before `shell_init()`:

```c
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif

void app_main(void) {
    /* ... other init ... */

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t usj_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usj_cfg));
    ESP_ERROR_CHECK(usb_serial_jtag_vfs_register());
    usb_serial_jtag_vfs_use_driver();
#endif

    shell_init(&my_shell, &shell_transport_stdio, NULL);
}
```

And add `esp_driver_usb_serial_jtag` to your component's `REQUIRES` (gate on the same Kconfig so non-USJ builds don't pull it in):

```cmake
set(deps ...)
if(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    list(APPEND deps "esp_driver_usb_serial_jtag")
endif()
idf_component_register(SRCS ${srcs} REQUIRES ${deps} ...)
```

Why this lives in the app and not in boreas: USB-SERIAL-JTAG availability, header paths, and Kconfig symbol names have drifted across chipsets and IDF versions. Wrapping it in boreas would risk baking in a chip- or version-specific assumption. The transport (`shell_transport_stdio`) works unmodified in both cases -- it's the driver/VFS binding underneath that differs.

Sanity checks if keys aren't reaching the shell:

- Host connected before the driver install? The host enumerates lazily; a 100ms delay before install is sometimes needed on slow hosts.
- Line endings: `usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR)` may be needed if your terminal sends `\r` only.
- Confirm `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` is actually primary (menuconfig -> Component config -> ESP System Settings -> Channel for console output).

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_ZSHELL` | n | Enable shell |
| `CONFIG_ZSHELL_CMD_BUFF_SIZE` | 128 | Command line buffer |
| `CONFIG_ZSHELL_PRINTF_BUFF_SIZE` | 256 | Output format buffer |
| `CONFIG_ZSHELL_MAX_ROOT_CMDS` | 32 | Max root commands |
| `CONFIG_ZSHELL_MAX_ARGC` | 12 | Max arguments per command |
| `CONFIG_ZSHELL_THREAD_STACK_SIZE` | 4096 | Shell thread stack |
| `CONFIG_ZSHELL_THREAD_PRIORITY` | 5 | Shell thread priority |
| `CONFIG_ZSHELL_HISTORY` | y | Command history |
| `CONFIG_ZSHELL_HISTORY_DEPTH` | 10 | History entries |
| `CONFIG_ZSHELL_VT100` | y | Arrow keys, cursor movement |
| `CONFIG_ZSHELL_TAB_COMPLETION` | y | Tab completion |
| `CONFIG_ZSHELL_ECHO` | y | Character echo |
| `CONFIG_ZSHELL_CMD_KERNEL` | y | Built-in kernel commands |
| `CONFIG_ZSHELL_CMD_LOG` | y | Built-in log commands |
| `CONFIG_ZSHELL_CMD_THREAD` | y | Built-in thread command |
