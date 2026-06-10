/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Fixed-size block allocator (k_mem_slab). Independent reimplementation
 * of upstream Zephyr's API over the Boreas substrate: the free-block
 * count and blocking ride the owned, notification-backed k_sem (count =
 * free blocks), and a portMUX guards the intrusive free list and the
 * usage counters. Reusing k_sem inherits its hardened wake protocol --
 * a give targets the highest-priority waiter without bumping the count,
 * so a freed block is reserved for the woken allocator (a racing
 * K_NO_WAIT alloc sees no credit and returns -ENOMEM), matching
 * upstream's direct hand-off semantics.
 *
 * Free-list scheme (upstream): blocks are threaded through an intrusive
 * singly-linked list whose next-pointer lives in each free block's first
 * word; alloc returns the raw (uninitialized) block. block_size must be
 * >= sizeof(void *) and word-aligned.
 */

#include "zephyr/kernel.h"

#include <errno.h>
#include <stdint.h>

#include "sdkconfig.h"

#include "esp_attr.h"

#include "zkernel_internal.h"

/* Thread the free list back-to-front (ascending block order), validating
 * the upstream alignment/overflow constraints. Caller serializes. */
static int K_ISR_SAFE z_mem_slab_create_free_list(struct k_mem_slab *slab)
{
	if (slab->block_size == 0U || slab->num_blocks == 0U) {
		return -EINVAL;
	}
	/* block_size and buffer base must both be word-aligned (the first
	 * word of each block holds the free-list next pointer). */
	if (((slab->block_size | (uintptr_t)slab->buffer) & (sizeof(void *) - 1U)) != 0U) {
		return -EINVAL;
	}

	size_t total;

	if (__builtin_mul_overflow(slab->block_size, (size_t)slab->num_blocks, &total)) {
		return -EINVAL;
	}
	uintptr_t end;

	if (__builtin_add_overflow((uintptr_t)slab->buffer, (uintptr_t)total, &end)) {
		return -EINVAL;
	}

	slab->free_list = NULL;
	char *p = (char *)slab->buffer + (total - slab->block_size);

	for (uint32_t i = 0; i < slab->num_blocks; i++) {
		*(char **)p = slab->free_list;
		slab->free_list = p;
		p -= slab->block_size;
	}
	return 0;
}

int k_mem_slab_init(struct k_mem_slab *slab, void *buffer, size_t block_size, uint32_t num_blocks)
{
	slab->buffer = buffer;
	slab->block_size = block_size;
	slab->num_blocks = num_blocks;
	slab->num_used = 0;
	slab->max_used = 0;
	slab->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

	int ret = z_mem_slab_create_free_list(slab);

	if (ret != 0) {
		return ret;
	}

	/* num_blocks >= 1 is guaranteed by create_free_list above. */
	ret = k_sem_init(&slab->avail, num_blocks, num_blocks);
	if (ret != 0) {
		return ret;
	}

	__atomic_store_n(&slab->threaded, true, __ATOMIC_RELEASE);
	return 0;
}

/* Thread the free list of a K_MEM_SLAB_DEFINE'd slab on first use, once,
 * under the slab's (statically-initialized) lock. The embedded sem is
 * already compile-time-initialized, so nothing here calls k_sem_init --
 * this is pure pointer work and therefore IRAM/ISR-safe. A slab created
 * via k_mem_slab_init() has threaded == true already and skips this. */
static int K_ISR_SAFE z_mem_slab_ensure_threaded(struct k_mem_slab *slab)
{
	if (__atomic_load_n(&slab->threaded, __ATOMIC_ACQUIRE)) {
		return 0;
	}

	int ret = 0;

	z_kernel_lock(&slab->lock);
	if (!__atomic_load_n(&slab->threaded, __ATOMIC_ACQUIRE)) {
		ret = z_mem_slab_create_free_list(slab);
		if (ret == 0) {
			__atomic_store_n(&slab->threaded, true, __ATOMIC_RELEASE);
		}
	}
	z_kernel_unlock(&slab->lock);
	return ret;
}

int K_ISR_SAFE k_mem_slab_alloc(struct k_mem_slab *slab, void **mem, k_timeout_t timeout)
{
	int ret = z_mem_slab_ensure_threaded(slab);

	if (ret != 0) {
		*mem = NULL;
		return ret; /* -EINVAL: bad DEFINE parameters */
	}

	/* The sem count IS the free-block count; taking a credit reserves
	 * exactly one block, which the locked pop below always finds. */
	ret = k_sem_take(&slab->avail, timeout);
	if (ret != 0) {
		*mem = NULL;
		/* k_sem_take: -EBUSY (K_NO_WAIT, none free) -> -ENOMEM;
		 * -EAGAIN (timeout) propagates unchanged. */
		return (ret == -EBUSY) ? -ENOMEM : ret;
	}

	z_kernel_lock(&slab->lock);
	char *block = slab->free_list;

	slab->free_list = *(char **)block;
	slab->num_used++;
	if (slab->num_used > slab->max_used) {
		slab->max_used = slab->num_used;
	}
	z_kernel_unlock(&slab->lock);

	*mem = block;
	return 0;
}

void K_ISR_SAFE k_mem_slab_free(struct k_mem_slab *slab, void *mem)
{
	/* Defensive: a correctly-used slab is always threaded by the time a
	 * caller holds a block to free, but a DEFINE'd slab freed before any
	 * alloc (caller error) would otherwise push onto an unthreaded list. */
	(void)z_mem_slab_ensure_threaded(slab);

	z_kernel_lock(&slab->lock);
	*(char **)mem = slab->free_list;
	slab->free_list = (char *)mem;
	/* num_used is re-incremented by the woken allocator in the hand-off
	 * case, so it nets unchanged; the brief dip is a relaxed-stats
	 * window only (the sem count, not num_used, gates allocation). */
	slab->num_used--;
	z_kernel_unlock(&slab->lock);

	/* Returns the credit. With a waiter pending, k_sem_give targets it
	 * (count stays 0), reserving the just-freed block for that waiter. */
	k_sem_give(&slab->avail);
}
