/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2015 Intel Corporation   (upstream Zephyr original)
 * Copyright 2026 Intercreate             (Boreas port)
 *
 * Zephyr-compatible ring buffer. Near-verbatim port of upstream Zephyr
 * lib/utils/ring_buffer.c (Apache-2.0), adapted only for the Boreas
 * include layout and toolchain shims:
 *   - upstream's lowercase min() (from <zephyr/sys/minmax.h>) -> MIN
 *     (sys/util.h);
 *   - the deprecated item-mode API (ring_buf_item_put/_get) is not
 *     ported -- see sys/ring_buffer.h.
 *
 * The index scheme is upstream's base-offset design: head/tail/base are
 * ring_buf_idx_t (uint16_t, or uint32_t with CONFIG_RING_BUFFER_LARGE),
 * the byte offset is (head - base) with a single wrap correction, and
 * RING_BUFFER_MAX_SIZE caps size to half the index range so unsigned
 * subtractions disambiguate full vs empty. Keep ring_buf_idx_t exactly
 * as the header declares it -- widening only one side breaks the
 * empty/full discrimination.
 */

#include <errno.h>
#include <string.h>

#include "sdkconfig.h" /* CONFIG_RING_BUFFER_LARGE selects the index width */

#include "zephyr/sys/ring_buffer.h"
#include "zephyr/sys/util.h"

uint32_t ring_buf_area_claim(struct ring_buf *buf, struct ring_buf_index *ring, uint8_t **data,
			     uint32_t size)
{
	ring_buf_idx_t head_offset, wrap_size;

	head_offset = ring->head - ring->base;
	if (unlikely(head_offset >= buf->size)) {
		/* ring->base is not yet adjusted */
		head_offset -= buf->size;
	}
	wrap_size = buf->size - head_offset;
	size = MIN(size, wrap_size);

	*data = &buf->buffer[head_offset];
	ring->head += size;

	return size;
}

int ring_buf_area_finish(struct ring_buf *buf, struct ring_buf_index *ring, uint32_t size)
{
	ring_buf_idx_t claimed_size, tail_offset;

	claimed_size = ring->head - ring->tail;
	if (unlikely(size > claimed_size)) {
		return -EINVAL;
	}

	ring->tail += size;
	ring->head = ring->tail;

	tail_offset = ring->tail - ring->base;
	if (unlikely(tail_offset >= buf->size)) {
		/* we wrapped: adjust ring->base */
		ring->base += buf->size;
	}

	return 0;
}

uint32_t ring_buf_put(struct ring_buf *buf, const uint8_t *data, uint32_t size)
{
	uint8_t *dst;
	uint32_t partial_size;
	uint32_t total_size = 0U;
	int err;

	do {
		partial_size = ring_buf_put_claim(buf, &dst, size);
		if (partial_size == 0) {
			break;
		}
		memcpy(dst, data, partial_size);
		total_size += partial_size;
		size -= partial_size;
		data += partial_size;
	} while (size != 0);

	err = ring_buf_put_finish(buf, total_size);
	__ASSERT_NO_MSG(err == 0);
	ARG_UNUSED(err);

	return total_size;
}

uint32_t ring_buf_get(struct ring_buf *buf, uint8_t *data, uint32_t size)
{
	uint8_t *src;
	uint32_t partial_size;
	uint32_t total_size = 0U;
	int err;

	do {
		partial_size = ring_buf_get_claim(buf, &src, size);
		if (partial_size == 0) {
			break;
		}
		if (data) {
			memcpy(data, src, partial_size);
			data += partial_size;
		}
		total_size += partial_size;
		size -= partial_size;
	} while (size != 0);

	err = ring_buf_get_finish(buf, total_size);
	__ASSERT_NO_MSG(err == 0);
	ARG_UNUSED(err);

	return total_size;
}

uint32_t ring_buf_peek(struct ring_buf *buf, uint8_t *data, uint32_t size)
{
	uint8_t *src;
	uint32_t partial_size;
	uint32_t total_size = 0U;
	int err;

	do {
		partial_size = ring_buf_get_claim(buf, &src, size);
		if (partial_size == 0) {
			break;
		}
		__ASSERT_NO_MSG(data != NULL);
		memcpy(data, src, partial_size);
		data += partial_size;
		total_size += partial_size;
		size -= partial_size;
	} while (size != 0);

	/* effectively unclaim total_size bytes */
	err = ring_buf_get_finish(buf, 0);
	__ASSERT_NO_MSG(err == 0);
	ARG_UNUSED(err);

	return total_size;
}
