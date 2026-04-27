/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zsys/retry.h"

#include "esp_random.h"

k_timeout_t retry_next_delay(struct retry_ctx *ctx)
{
	if (retry_exhausted(ctx)) {
		return K_NO_WAIT;
	}

	uint32_t delay_ms;

	switch (ctx->strategy) {
	case RETRY_FIXED:
		delay_ms = ctx->base_delay_ms;
		break;

	case RETRY_EXPONENTIAL: {
		delay_ms = ctx->base_delay_ms;
		for (uint8_t i = 0; i < ctx->attempt && delay_ms < ctx->max_delay_ms; i++) {
			delay_ms *= 2;
		}
		if (delay_ms > ctx->max_delay_ms) {
			delay_ms = ctx->max_delay_ms;
		}
		break;
	}

	case RETRY_EXP_JITTER: {
		delay_ms = ctx->base_delay_ms;
		for (uint8_t i = 0; i < ctx->attempt && delay_ms < ctx->max_delay_ms; i++) {
			delay_ms *= 2;
		}
		if (delay_ms > ctx->max_delay_ms) {
			delay_ms = ctx->max_delay_ms;
		}
		/* Add 0-25% random jitter, then re-clamp */
		uint32_t jitter = (esp_random() % (delay_ms / 4 + 1));
		delay_ms += jitter;
		if (delay_ms > ctx->max_delay_ms) {
			delay_ms = ctx->max_delay_ms;
		}
		break;
	}

	default:
		delay_ms = ctx->base_delay_ms;
		break;
	}

	ctx->attempt++;
	return K_MSEC(delay_ms);
}
