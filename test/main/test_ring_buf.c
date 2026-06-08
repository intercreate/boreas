/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <errno.h>
#include <string.h>

#include "unity.h"
#include "zephyr/sys/ring_buffer.h"

/* ----------------------------------------------------------------
 * Accounting: capacity / size / space / is_empty
 * ---------------------------------------------------------------- */

static void test_ring_buf_init_accounting(void)
{
	uint8_t storage[16];
	struct ring_buf rb;

	ring_buf_init(&rb, sizeof(storage), storage);

	TEST_ASSERT_EQUAL(16, ring_buf_capacity_get(&rb));
	TEST_ASSERT_EQUAL(0, ring_buf_size_get(&rb));
	TEST_ASSERT_EQUAL(16, ring_buf_space_get(&rb));
	TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
}

static void test_ring_buf_put_get_roundtrip(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	const uint8_t in[] = {1, 2, 3, 4, 5};
	uint8_t out[8] = {0};

	ring_buf_init(&rb, sizeof(storage), storage);

	TEST_ASSERT_EQUAL(5, ring_buf_put(&rb, in, sizeof(in)));
	TEST_ASSERT_EQUAL(5, ring_buf_size_get(&rb));
	TEST_ASSERT_EQUAL(11, ring_buf_space_get(&rb));
	TEST_ASSERT_FALSE(ring_buf_is_empty(&rb));

	TEST_ASSERT_EQUAL(5, ring_buf_get(&rb, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, sizeof(in));
	TEST_ASSERT_EQUAL(0, ring_buf_size_get(&rb));
	TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
}

/* ----------------------------------------------------------------
 * Full / partial semantics
 * ---------------------------------------------------------------- */

static void test_ring_buf_put_saturates_at_capacity(void)
{
	uint8_t storage[8];
	struct ring_buf rb;
	uint8_t in[12];

	for (int i = 0; i < (int)sizeof(in); i++) {
		in[i] = (uint8_t)i;
	}
	ring_buf_init(&rb, sizeof(storage), storage);

	/* Only capacity bytes fit; the rest is refused. */
	TEST_ASSERT_EQUAL(8, ring_buf_put(&rb, in, sizeof(in)));
	TEST_ASSERT_EQUAL(0, ring_buf_space_get(&rb));
	/* A further put on a full buffer writes nothing. */
	TEST_ASSERT_EQUAL(0, ring_buf_put(&rb, in, 1));

	/* The zero-copy claim path (what the UART IRQ uses) must also
	 * report no room: put_claim returns 0 on a full buffer. */
	uint8_t *dst;
	TEST_ASSERT_EQUAL(0, ring_buf_put_claim(&rb, &dst, 1));
}

static void test_ring_buf_get_more_than_available(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	const uint8_t in[] = {9, 8, 7};
	uint8_t out[8] = {0};

	ring_buf_init(&rb, sizeof(storage), storage);
	ring_buf_put(&rb, in, sizeof(in));

	/* Asking for more than present returns only what is there. */
	TEST_ASSERT_EQUAL(3, ring_buf_get(&rb, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 3);
}

static void test_ring_buf_get_null_discards(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	const uint8_t in[] = {1, 2, 3, 4};

	ring_buf_init(&rb, sizeof(storage), storage);
	ring_buf_put(&rb, in, sizeof(in));

	/* NULL output discards without copying. */
	TEST_ASSERT_EQUAL(4, ring_buf_get(&rb, NULL, sizeof(in)));
	TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
}

/* ----------------------------------------------------------------
 * peek does not consume
 * ---------------------------------------------------------------- */

static void test_ring_buf_peek_does_not_consume(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	const uint8_t in[] = {5, 6, 7, 8};
	uint8_t out[4] = {0};

	ring_buf_init(&rb, sizeof(storage), storage);
	ring_buf_put(&rb, in, sizeof(in));

	TEST_ASSERT_EQUAL(4, ring_buf_peek(&rb, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, sizeof(in));
	/* Still all there after peek. */
	TEST_ASSERT_EQUAL(4, ring_buf_size_get(&rb));

	/* Second peek returns the same bytes. */
	memset(out, 0, sizeof(out));
	TEST_ASSERT_EQUAL(4, ring_buf_peek(&rb, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, sizeof(in));
}

/* ----------------------------------------------------------------
 * reset
 * ---------------------------------------------------------------- */

static void test_ring_buf_reset_empties(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	const uint8_t in[] = {1, 2, 3};

	ring_buf_init(&rb, sizeof(storage), storage);
	ring_buf_put(&rb, in, sizeof(in));
	TEST_ASSERT_FALSE(ring_buf_is_empty(&rb));

	ring_buf_reset(&rb);
	TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
	TEST_ASSERT_EQUAL(0, ring_buf_size_get(&rb));
	TEST_ASSERT_EQUAL(16, ring_buf_space_get(&rb));
}

/* ----------------------------------------------------------------
 * claim / finish (zero-copy paths)
 * ---------------------------------------------------------------- */

static void test_ring_buf_put_claim_finish(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	uint8_t *dst;
	uint8_t out[8] = {0};

	ring_buf_init(&rb, sizeof(storage), storage);

	uint32_t claimed = ring_buf_put_claim(&rb, &dst, 6);
	TEST_ASSERT_EQUAL(6, claimed);
	for (uint32_t i = 0; i < claimed; i++) {
		dst[i] = (uint8_t)(0x10 + i);
	}
	TEST_ASSERT_EQUAL(0, ring_buf_put_finish(&rb, 6));
	TEST_ASSERT_EQUAL(6, ring_buf_size_get(&rb));

	TEST_ASSERT_EQUAL(6, ring_buf_get(&rb, out, 6));
	for (int i = 0; i < 6; i++) {
		TEST_ASSERT_EQUAL_UINT8(0x10 + i, out[i]);
	}
}

static void test_ring_buf_put_finish_surplus_returns_space(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	uint8_t *dst;

	ring_buf_init(&rb, sizeof(storage), storage);

	/* Claim 10 but only commit 4; the surplus 6 returns to free space. */
	TEST_ASSERT_EQUAL(10, ring_buf_put_claim(&rb, &dst, 10));
	TEST_ASSERT_EQUAL(0, ring_buf_put_finish(&rb, 4));
	TEST_ASSERT_EQUAL(4, ring_buf_size_get(&rb));
	TEST_ASSERT_EQUAL(12, ring_buf_space_get(&rb));
}

static void test_ring_buf_put_finish_over_claim_einval(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	uint8_t *dst;

	ring_buf_init(&rb, sizeof(storage), storage);

	TEST_ASSERT_EQUAL(4, ring_buf_put_claim(&rb, &dst, 4));
	/* Committing more than claimed is rejected. */
	TEST_ASSERT_EQUAL(-EINVAL, ring_buf_put_finish(&rb, 5));
}

static void test_ring_buf_get_claim_finish(void)
{
	uint8_t storage[16];
	struct ring_buf rb;
	const uint8_t in[] = {1, 2, 3, 4, 5, 6};
	uint8_t *src;

	ring_buf_init(&rb, sizeof(storage), storage);
	ring_buf_put(&rb, in, sizeof(in));

	uint32_t got = ring_buf_get_claim(&rb, &src, 6);
	TEST_ASSERT_EQUAL(6, got);
	TEST_ASSERT_EQUAL_UINT8_ARRAY(in, src, 6);

	/* Free only 2; the other 4 remain available. */
	TEST_ASSERT_EQUAL(0, ring_buf_get_finish(&rb, 2));
	TEST_ASSERT_EQUAL(4, ring_buf_size_get(&rb));
}

/* ----------------------------------------------------------------
 * Wraparound: a claim never crosses the physical end of the buffer,
 * so a put/get spanning the wrap is split across two claims. This is
 * the load-bearing index arithmetic (base-offset scheme).
 * ---------------------------------------------------------------- */

static void test_ring_buf_put_claim_wraps(void)
{
	uint8_t storage[8];
	struct ring_buf rb;
	uint8_t *dst;
	const uint8_t a[] = {1, 2, 3, 4, 5, 6};

	ring_buf_init(&rb, sizeof(storage), storage);

	/* Advance head/tail to offset 6, then drain so space==capacity
	 * but the write offset sits near the end. */
	ring_buf_put(&rb, a, sizeof(a));
	TEST_ASSERT_EQUAL(6, ring_buf_get(&rb, NULL, sizeof(a)));
	TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));

	/* Now a claim for 8 returns only the 2 contiguous bytes to the
	 * physical end; the rest needs a second claim after it. */
	uint32_t first = ring_buf_put_claim(&rb, &dst, 8);
	TEST_ASSERT_EQUAL(2, first);
	dst[0] = 0xAA;
	dst[1] = 0xBB;

	uint32_t second = ring_buf_put_claim(&rb, &dst, 8);
	TEST_ASSERT_EQUAL(6, second); /* wrapped to the start */
	for (uint32_t i = 0; i < second; i++) {
		dst[i] = (uint8_t)(0xC0 + i);
	}
	TEST_ASSERT_EQUAL(0, ring_buf_put_finish(&rb, first + second));
	TEST_ASSERT_EQUAL(8, ring_buf_size_get(&rb));
}

/* ring_buf_put/get themselves loop over the wrap, so a single call that
 * spans the boundary must still round-trip the bytes in order. */
static void test_ring_buf_put_get_across_wrap(void)
{
	uint8_t storage[8];
	struct ring_buf rb;
	uint8_t out[8] = {0};
	const uint8_t a[] = {10, 11, 12, 13, 14};
	const uint8_t b[] = {20, 21, 22, 23, 24, 25};

	ring_buf_init(&rb, sizeof(storage), storage);

	/* Push the offset forward by 5, drain it. */
	ring_buf_put(&rb, a, sizeof(a));
	ring_buf_get(&rb, out, sizeof(a));

	/* This 6-byte write starts at offset 5 and wraps the 8-byte buffer. */
	TEST_ASSERT_EQUAL(6, ring_buf_put(&rb, b, sizeof(b)));
	/* Accounting must be correct across the wrap: space_get uses the
	 * other unsigned-subtraction path (put.head - get.tail). */
	TEST_ASSERT_EQUAL(6, ring_buf_size_get(&rb));
	TEST_ASSERT_EQUAL(2, ring_buf_space_get(&rb));

	memset(out, 0, sizeof(out));
	TEST_ASSERT_EQUAL(6, ring_buf_get(&rb, out, sizeof(b)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out, sizeof(b));
	/* Fully drained after the wrap. */
	TEST_ASSERT_EQUAL(0, ring_buf_size_get(&rb));
	TEST_ASSERT_EQUAL(8, ring_buf_space_get(&rb));
}

/* Many wrap cycles exercise base advancement and the unsigned index
 * subtractions across the full ring_buf_idx_t range without drift. */
static void test_ring_buf_many_wrap_cycles(void)
{
	uint8_t storage[7]; /* non-power-of-two on purpose */
	struct ring_buf rb;
	uint8_t next_write = 0;
	uint8_t next_read = 0;

	ring_buf_init(&rb, sizeof(storage), storage);

	/* Far more bytes than fit in ring_buf_idx_t at a time, streamed in
	 * 5-byte chunks through a 7-byte buffer so the offset wraps often. */
	for (int cycle = 0; cycle < 20000; cycle++) {
		uint8_t chunk[5];

		for (int i = 0; i < 5; i++) {
			chunk[i] = next_write++;
		}
		TEST_ASSERT_EQUAL(5, ring_buf_put(&rb, chunk, sizeof(chunk)));

		uint8_t out[5] = {0};
		TEST_ASSERT_EQUAL(5, ring_buf_get(&rb, out, sizeof(out)));
		for (int i = 0; i < 5; i++) {
			TEST_ASSERT_EQUAL_UINT8(next_read++, out[i]);
		}
	}
	TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
}

/* Fill-to-full / drain-to-empty repeatedly, enough cycles to carry the
 * uint16_t indices across their 65536 wrap, asserting the full and
 * empty discriminations AT high `allocated` each cycle. The
 * many-wrap-cycles test above drains within a 7-byte span so allocated
 * never approaches capacity; this one holds the buffer completely full
 * across the index wrap, exercising space_get == 0 / put returning 0
 * (full) vs is_empty (empty) right where the modular subtraction must
 * stay unambiguous. */
static void test_ring_buf_full_empty_across_wrap(void)
{
	static uint8_t storage[4096];
	struct ring_buf rb;
	uint8_t *dst;

	ring_buf_init(&rb, sizeof(storage), storage);

	/* 4096 bytes/cycle; 40 cycles = 163840 bytes, crossing the 65536
	 * index wrap ~2.5 times. */
	for (int cycle = 0; cycle < 40; cycle++) {
		uint8_t seed = (uint8_t)cycle;

		/* Fill to exactly full via the claim path. */
		uint32_t total = 0;
		while (total < sizeof(storage)) {
			uint32_t n = ring_buf_put_claim(&rb, &dst, sizeof(storage) - total);
			TEST_ASSERT_NOT_EQUAL(0, n);
			for (uint32_t i = 0; i < n; i++) {
				dst[i] = (uint8_t)(seed + total + i);
			}
			total += n;
			TEST_ASSERT_EQUAL(0, ring_buf_put_finish(&rb, n));
		}

		/* Full: no space, both put paths refuse. */
		TEST_ASSERT_EQUAL(sizeof(storage), ring_buf_size_get(&rb));
		TEST_ASSERT_EQUAL(0, ring_buf_space_get(&rb));
		TEST_ASSERT_FALSE(ring_buf_is_empty(&rb));
		TEST_ASSERT_EQUAL(0, ring_buf_put_claim(&rb, &dst, 1));

		/* Drain fully, verifying the bytes survived the wrap. */
		uint32_t got = 0;
		while (got < sizeof(storage)) {
			uint8_t *src;
			uint32_t n = ring_buf_get_claim(&rb, &src, sizeof(storage) - got);
			TEST_ASSERT_NOT_EQUAL(0, n);
			for (uint32_t i = 0; i < n; i++) {
				TEST_ASSERT_EQUAL_UINT8((uint8_t)(seed + got + i), src[i]);
			}
			got += n;
			TEST_ASSERT_EQUAL(0, ring_buf_get_finish(&rb, n));
		}

		/* Empty: discrimination from the full state above. */
		TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
		TEST_ASSERT_EQUAL(0, ring_buf_size_get(&rb));
		TEST_ASSERT_EQUAL(sizeof(storage), ring_buf_space_get(&rb));
	}
}

/* ----------------------------------------------------------------
 * RING_BUF_DECLARE static instance
 * ---------------------------------------------------------------- */

RING_BUF_DECLARE(declared_rb, 12);

static void test_ring_buf_declare_usable(void)
{
	const uint8_t in[] = {0x55, 0x66, 0x77};
	uint8_t out[3] = {0};

	TEST_ASSERT_EQUAL(12, ring_buf_capacity_get(&declared_rb));
	TEST_ASSERT_TRUE(ring_buf_is_empty(&declared_rb));

	TEST_ASSERT_EQUAL(3, ring_buf_put(&declared_rb, in, sizeof(in)));
	TEST_ASSERT_EQUAL(3, ring_buf_get(&declared_rb, out, sizeof(out)));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, sizeof(in));
}

void test_ring_buf_group(void)
{
	RUN_TEST(test_ring_buf_init_accounting);
	RUN_TEST(test_ring_buf_put_get_roundtrip);
	RUN_TEST(test_ring_buf_put_saturates_at_capacity);
	RUN_TEST(test_ring_buf_get_more_than_available);
	RUN_TEST(test_ring_buf_get_null_discards);
	RUN_TEST(test_ring_buf_peek_does_not_consume);
	RUN_TEST(test_ring_buf_reset_empties);
	RUN_TEST(test_ring_buf_put_claim_finish);
	RUN_TEST(test_ring_buf_put_finish_surplus_returns_space);
	RUN_TEST(test_ring_buf_put_finish_over_claim_einval);
	RUN_TEST(test_ring_buf_get_claim_finish);
	RUN_TEST(test_ring_buf_put_claim_wraps);
	RUN_TEST(test_ring_buf_put_get_across_wrap);
	RUN_TEST(test_ring_buf_many_wrap_cycles);
	RUN_TEST(test_ring_buf_full_empty_across_wrap);
	RUN_TEST(test_ring_buf_declare_usable);
}
