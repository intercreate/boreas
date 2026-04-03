/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * UART transport backend for the shell.
 * Uses ESP-IDF UART driver with polling reads.
 *
 * Zephyr reference: subsys/shell/backends/shell_uart.c
 */

#include "zshell/shell.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"

static const char *TAG = "shell_uart";

struct shell_uart_ctx {
    bool initialized;
};

static struct shell_uart_ctx uart_ctx;

static int shell_uart_init(const struct shell_transport *transport,
                           void *context)
{
    /* ESP-IDF already sets up UART0 as the console via VFS.
     * We read/write through stdin/stdout to cooperate with IDF's
     * console infrastructure instead of fighting it. */

    /* Disable stdin line buffering so we get chars immediately */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Make stdin non-blocking so our read can timeout */
    int flags = fcntl(fileno(stdin), F_GETFL, 0);
    fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK);

    uart_ctx.initialized = true;

    ESP_LOGI(TAG, "Shell transport initialized (VFS console)");
    return 0;
}

static int shell_uart_uninit(const struct shell_transport *transport)
{
    uart_ctx.initialized = false;
    return 0;
}

static int shell_uart_write(const struct shell_transport *transport,
                            const void *data, size_t length, size_t *cnt)
{
    if (!uart_ctx.initialized) {
        *cnt = 0;
        return -1;
    }
    /* Write through stdout -- goes through VFS to UART */
    size_t written = fwrite(data, 1, length, stdout);
    fflush(stdout);
    *cnt = written;
    return 0;
}

static int shell_uart_read(const struct shell_transport *transport,
                           void *data, size_t length, size_t *cnt)
{
    if (!uart_ctx.initialized) {
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
static const struct shell_transport_api shell_uart_api = {
    .init   = shell_uart_init,
    .uninit = shell_uart_uninit,
    .write  = shell_uart_write,
    .read   = shell_uart_read,
};

/* Public transport instance */
const struct shell_transport shell_transport_uart = {
    .api = &shell_uart_api,
    .ctx = &uart_ctx,
};
