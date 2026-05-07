/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include "zephyr/kernel.h"

#include <errno.h>
#include "esp_attr.h"
#include "freertos/timers.h"
#include "sdkconfig.h"

/* Trampoline: adapts Zephyr's 3-arg entry to FreeRTOS's 1-arg entry.
 * If _start_suspended is set, suspends self before calling entry
 * (used for K_FOREVER and finite-delay deferred start).
 * After the entry function returns, the thread suspends itself
 * (safe for static tasks -- vTaskDelete is NOT safe with static TCBs). */
static void k_thread_entry_wrapper(void *arg)
{
	struct k_thread *thread = (struct k_thread *)arg;

	if (thread->_start_suspended) {
		vTaskSuspend(NULL); /* wait for k_thread_resume or timer callback */
	}

	thread->entry(thread->p1, thread->p2, thread->p3);
	vTaskSuspend(NULL);
}

#ifdef CONFIG_K_TIMER_DISPATCH_ISR
static void k_thread_delay_resume_pended(void *param, uint32_t unused)
{
	(void)unused;
	struct k_thread *thread = (struct k_thread *)param;
	if (thread && thread->handle) {
		vTaskResume(thread->handle);
	}
}

static void IRAM_ATTR k_thread_delay_expiry(struct k_timer *timer)
{
	struct k_thread *thread = (struct k_thread *)k_timer_user_data_get(timer);
	if (thread) {
		BaseType_t woken = pdFALSE;
		xTimerPendFunctionCallFromISR(k_thread_delay_resume_pended, thread, 0, &woken);
		portYIELD_FROM_ISR(woken);
	}
}
#else
static void k_thread_delay_expiry(struct k_timer *timer)
{
	struct k_thread *thread = (struct k_thread *)k_timer_user_data_get(timer);
	if (thread && thread->handle) {
		vTaskResume(thread->handle);
	}
}
#endif

k_tid_t k_thread_create(struct k_thread *thread, StackType_t *stack, size_t stack_size,
			k_thread_entry_t entry, void *p1, void *p2, void *p3, int prio,
			uint32_t options, k_timeout_t delay)
{
	(void)options;

	thread->stack = stack;
	thread->stack_size = stack_size;
	thread->entry = entry;
	thread->p1 = p1;
	thread->p2 = p2;
	thread->p3 = p3;
	thread->_delay_timer.handle = NULL; /* explicitly clear for abort safety */

	/* Set flag BEFORE creating task so the wrapper sees it immediately */
	thread->_start_suspended = !k_timeout_is_no_wait(delay);

	/* Pin to core 0. ESP-IDF's vPortCleanUpCoprocArea passes
	 * tskNO_AFFINITY (0x7FFFFFFF) to _xt_coproc_release, which
	 * overflows the owner-table index and silently skips the real
	 * entries — leaking stale FPU ownership. */
	thread->handle = xTaskCreateStaticPinnedToCore(
		k_thread_entry_wrapper, thread->name ? thread->name : "k_thread",
		stack_size / sizeof(StackType_t), thread, prio, stack, &thread->tcb, 0);

	/* xTaskCreateStatic only fails on programmer error (misaligned
	 * stack, NULL stack pointer, etc.). Match upstream Zephyr's
	 * "always returns a valid tid" contract by asserting here rather
	 * than returning NULL -- callers ported from Zephyr won't check. */
	__ASSERT(thread->handle != NULL, "k_thread_create: xTaskCreateStatic failed");

	if (!k_timeout_is_forever(delay) && !k_timeout_is_no_wait(delay)) {
		k_timer_init(&thread->_delay_timer, k_thread_delay_expiry, NULL);
		k_timer_user_data_set(&thread->_delay_timer, thread);
		k_timer_start(&thread->_delay_timer, delay, K_NO_WAIT);
	}
	/* K_FOREVER: thread self-suspended, user calls k_thread_resume */
	/* K_NO_WAIT: _start_suspended=false, thread runs immediately */

	return thread->handle;
}

void k_thread_name_set(struct k_thread *thread, const char *name)
{
	/* Stored for diagnostics only. FreeRTOS does not support renaming a
	 * task after creation, and the FreeRTOS task name (used by vTaskList
	 * and similar tooling) is fixed at xTaskCreate time. To get a
	 * meaningful name in tooling, set thread->name BEFORE calling
	 * k_thread_create(). */
	thread->name = name;
}

void k_thread_abort(struct k_thread *thread)
{
	if (thread->handle != NULL) {
		/* Stop delay timer if it was used for deferred start */
		if (thread->_delay_timer.handle != NULL) {
			k_timer_stop(&thread->_delay_timer);
		}
		vTaskDelete(thread->handle);
		thread->handle = NULL;
	}
}

void k_thread_suspend(struct k_thread *thread)
{
	if (thread->handle != NULL) {
		vTaskSuspend(thread->handle);
	}
}

void k_thread_resume(struct k_thread *thread)
{
	if (thread->handle != NULL) {
		vTaskResume(thread->handle);
	}
}

int k_thread_join(struct k_thread *thread, k_timeout_t timeout)
{
	/* FreeRTOS doesn't have native join. Poll eTaskGetState. */
	TickType_t deadline = xTaskGetTickCount() + k_timeout_to_ticks(timeout);
	bool forever = k_timeout_is_forever(timeout);

	while (thread->handle != NULL) {
		eTaskState state = eTaskGetState(thread->handle);
		if (state == eDeleted || state == eInvalid || state == eSuspended) {
			thread->handle = NULL;
			return 0;
		}
		if (!forever && xTaskGetTickCount() >= deadline) {
			return -EAGAIN;
		}
		k_msleep(10);
	}
	return 0;
}

int k_thread_stack_space_get(struct k_thread *thread, size_t *unused)
{
	if (thread->handle == NULL) {
		return -EINVAL;
	}
	UBaseType_t hwm = uxTaskGetStackHighWaterMark(thread->handle);
	*unused = (size_t)(hwm * sizeof(StackType_t));
	return 0;
}
