# Shell Demo

Demonstrates the Boreas shell (`zshell`) with hierarchical commands, tab completion, command history, and thread-safe output.

## Built-in Commands

| Command | Description |
|---------|-------------|
| `help [cmd]` | List all commands, or show help + subcommands for a specific command |
| `clear` | Clear the terminal screen |
| `echo [on\|off]` | Toggle character echo |
| `kernel uptime` | Print system uptime |
| `kernel heap` | Print free and minimum free heap |
| `log list` | List registered log modules and their current levels |
| `log level <module> <0-4>` | Set a module's runtime log level (0=NONE, 1=ERR, 2=WRN, 3=INF, 4=DBG) |
| `thread` | Print thread stack and CPU usage statistics |
| `app status` | Custom demo command: show app status |
| `app version` | Custom demo command: show version |
| `login <password>` | Unlock the shell (see below) |
| `logout` | Lock the shell again |

## Key Bindings

| Key | Action |
|-----|--------|
| Tab | Complete command or show options |
| Up/Down | Navigate command history |
| Left/Right | Move cursor within line |
| Home/End | Move cursor to start/end of line |
| Delete | Delete character at cursor |
| Backspace | Delete character before cursor |
| Ctrl-A | Move cursor to start of line |
| Ctrl-E | Move cursor to end of line |
| Ctrl-K | Delete from cursor to end of line |
| Ctrl-W | Delete word backward |
| Ctrl-C | Cancel current line |
| Ctrl-L | Clear screen |

## Build and Flash

```bash
cd examples/shell_demo
source ../../.env      # or your IDF setup
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

## Login / Logout

Start the demo locked: `login` is the selected root command, so the prompt
reads `login: `, echo shows `*`, and the typed line is passed to `login` as its
argument. Enter just the password (`boreas`), not `login boreas`:

```bash
idf.py -DLOGIN=y build      # rm sdkconfig first if switching an existing build
```

The default password is `boreas`. `login` purges the history entry holding the
password, turns obscuring off, restores the full command tree and the normal
prompt. `logout` reverses that. The composition mirrors Zephyr's
`samples/subsys/shell/shell_module` login sample; the shell provides the
primitives (`shell_obscure_set()`, `shell_set_root_cmd()`,
`shell_prompt_change()`, `shell_history_purge()`) and the application owns
the credential check.

## Adding Custom Commands

```c
#include <boreas/zshell/shell.h>

static int cmd_my_status(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "All systems go");
    return 0;
}

/* Subcommand set */
SHELL_STATIC_SUBCMD_SET_CREATE(my_subcmds,
    SHELL_CMD(status, NULL, "Show status", cmd_my_status)
);

/* Root command -- picked up by shell_init(), available automatically */
SHELL_CMD_REGISTER(myapp, &my_subcmds, "My application", NULL);
```

## Configuration

Enable the shell in `sdkconfig.defaults`:

```
CONFIG_ZSHELL=y
```

All features (history, VT100, tab completion, built-in commands) are enabled by default. See `components/zshell/Kconfig` for all options.
