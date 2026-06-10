/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2015 Intel Corporation   (upstream Zephyr original)
 * Copyright 2026 Intercreate             (Boreas port)
 *
 * Zephyr-compatible ring buffer (sys/ring_buffer.h). Near-verbatim port
 * of upstream Zephyr (include/zephyr/sys/ring_buffer.h,
 * lib/utils/ring_buffer.c -- both Apache-2.0), adapted only for the
 * Boreas include layout and toolchain shims (see sys/util.h).
 *
 * Divergences:
 *   - the upstream item-mode API (ring_buf_item_put/_get,
 *     ring_buf_item_init, RING_BUF_ITEM_DECLARE*) is intentionally NOT
 *     ported -- it is @deprecated upstream ("use <zephyr/sys/ringq.h>")
 *     and the byte-mode API below is what the UART IRQ pattern, the
 *     direct-UART shell transport, and the modbus serial backend use;
 *   - the implementation functions are K_ISR_SAFE (IRAM-resident) so
 *     the byte API is callable from ESP_INTR_FLAG_IRAM ISRs -- see
 *     src/ring_buf.c.
 *
 * Concurrency (code unchanged from upstream): the buffer is lock-free
 * for a SINGLE producer and a SINGLE consumer in separate contexts
 * (two threads, or thread + ISR) -- the producer writes only `put`,
 * the consumer writes only `get`, and the query inlines (is_empty,
 * size_get, space_get) read one index from each side as aligned
 * single-word loads, so they are tear-free from either side and return
 * conservatively stale answers. No field is volatile and no memory
 * barriers are issued, so:
 *   - do NOT busy-wait on the query inlines: in a call-free loop the
 *     compiler may legally hoist the index load and never observe the
 *     other side's progress. Block on a k_sem/k_event signaled by the
 *     other side instead;
 *   - on SMP, the buffer alone does not order the data bytes against
 *     the index publication: pair each handoff with a k_sem (or other
 *     acquire/release primitive) and do not drain past the handoff
 *     you were signaled for.
 * Multiple producers OR multiple consumers must serialize access
 * externally (e.g. a k_mutex or by locking interrupts).
 */

#pragma once

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

/* struct ring_buf's index width is selected by CONFIG_RING_BUFFER_LARGE
 * below, so the config must be visible wherever this struct is defined
 * -- include it explicitly rather than relying on a transitive chain
 * (the library and every consumer must agree on the layout). */
#include "sdkconfig.h"

#include "zephyr/sys/util.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL_HIDDEN */

#ifdef CONFIG_RING_BUFFER_LARGE
typedef uint32_t ring_buf_idx_t;
#define RING_BUFFER_MAX_SIZE        (UINT32_MAX / 2)
#define RING_BUFFER_SIZE_ASSERT_MSG "Size too big"
#else
typedef uint16_t ring_buf_idx_t;
#define RING_BUFFER_MAX_SIZE        (UINT16_MAX / 2)
#define RING_BUFFER_SIZE_ASSERT_MSG "Size too big, please enable CONFIG_RING_BUFFER_LARGE"
#endif

struct ring_buf_index {
	ring_buf_idx_t head;
	ring_buf_idx_t tail;
	ring_buf_idx_t base;
};

/** @endcond */

/**
 * @brief A structure to represent a ring buffer.
 */
struct ring_buf {
	/** @cond INTERNAL_HIDDEN */
	uint8_t *buffer;
	struct ring_buf_index put;
	struct ring_buf_index get;
	uint32_t size;
	/** @endcond */
};

/** @cond INTERNAL_HIDDEN */

uint32_t ring_buf_area_claim(struct ring_buf *buf, struct ring_buf_index *ring, uint8_t **data,
			     uint32_t size);
int ring_buf_area_finish(struct ring_buf *buf, struct ring_buf_index *ring, uint32_t size);

/**
 * @brief Force ring_buf internal states to a given value.
 *
 * Any value other than 0 makes sense only in validation-testing context.
 */
static inline void ring_buf_internal_reset(struct ring_buf *buf, ring_buf_idx_t value)
{
	buf->put.head = buf->put.tail = buf->put.base = value;
	buf->get.head = buf->get.tail = buf->get.base = value;
}

/** @endcond */

/**
 * @brief Statically initialize a ring buffer for byte data.
 *
 * For use in a struct ring_buf initializer with a caller-provided data
 * area, e.g. `struct ring_buf rb = RING_BUF_INIT(buf, sizeof(buf));`.
 *
 * @warning @a size8 must be <= RING_BUFFER_MAX_SIZE. Unlike
 * @ref RING_BUF_DECLARE (BUILD_ASSERT) and @ref ring_buf_init
 * (runtime __ASSERT), an initializer cannot check this; an oversized
 * buffer compiles silently and corrupts data without error once
 * cumulative traffic exceeds the index range (upstream has the same
 * unchecked macro).
 *
 * @param buf   Pointer to the data area (uint8_t[]).
 * @param size8 Size of the data area (in bytes).
 */
#define RING_BUF_INIT(buf, size8)                                                                  \
	{                                                                                          \
		.buffer = (buf),                                                                   \
		.size = (size8),                                                                   \
	}

/**
 * @brief Define and initialize a ring buffer for byte data.
 *
 * This macro establishes a ring buffer of an arbitrary size.
 * The basic storage unit is a byte.
 *
 * The ring buffer can be accessed outside the module where it is defined
 * using:
 *
 * @code extern struct ring_buf <name>; @endcode
 *
 * @note File-scope only: the macro expands to a BUILD_ASSERT plus two
 * declarations, so it cannot be prefixed with `static`, and at block
 * scope it would silently create a fresh automatic struct ring_buf per
 * call over one shared function-static data array.
 *
 * @param name  Name of the ring buffer.
 * @param size8 Size of ring buffer (in bytes).
 */
#define RING_BUF_DECLARE(name, size8)                                                              \
	BUILD_ASSERT((size8) <= RING_BUFFER_MAX_SIZE, RING_BUFFER_SIZE_ASSERT_MSG);                \
	static uint8_t __noinit _ring_buffer_data_##name[size8];                                   \
	struct ring_buf name = RING_BUF_INIT(_ring_buffer_data_##name, size8)

/**
 * @brief Initialize a ring buffer for byte data.
 *
 * This routine initializes a ring buffer, prior to its first use. It is only
 * used for ring buffers not defined using RING_BUF_DECLARE.
 *
 * @param buf  Address of ring buffer.
 * @param size Ring buffer size (in bytes).
 * @param data Ring buffer data area (uint8_t data[size]).
 */
static inline void ring_buf_init(struct ring_buf *buf, uint32_t size, uint8_t *data)
{
	__ASSERT(size <= RING_BUFFER_MAX_SIZE, RING_BUFFER_SIZE_ASSERT_MSG);

	buf->size = size;
	buf->buffer = data;
	ring_buf_internal_reset(buf, 0);
}

/**
 * @brief Determine if a ring buffer is empty.
 *
 * @param buf Address of ring buffer.
 *
 * @return true if the ring buffer is empty, or false if not.
 */
static inline bool ring_buf_is_empty(const struct ring_buf *buf)
{
	return buf->get.head == buf->put.tail;
}

/**
 * @brief Reset ring buffer state.
 *
 * @param buf Address of ring buffer.
 */
static inline void ring_buf_reset(struct ring_buf *buf)
{
	ring_buf_internal_reset(buf, 0);
}

/**
 * @brief Determine free space in a ring buffer.
 *
 * @param buf Address of ring buffer.
 *
 * @return Ring buffer free space (in bytes).
 */
static inline uint32_t ring_buf_space_get(const struct ring_buf *buf)
{
	ring_buf_idx_t allocated = buf->put.head - buf->get.tail;

	return buf->size - allocated;
}

/**
 * @brief Return ring buffer capacity.
 *
 * @param buf Address of ring buffer.
 *
 * @return Ring buffer capacity (in bytes).
 */
static inline uint32_t ring_buf_capacity_get(const struct ring_buf *buf)
{
	return buf->size;
}

/**
 * @brief Determine size of available data in a ring buffer.
 *
 * Counts committed-and-unread bytes only: bytes inside an outstanding
 * (unfinished) claim are counted neither here nor by
 * @ref ring_buf_space_get. (Wording from current upstream main; the
 * v3.7-era "used space" phrasing was misleading around claims.)
 *
 * @param buf Address of ring buffer.
 *
 * @return Ring buffer data size (in bytes).
 */
static inline uint32_t ring_buf_size_get(const struct ring_buf *buf)
{
	ring_buf_idx_t available = buf->put.tail - buf->get.head;

	return available;
}

/**
 * @brief Allocate buffer for writing data to a ring buffer.
 *
 * With this routine, memory copying can be reduced since internal ring buffer
 * can be used directly by the user. Once data is written to allocated area
 * number of bytes written must be confirmed (see @ref ring_buf_put_finish).
 *
 * @warning
 * Use cases involving multiple writers to the ring buffer must prevent
 * concurrent write operations, either by preventing all writers from
 * being preempted or by using a mutex to govern writes to the ring buffer.
 *
 * @note An outstanding claim must be completed with
 * @ref ring_buf_put_finish before any other put-side call (including
 * copy-mode @ref ring_buf_put): finishing rewinds the allocation head to
 * the committed tail, silently discarding whatever was still claimed.
 *
 * @param buf  Address of ring buffer.
 * @param data Pointer to the address. It is set to a location within
 *		ring buffer.
 * @param size Requested allocation size (in bytes).
 *
 * @return Size of allocated buffer which can be smaller than requested if
 *	   there is not enough free space or buffer wraps.
 */
static inline uint32_t ring_buf_put_claim(struct ring_buf *buf, uint8_t **data, uint32_t size)
{
	uint32_t space = ring_buf_space_get(buf);

	return ring_buf_area_claim(buf, &buf->put, data, MIN(size, space));
}

/**
 * @brief Indicate number of bytes written to allocated buffers.
 *
 * The number of bytes must be equal to or lower than the sum corresponding
 * to all preceding @ref ring_buf_put_claim invocations (or even 0). Surplus
 * bytes will be returned to the available free buffer space.
 *
 * @warning
 * Use cases involving multiple writers to the ring buffer must prevent
 * concurrent write operations, either by preventing all writers from
 * being preempted or by using a mutex to govern writes to the ring buffer.
 *
 * @param buf  Address of ring buffer.
 * @param size Number of valid bytes in the allocated buffers.
 *
 * @retval 0 Successful operation.
 * @retval -EINVAL Provided @a size exceeds the bytes claimed by preceding
 *	   @ref ring_buf_put_claim invocations and not yet finished.
 *	   (Upstream documents "exceeds free space", but the code -- here
 *	   and upstream -- checks the outstanding claimed amount.)
 */
static inline int ring_buf_put_finish(struct ring_buf *buf, uint32_t size)
{
	return ring_buf_area_finish(buf, &buf->put, size);
}

/**
 * @brief Write (copy) data to a ring buffer.
 *
 * This routine writes data to a ring buffer @a buf.
 *
 * @warning
 * Use cases involving multiple writers to the ring buffer must prevent
 * concurrent write operations, either by preventing all writers from
 * being preempted or by using a mutex to govern writes to the ring buffer.
 *
 * @param buf  Address of ring buffer.
 * @param data Address of data.
 * @param size Data size (in bytes).
 *
 * @return Number of bytes written.
 */
uint32_t ring_buf_put(struct ring_buf *buf, const uint8_t *data, uint32_t size);

/**
 * @brief Get address of a valid data in a ring buffer.
 *
 * With this routine, memory copying can be reduced since internal ring buffer
 * can be used directly by the user. Once data is processed it must be freed
 * using @ref ring_buf_get_finish.
 *
 * @warning
 * Use cases involving multiple reads of the ring buffer must prevent
 * concurrent read operations, either by preventing all readers from being
 * preempted or by using a mutex to govern reads to the ring buffer.
 *
 * @note An outstanding claim must be completed with
 * @ref ring_buf_get_finish before any other get-side call (including
 * copy-mode @ref ring_buf_get and @ref ring_buf_peek): finishing rewinds
 * the claim head to the freed tail, silently discarding whatever was
 * still claimed.
 *
 * @param buf  Address of ring buffer.
 * @param data Pointer to the address. It is set to a location within
 *		ring buffer.
 * @param size Requested size (in bytes).
 *
 * @return Number of valid bytes in the provided buffer which can be smaller
 *	   than requested if there is not enough free space or buffer wraps.
 */
static inline uint32_t ring_buf_get_claim(struct ring_buf *buf, uint8_t **data, uint32_t size)
{
	uint32_t buf_size = ring_buf_size_get(buf);

	return ring_buf_area_claim(buf, &buf->get, data, MIN(size, buf_size));
}

/**
 * @brief Indicate number of bytes read from claimed buffer.
 *
 * The number of bytes must be equal or lower than the sum corresponding to
 * all preceding @ref ring_buf_get_claim invocations (or even 0). Surplus
 * bytes will remain available in the buffer.
 *
 * @warning
 * Use cases involving multiple reads of the ring buffer must prevent
 * concurrent read operations, either by preventing all readers from being
 * preempted or by using a mutex to govern reads to the ring buffer.
 *
 * @param buf  Address of ring buffer.
 * @param size Number of bytes that can be freed.
 *
 * @retval 0 Successful operation.
 * @retval -EINVAL Provided @a size exceeds the bytes claimed by preceding
 *	   @ref ring_buf_get_claim invocations and not yet finished.
 *	   (Upstream documents "exceeds valid bytes", but the code -- here
 *	   and upstream -- checks the outstanding claimed amount.)
 */
static inline int ring_buf_get_finish(struct ring_buf *buf, uint32_t size)
{
	return ring_buf_area_finish(buf, &buf->get, size);
}

/**
 * @brief Read data from a ring buffer.
 *
 * This routine reads data from a ring buffer @a buf.
 *
 * @warning
 * Use cases involving multiple reads of the ring buffer must prevent
 * concurrent read operations, either by preventing all readers from being
 * preempted or by using a mutex to govern reads to the ring buffer.
 *
 * @param buf  Address of ring buffer.
 * @param data Address of the output buffer. Can be NULL to discard data.
 * @param size Data size (in bytes).
 *
 * @return Number of bytes written to the output buffer.
 */
uint32_t ring_buf_get(struct ring_buf *buf, uint8_t *data, uint32_t size);

/**
 * @brief Peek at data from a ring buffer.
 *
 * This routine reads data from a ring buffer @a buf without removing it from
 * the buffer.
 *
 * @warning
 * Use cases involving multiple reads of the ring buffer must prevent
 * concurrent read operations, either by preventing all readers from being
 * preempted or by using a mutex to govern reads to the ring buffer.
 *
 * @warning
 * Multiple calls to peek will result in the same data being 'peeked' multiple
 * times. To remove data, use either @ref ring_buf_get or @ref
 * ring_buf_get_claim followed by @ref ring_buf_get_finish with a non-zero
 * `size`.
 *
 * @note Internally peek claims and then unclaims (finishes with size 0),
 * which rewinds the get-side claim head -- so it must not be called while
 * a @ref ring_buf_get_claim is outstanding (the claim would be silently
 * voided and peek would return bytes past it).
 *
 * @param buf  Address of ring buffer.
 * @param data Address of the output buffer. Cannot be NULL.
 * @param size Data size (in bytes).
 *
 * @return Number of bytes written to the output buffer.
 */
uint32_t ring_buf_peek(struct ring_buf *buf, uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif
