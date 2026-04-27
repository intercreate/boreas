/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Thread-safe shell output.
 * All output goes through the transport's write() with mutex protection.
 *
 * Zephyr reference: subsys/shell/shell_fprintf.c
 */

#include "zshell/shell.h"

#include <stdio.h>
#include <string.h>

/* VT100 color codes */
#define VT100_COLOR_RED     "\033[31m"
#define VT100_COLOR_GREEN   "\033[32m"
#define VT100_COLOR_YELLOW  "\033[33m"
#define VT100_COLOR_DEFAULT "\033[0m"

static void shell_vfprintf_color(const struct shell *sh, const char *color, const char *fmt,
				 va_list args)
{
	struct shell_ctx *ctx = (struct shell_ctx *)&sh->ctx;

	k_mutex_lock(&ctx->wr_mtx, K_FOREVER);

	size_t offset = 0;

	/* Color prefix */
	if (color != NULL) {
		size_t clen = strlen(color);
		memcpy(ctx->printf_buff, color, clen);
		offset = clen;
	}

	/* Format message */
	size_t avail = CONFIG_ZSHELL_PRINTF_BUFF_SIZE - offset;
	int len = vsnprintf(ctx->printf_buff + offset, avail, fmt, args);
	if (len > 0) {
		offset += ((size_t)len < avail) ? (size_t)len : (avail > 0 ? avail - 1 : 0);
	}

	/* Color reset */
	if (color != NULL && offset + 4 < CONFIG_ZSHELL_PRINTF_BUFF_SIZE) {
		memcpy(ctx->printf_buff + offset, VT100_COLOR_DEFAULT, 4);
		offset += 4;
	}

	/* Newline */
	if (offset + 2 < CONFIG_ZSHELL_PRINTF_BUFF_SIZE) {
		ctx->printf_buff[offset++] = '\r';
		ctx->printf_buff[offset++] = '\n';
	}

	/* Write via transport */
	if (sh->iface && sh->iface->api && sh->iface->api->write) {
		size_t cnt;
		sh->iface->api->write(sh->iface, ctx->printf_buff, offset, &cnt);
	}

	k_mutex_unlock(&ctx->wr_mtx);
}

void shell_print(const struct shell *sh, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	shell_vfprintf_color(sh, NULL, fmt, args);
	va_end(args);
}

void shell_error(const struct shell *sh, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	shell_vfprintf_color(sh, VT100_COLOR_RED, fmt, args);
	va_end(args);
}

void shell_warn(const struct shell *sh, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	shell_vfprintf_color(sh, VT100_COLOR_YELLOW, fmt, args);
	va_end(args);
}

void shell_info(const struct shell *sh, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	shell_vfprintf_color(sh, VT100_COLOR_GREEN, fmt, args);
	va_end(args);
}

void shell_write(const struct shell *sh, const void *data, size_t len)
{
	struct shell_ctx *ctx = (struct shell_ctx *)&sh->ctx;

	k_mutex_lock(&ctx->wr_mtx, K_FOREVER);
	if (sh->iface && sh->iface->api && sh->iface->api->write) {
		size_t cnt;
		sh->iface->api->write(sh->iface, data, len, &cnt);
	}
	k_mutex_unlock(&ctx->wr_mtx);
}
