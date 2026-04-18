# zshell

Zephyr-style interactive shell with hierarchical commands, tab completion, command history, and VT100 terminal support. Runs over UART via ESP-IDF's VFS console layer.

**Note:** USB-CDC is not currently tested and may have issues with the non-blocking stdin polling used by the UART transport. A dedicated USB-CDC transport backend may be needed.

## Quick start

```c
#include "zshell/shell.h"

static struct shell my_shell = { .name = "app" };

void app_main(void) {
    shell_init(&my_shell, &shell_transport_uart, "boreas> ");
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

The shell is transport-agnostic. `shell_transport_uart` is the default (VFS stdio). Custom transports implement:

```c
struct shell_transport_api {
    int (*init)(const struct shell_transport *t, void *ctx);
    int (*uninit)(const struct shell_transport *t);
    int (*write)(const struct shell_transport *t, const void *data, size_t len, size_t *cnt);
    int (*read)(const struct shell_transport *t, void *data, size_t len, size_t *cnt);
};
```

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
