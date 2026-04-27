/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Zephyr-compatible byte-order helpers for little- and big-endian
 * 16- and 32-bit access over byte buffers.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint16_t sys_get_le16(const uint8_t src[2])
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static inline void sys_put_le16(uint16_t val, uint8_t dst[2])
{
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
}

static inline uint16_t sys_get_be16(const uint8_t src[2])
{
    return ((uint16_t)src[0] << 8) | (uint16_t)src[1];
}

static inline void sys_put_be16(uint16_t val, uint8_t dst[2])
{
    dst[0] = (uint8_t)((val >> 8) & 0xFF);
    dst[1] = (uint8_t)(val & 0xFF);
}

static inline uint32_t sys_get_le32(const uint8_t src[4])
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static inline void sys_put_le32(uint32_t val, uint8_t dst[4])
{
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    dst[2] = (uint8_t)((val >> 16) & 0xFF);
    dst[3] = (uint8_t)((val >> 24) & 0xFF);
}

static inline uint32_t sys_get_be32(const uint8_t src[4])
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static inline void sys_put_be32(uint32_t val, uint8_t dst[4])
{
    dst[0] = (uint8_t)((val >> 24) & 0xFF);
    dst[1] = (uint8_t)((val >> 16) & 0xFF);
    dst[2] = (uint8_t)((val >> 8) & 0xFF);
    dst[3] = (uint8_t)(val & 0xFF);
}

#ifdef __cplusplus
}
#endif
