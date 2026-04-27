/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Retry/backoff primitive for consistent reconnection logic.
 *
 * Usage:
 *   struct retry_ctx ctx;
 *   retry_ctx_init(&ctx, 5, 1000, 60000, RETRY_EXPONENTIAL);
 *
 *   while (!connected && !retry_exhausted(&ctx)) {
 *       attempt_connect();
 *       k_timeout_t delay = retry_next_delay(&ctx);
 *       k_sleep(delay);
 *   }
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "zephyr/sys/time_units.h"

#ifdef __cplusplus
extern "C" {
#endif

enum retry_strategy {
	RETRY_FIXED,       /* constant delay */
	RETRY_EXPONENTIAL, /* base * 2^attempt, capped at max */
	RETRY_EXP_JITTER,  /* exponential + random jitter (0-25%) */
};

struct retry_ctx {
	uint8_t attempt;
	uint8_t max_attempts;
	uint32_t base_delay_ms;
	uint32_t max_delay_ms;
	enum retry_strategy strategy;
};

static inline void retry_ctx_init(struct retry_ctx *ctx, uint8_t max_attempts,
				  uint32_t base_delay_ms, uint32_t max_delay_ms,
				  enum retry_strategy strategy)
{
	ctx->attempt = 0;
	ctx->max_attempts = max_attempts;
	ctx->base_delay_ms = base_delay_ms;
	ctx->max_delay_ms = max_delay_ms;
	ctx->strategy = strategy;
}

static inline void retry_reset(struct retry_ctx *ctx)
{
	ctx->attempt = 0;
}

static inline bool retry_exhausted(const struct retry_ctx *ctx)
{
	return ctx->attempt >= ctx->max_attempts;
}

static inline uint8_t retry_attempt_get(const struct retry_ctx *ctx)
{
	return ctx->attempt;
}

/**
 * Compute the next delay and increment the attempt counter.
 * Returns K_NO_WAIT if retries are exhausted.
 */
k_timeout_t retry_next_delay(struct retry_ctx *ctx);

#ifdef __cplusplus
}
#endif
