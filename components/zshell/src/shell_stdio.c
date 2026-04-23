/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * stdio transport backend for the shell.
 *
 * Reads and writes go through stdin/stdout, so the actual hardware this
 * rides is whatever ESP-IDF's VFS console layer is bound to -- UART by
 * default, or USB-SERIAL-JTAG / USB-CDC when CONFIG_ESP_CONSOLE_* selects
 * them. No UART-specific calls here; the name is historical.
 *
 * Zephyr reference: subsys/shell/backends/shell_uart.c
 */

#include "zshell/shell.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "zsys/log.h"

LOG_MODULE_REGISTER(shell_stdio, LOG_LEVEL_INF);

struct shell_stdio_ctx {
    bool initialized;
};

static struct shell_stdio_ctx stdio_ctx;

static int shell_stdio_init(const struct shell_transport *transport,
                            void *context)
{
    /* Disable stdin line buffering so we get chars immediately */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Make stdin non-blocking so our read can timeout */
    int flags = fcntl(fileno(stdin), F_GETFL, 0);
    fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK);

    stdio_ctx.initialized = true;

    LOG_INF("Shell transport initialized (VFS console)");
    return 0;
}

static int shell_stdio_uninit(const struct shell_transport *transport)
{
    stdio_ctx.initialized = false;
    return 0;
}

static int shell_stdio_write(const struct shell_transport *transport,
                             const void *data, size_t length, size_t *cnt)
{
    if (!stdio_ctx.initialized) {
        *cnt = 0;
        return -1;
    }
    size_t written = fwrite(data, 1, length, stdout);
    fflush(stdout);
    *cnt = written;
    return 0;
}

static int shell_stdio_read(const struct shell_transport *transport,
                            void *data, size_t length, size_t *cnt)
{
    if (!stdio_ctx.initialized) {
        *cnt = 0;
        return -1;
    }

    /* Non-blocking read from stdin, then sleep if nothing available */
    int ch = fgetc(stdin);
    if (ch != EOF) {
        *(char *)data = (char)ch;
        *cnt = 1;
    } else {
        *cnt = 0;
        /* Small sleep to avoid busy-spinning */
        k_msleep(10);
    }
    return 0;
}

/* Transport API vtable */
static const struct shell_transport_api shell_stdio_api = {
    .init   = shell_stdio_init,
    .uninit = shell_stdio_uninit,
    .write  = shell_stdio_write,
    .read   = shell_stdio_read,
};

/* Public transport instance */
const struct shell_transport shell_transport_stdio = {
    .api = &shell_stdio_api,
    .ctx = &stdio_ctx,
};
