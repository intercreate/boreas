/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <string.h>

#include "unity.h"
#include "zephyr/sys/byteorder.h"

static void test_byteorder_le16_roundtrip(void)
{
	uint8_t buf[2];
	sys_put_le16(0xABCD, buf);
	TEST_ASSERT_EQUAL_HEX8(0xCD, buf[0]);
	TEST_ASSERT_EQUAL_HEX8(0xAB, buf[1]);
	TEST_ASSERT_EQUAL_HEX16(0xABCD, sys_get_le16(buf));
}

static void test_byteorder_be16_roundtrip(void)
{
	uint8_t buf[2];
	sys_put_be16(0xABCD, buf);
	TEST_ASSERT_EQUAL_HEX8(0xAB, buf[0]);
	TEST_ASSERT_EQUAL_HEX8(0xCD, buf[1]);
	TEST_ASSERT_EQUAL_HEX16(0xABCD, sys_get_be16(buf));
}

static void test_byteorder_le32_roundtrip(void)
{
	uint8_t buf[4];
	sys_put_le32(0xDEADBEEFu, buf);
	TEST_ASSERT_EQUAL_HEX8(0xEF, buf[0]);
	TEST_ASSERT_EQUAL_HEX8(0xBE, buf[1]);
	TEST_ASSERT_EQUAL_HEX8(0xAD, buf[2]);
	TEST_ASSERT_EQUAL_HEX8(0xDE, buf[3]);
	TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, sys_get_le32(buf));
}

static void test_byteorder_be32_roundtrip(void)
{
	uint8_t buf[4];
	sys_put_be32(0xDEADBEEFu, buf);
	TEST_ASSERT_EQUAL_HEX8(0xDE, buf[0]);
	TEST_ASSERT_EQUAL_HEX8(0xAD, buf[1]);
	TEST_ASSERT_EQUAL_HEX8(0xBE, buf[2]);
	TEST_ASSERT_EQUAL_HEX8(0xEF, buf[3]);
	TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, sys_get_be32(buf));
}

static void test_byteorder_endian_distinct(void)
{
	/* Same value put LE and BE should produce reversed byte orders. */
	uint8_t le[4], be[4];
	sys_put_le32(0x11223344u, le);
	sys_put_be32(0x11223344u, be);
	TEST_ASSERT_EQUAL_HEX8(le[0], be[3]);
	TEST_ASSERT_EQUAL_HEX8(le[1], be[2]);
	TEST_ASSERT_EQUAL_HEX8(le[2], be[1]);
	TEST_ASSERT_EQUAL_HEX8(le[3], be[0]);
}

static void test_byteorder_boundary_values(void)
{
	uint8_t buf[4];

	sys_put_le16(0, buf);
	TEST_ASSERT_EQUAL_HEX16(0, sys_get_le16(buf));
	sys_put_be16(0, buf);
	TEST_ASSERT_EQUAL_HEX16(0, sys_get_be16(buf));
	sys_put_le32(0, buf);
	TEST_ASSERT_EQUAL_HEX32(0, sys_get_le32(buf));
	sys_put_be32(0, buf);
	TEST_ASSERT_EQUAL_HEX32(0, sys_get_be32(buf));

	sys_put_le16(0xFFFFu, buf);
	TEST_ASSERT_EQUAL_HEX16(0xFFFFu, sys_get_le16(buf));
	sys_put_be16(0xFFFFu, buf);
	TEST_ASSERT_EQUAL_HEX16(0xFFFFu, sys_get_be16(buf));
	sys_put_le32(0xFFFFFFFFu, buf);
	TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, sys_get_le32(buf));
	sys_put_be32(0xFFFFFFFFu, buf);
	TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, sys_get_be32(buf));
}

static void test_byteorder_no_overrun(void)
{
	/* Verify writes stay in bounds (4-byte writes don't touch byte 4). */
	uint8_t buf[5] = {0, 0, 0, 0, 0xA5};
	sys_put_le32(0xFFFFFFFFu, buf);
	TEST_ASSERT_EQUAL_HEX8(0xA5, buf[4]);
	sys_put_be32(0xFFFFFFFFu, buf);
	TEST_ASSERT_EQUAL_HEX8(0xA5, buf[4]);
}

void test_byteorder_group(void)
{
	RUN_TEST(test_byteorder_le16_roundtrip);
	RUN_TEST(test_byteorder_be16_roundtrip);
	RUN_TEST(test_byteorder_le32_roundtrip);
	RUN_TEST(test_byteorder_be32_roundtrip);
	RUN_TEST(test_byteorder_endian_distinct);
	RUN_TEST(test_byteorder_boundary_values);
	RUN_TEST(test_byteorder_no_overrun);
}
