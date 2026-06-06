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
 * When the entry function returns the thread is terminated -- matching
 * upstream Zephyr -- via _completed plus a target-specific mechanism;
 * the _completed store is ordered before it so that observers of the
 * flag also see all of the entry function's side effects.
 *
 * linux: self-delete. Takes the POSIX port's xDying -> pthread_exit
 * path, so idle's later pthread_join succeeds without relying on
 * cancellation delivery (which appears undeliverable to threads
 * parked with all signals blocked on macOS hosts).
 *
 * silicon: park in vTaskSuspend; k_thread_join/k_thread_abort reap us
 * with a vTaskDelete from THEIR context, which FreeRTOS reclaims
 * synchronously (a non-running task is never idle-deferred). Self-
 * delete here would defer prvDeleteTCB -- which dereferences the TCB
 * for newlib reent reclaim -- to the idle task, racing the caller's
 * storage reuse after join/abort returns (observed as a wild free()
 * from idle on esp32s3). */
static void k_thread_entry_wrapper(void *arg)
{
	struct k_thread *thread = (struct k_thread *)arg;

	if (thread->_start_suspended) {
		vTaskSuspend(NULL); /* wait for k_thread_resume or timer callback */
	}

	thread->entry(thread->p1, thread->p2, thread->p3);

	__atomic_store_n(&thread->_completed, true, __ATOMIC_RELEASE);
#if CONFIG_IDF_TARGET_LINUX
	vTaskDelete(NULL); /* cannot return (pthread_exit) */
#else
	/* Parked until join/abort reaps us. Looped: a stray resume of a
	 * completed thread would otherwise return from vTaskSuspend and
	 * fall off the task function -- a fatal abort() in ESP-IDF's
	 * vPortTaskWrapper. Re-park instead. */
	for (;;) {
		vTaskSuspend(NULL);
	}
#endif
}

/* Best-effort window for the POSIX port's idle task to reap a deleted
 * task. portCLEAN_UP_TCB -> vPortCancelThread runs in idle and
 * dereferences the TCB AND the port's Thread_t parked at the top of
 * the task's stack buffer -- both live in caller-owned storage, which
 * the caller may legally reuse or free as soon as join/abort returns.
 * Block two ticks so idle runs the cleanup first. BEST-EFFORT: nothing
 * exported by the port signals "reap done", so a system whose ready
 * tasks starve idle could still hold references after this returns;
 * the linux backend exists for host test runners, where the blocked
 * caller reliably yields the CPU to idle (see @note on k_thread_join).
 * No-op on silicon, where reclamation is synchronous. */
static void z_thread_linux_reap_window(void)
{
#if CONFIG_IDF_TARGET_LINUX
	k_msleep(2 * portTICK_PERIOD_MS);
#endif
}

/* Reclaim a thread whose entry function has returned (_completed set).
 * silicon: the wrapper is parked in (or headed for) vTaskSuspend; wait
 * for it to arrive, then delete it from this context so reclamation is
 * synchronous and DETERMINISTIC -- when this returns, the kernel holds
 * no references into the caller's storage.
 * linux: the wrapper self-deleted; give idle its reap window. */
static void z_thread_reap_completed(struct k_thread *thread)
{
#if CONFIG_IDF_TARGET_LINUX
	(void)thread;
	z_thread_linux_reap_window();
#else
	eTaskState state;

	while ((state = eTaskGetState(thread->handle)) != eSuspended) {
		if (state == eDeleted || state == eInvalid) {
			return; /* already reaped (e.g. a racing join/abort) */
		}
		vTaskDelay(1); /* between the _completed store and the park */
	}
	vTaskDelete(thread->handle);
#endif
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

static void K_ISR_SAFE k_thread_delay_expiry(struct k_timer *timer)
{
	struct k_thread *thread = (struct k_thread *)k_timer_user_data_get(timer);
	if (thread) {
		BaseType_t woken = pdFALSE;
		BaseType_t ret = xTimerPendFunctionCallFromISR(k_thread_delay_resume_pended, thread,
							       0, &woken);
		if (ret != pdPASS) {
			k_panic();
		}
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
	thread->_completed = false;         /* struct may be reused after join/abort */

	/* Set flag BEFORE creating task so the wrapper sees it immediately */
	thread->_start_suspended = !k_timeout_is_no_wait(delay);

	/* Pin to core 0. ESP-IDF's vPortCleanUpCoprocArea passes
	 * tskNO_AFFINITY (0x7FFFFFFF) to _xt_coproc_release, which
	 * overflows the owner-table index and silently skips the real
	 * entries — leaking stale FPU ownership. */
	thread->handle = xTaskCreateStaticPinnedToCore(
		k_thread_entry_wrapper, thread->name ? thread->name : "k_thread",
		stack_size / sizeof(StackType_t), thread, prio, stack, &thread->tcb, 0);

	/* xTaskCreateStaticPinnedToCore only fails on programmer error
	 * (misaligned stack, NULL stack pointer, etc.). Match upstream
	 * Zephyr's "always returns a valid tid" contract by asserting
	 * here rather than returning NULL -- callers ported from Zephyr
	 * won't check. */
	__ASSERT(thread->handle != NULL, "k_thread_create: xTaskCreateStaticPinnedToCore failed");

	/* Finite delay: thread self-suspended, set up timer to resume */
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
		/* If the entry function already returned, take the reap path:
		 * on linux the task already self-deleted (a second delete
		 * would corrupt the termination list); on silicon it is
		 * parked awaiting our synchronous delete.
		 *
		 * Known window (no kernel-lock access from a compat layer):
		 * an abort racing the entry function's return can observe
		 * _completed == false here and then delete a task that
		 * finishes in between. On silicon that is a benign
		 * abort-while-running; on linux a tick preemption landing
		 * exactly between this load and the vTaskDelete below could
		 * double-delete. Do not abort a thread concurrently with
		 * its own exit (see @note on k_thread_join). */
		if (__atomic_load_n(&thread->_completed, __ATOMIC_ACQUIRE)) {
			z_thread_reap_completed(thread);
			thread->handle = NULL;
			return;
		}
		vTaskDelete(thread->handle);
		thread->handle = NULL;
		z_thread_linux_reap_window();
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
	/* FreeRTOS doesn't have native join. Poll for completion. The
	 * _completed flag is the primary signal (set by the entry wrapper
	 * before self-delete); eDeleted/eInvalid catch externally aborted
	 * tasks. eSuspended is deliberately NOT treated as completion --
	 * a user-suspended or deferred-start thread has not terminated
	 * (upstream Zephyr blocks in that case too). */
	TickType_t deadline = xTaskGetTickCount() + k_timeout_to_ticks(timeout);
	bool forever = k_timeout_is_forever(timeout);

	if (thread->handle != NULL && thread->handle == xTaskGetCurrentTaskHandle()) {
		return -EDEADLK; /* joining self -- matches upstream Zephyr */
	}

	while (thread->handle != NULL) {
		if (__atomic_load_n(&thread->_completed, __ATOMIC_ACQUIRE)) {
			z_thread_reap_completed(thread);
			thread->handle = NULL;
			return 0;
		}

		eTaskState state = eTaskGetState(thread->handle);
		if (state == eDeleted || state == eInvalid) {
			/* Externally deleted without completing (e.g. a racing
			 * abort from another context). */
			thread->handle = NULL;
			z_thread_linux_reap_window();
			return 0;
		}
		if (k_timeout_is_no_wait(timeout)) {
			return -EBUSY; /* still running -- matches upstream Zephyr */
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
