/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 */

#include <errno.h>

#include "unity.h"
#include "zephyr/kernel.h"

static volatile int work_executed;

static void test_work_handler(struct k_work *work)
{
	(void)work;
	work_executed++;
}

static void test_work_submit(void)
{

	struct k_work work;
	k_work_init(&work, test_work_handler);
	work_executed = 0;

	TEST_ASSERT_EQUAL(1, k_work_submit(&work)); /* 1 = was idle, queued */
	k_msleep(100);
	TEST_ASSERT_EQUAL(1, work_executed);
}

static void test_work_submit_idempotent(void)
{

	struct k_work work;
	k_work_init(&work, test_work_handler);
	work_executed = 0;

	TEST_ASSERT_EQUAL(1, k_work_submit(&work)); /* 1 = was idle, queued */
	k_work_submit(&work);                       /* 0 (still queued) or 1/2 if it raced ahead */
	k_msleep(100);
	TEST_ASSERT_GREATER_OR_EQUAL(1, work_executed);
}

static void test_work_cancel(void)
{
	struct k_work work;
	k_work_init(&work, test_work_handler);

	/* Cancel on non-queued work: item is idle afterwards -> 0 */
	TEST_ASSERT_EQUAL(0, k_work_cancel(&work));
}

static void test_work_is_pending(void)
{
	struct k_work work;
	k_work_init(&work, test_work_handler);

	TEST_ASSERT_FALSE(k_work_is_pending(&work));
}

static void test_work_delayable(void)
{

	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	k_work_schedule(&dwork, K_MSEC(50));
	TEST_ASSERT_TRUE(k_work_delayable_is_pending(&dwork));

	k_msleep(10);
	TEST_ASSERT_EQUAL(0, work_executed);

	k_msleep(200);
	TEST_ASSERT_EQUAL(1, work_executed);
}

static void test_work_cancel_delayable(void)
{

	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	k_work_schedule(&dwork, K_MSEC(200));
	k_msleep(10);
	k_work_cancel_delayable(&dwork);

	k_msleep(300);
	TEST_ASSERT_EQUAL(0, work_executed);
}

static void test_work_reschedule(void)
{

	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	/* Schedule for 500ms, then reschedule to 50ms */
	k_work_schedule(&dwork, K_MSEC(500));
	k_msleep(10);
	k_work_reschedule(&dwork, K_MSEC(50));

	/* Should fire at ~60ms total, not 500ms */
	k_msleep(200);
	TEST_ASSERT_EQUAL(1, work_executed);
}

static void test_work_delayable_remaining(void)
{

	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	k_work_schedule(&dwork, K_MSEC(500));
	k_msleep(50);

	int64_t remaining = k_work_delayable_remaining_get(&dwork);
	TEST_ASSERT_GREATER_THAN(100, remaining);
	TEST_ASSERT_LESS_THAN(500, remaining);

	k_work_cancel_delayable(&dwork);
}

static volatile int custom_wq_executed;

static void custom_wq_handler(struct k_work *work)
{
	(void)work;
	custom_wq_executed++;
}

static void test_work_submit_to_queue(void)
{

	struct k_work work;
	k_work_init(&work, custom_wq_handler);
	custom_wq_executed = 0;

	/* submit_to_queue with explicit queue (vs submit which uses sys queue) */
	TEST_ASSERT_EQUAL(1, k_work_submit_to_queue(&k_sys_work_q, &work));
	k_msleep(100);
	TEST_ASSERT_EQUAL(1, custom_wq_executed);
}

static volatile int slow_work_done;

static void slow_work_handler(struct k_work *work)
{
	(void)work;
	k_msleep(200);
	slow_work_done = 1;
}

static void test_work_flush(void)
{
	struct k_work work;
	k_work_init(&work, slow_work_handler);
	slow_work_done = 0;

	k_work_submit(&work);

	/* Flush should block until handler finishes (~200ms) */
	struct k_work_sync sync;
	int64_t start = k_uptime_get();
	k_work_flush(&work, &sync);
	int64_t elapsed = k_uptime_get() - start;

	TEST_ASSERT_EQUAL(1, slow_work_done);
	TEST_ASSERT_GREATER_OR_EQUAL(100, elapsed);
}

static void test_work_flush_not_pending(void)
{
	struct k_work work;
	k_work_init(&work, test_work_handler);

	/* Flush on non-pending work should return immediately */
	struct k_work_sync sync;
	int ret = k_work_flush(&work, &sync);
	TEST_ASSERT_EQUAL(0, ret);
}

/* ----------------------------------------------------------------
 * dlist-rewrite regression tests (PR 3)
 * ---------------------------------------------------------------- */

/* Block the system workqueue worker on a sem so we can build up a
 * queue of pending items deterministically. Used by cancel-while-
 * pending and busy_get tests below. */
static struct k_sem block_sem;

static void blocking_handler(struct k_work *work)
{
	(void)work;
	k_sem_take(&block_sem, K_FOREVER);
}

static void test_work_cancel_while_pending(void)
{
	/* Regression test for the old wart: under the FreeRTOS xQueue
	 * implementation, cancelling a queued item only cleared the
	 * QUEUED flag -- the item still occupied a queue slot until the
	 * worker drained it. After the dlist rewrite, cancel removes the
	 * node synchronously. Verify by submitting items, cancelling
	 * them while still pending, and confirming we can immediately
	 * re-submit without backpressure. */

	struct k_work blocker;
	struct k_work victim;
	k_work_init(&blocker, blocking_handler);
	k_work_init(&victim, test_work_handler);
	k_sem_init(&block_sem, 0, 1);
	work_executed = 0;

	/* Submit blocker first -- worker takes it and parks on block_sem. */
	TEST_ASSERT_EQUAL(1, k_work_submit(&blocker));
	k_msleep(20);

	/* Now submit victim; worker is busy, so it stays QUEUED. */
	TEST_ASSERT_EQUAL(1, k_work_submit(&victim));
	TEST_ASSERT_TRUE(k_work_busy_get(&victim) & K_WORK_QUEUED);

	/* Cancel synchronously removes from the list; item idle -> 0 */
	TEST_ASSERT_EQUAL(0, k_work_cancel(&victim));
	TEST_ASSERT_FALSE(k_work_busy_get(&victim) & K_WORK_QUEUED);

	/* Re-submit must succeed immediately -- the slot is free. */
	TEST_ASSERT_EQUAL(1, k_work_submit(&victim));
	TEST_ASSERT_TRUE(k_work_busy_get(&victim) & K_WORK_QUEUED);

	/* Unblock worker and drain. */
	k_sem_give(&block_sem);
	k_work_queue_drain(&k_sys_work_q, false);
	TEST_ASSERT_EQUAL(1, work_executed);
}

static void test_work_busy_get_states(void)
{
	struct k_work blocker;
	struct k_work observed;
	k_work_init(&blocker, blocking_handler);
	k_work_init(&observed, test_work_handler);
	k_sem_init(&block_sem, 0, 1);
	work_executed = 0;

	/* Initially: no flags. */
	TEST_ASSERT_EQUAL(0, k_work_busy_get(&observed));

	/* Block worker, submit observed -> QUEUED only. */
	k_work_submit(&blocker);
	k_msleep(20);
	k_work_submit(&observed);
	TEST_ASSERT_EQUAL(K_WORK_QUEUED,
			  k_work_busy_get(&observed) & (K_WORK_QUEUED | K_WORK_RUNNING));

	/* Drain everything. */
	k_sem_give(&block_sem);
	k_work_queue_drain(&k_sys_work_q, false);
	TEST_ASSERT_EQUAL(0, k_work_busy_get(&observed));
}

static volatile int cancel_sync_done;

static void slow_then_flag(struct k_work *work)
{
	(void)work;
	k_msleep(150);
	cancel_sync_done = 1;
}

static void test_work_cancel_sync_running(void)
{
	struct k_work work;
	struct k_work_sync sync;
	k_work_init(&work, slow_then_flag);
	cancel_sync_done = 0;

	k_work_submit(&work);
	k_msleep(30); /* let it start running */
	TEST_ASSERT_TRUE(k_work_busy_get(&work) & K_WORK_RUNNING);

	/* cancel_sync must block until handler finishes; the work was
	 * pending (running) -> true */
	int64_t start = k_uptime_get();
	bool was_pending = k_work_cancel_sync(&work, &sync);
	int64_t elapsed = k_uptime_get() - start;

	TEST_ASSERT_TRUE(was_pending);
	TEST_ASSERT_EQUAL(1, cancel_sync_done);
	TEST_ASSERT_GREATER_OR_EQUAL(50, elapsed);
}

/* Submit from a k_timer callback. Note: Boreas k_timer callbacks run
 * on ESP_TIMER_TASK, not in true ISR context, so xPortInIsrContext()
 * returns false here. This still exercises the cross-thread submit
 * path and verifies the lock/sem dance is sound; a true ISR-context
 * test would require a hardware ISR. */
static struct k_work timer_submitted_work;
static volatile int timer_submitted_executed;
static struct k_timer submit_timer;

static void from_timer_handler(struct k_work *work)
{
	(void)work;
	timer_submitted_executed++;
}

static void timer_submits_work(struct k_timer *timer)
{
	(void)timer;
	k_work_submit(&timer_submitted_work);
}

static void test_work_submit_from_timer_callback(void)
{
	k_work_init(&timer_submitted_work, from_timer_handler);
	k_timer_init(&submit_timer, timer_submits_work, NULL);
	timer_submitted_executed = 0;

	k_timer_start(&submit_timer, K_MSEC(20), K_NO_WAIT);
	k_msleep(100);

	TEST_ASSERT_EQUAL(1, timer_submitted_executed);
	k_timer_stop(&submit_timer);
}

/* Custom queue with cfg (multi-queue + k_work_queue_config exercise). */
K_THREAD_STACK_DEFINE(custom_wq_stack, 4096);
static struct k_work_q custom_wq;
static volatile int custom_q_executed;

static void custom_q_handler(struct k_work *work)
{
	(void)work;
	custom_q_executed++;
}

static void test_work_custom_queue_with_cfg(void)
{
	const struct k_work_queue_config cfg = {
		.name = "custom_wq",
		.no_yield = false,
		.essential = false,
	};
	k_work_queue_start(&custom_wq, custom_wq_stack, sizeof(custom_wq_stack), 5, &cfg);

	struct k_work work;
	k_work_init(&work, custom_q_handler);
	custom_q_executed = 0;

	TEST_ASSERT_EQUAL(1, k_work_submit_to_queue(&custom_wq, &work));
	k_work_queue_drain(&custom_wq, false);
	TEST_ASSERT_EQUAL(1, custom_q_executed);
}

static void test_work_schedule_for_queue(void)
{
	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, custom_q_handler);
	custom_q_executed = 0;

	k_work_schedule_for_queue(&custom_wq, &dwork, K_MSEC(20));
	k_msleep(100);
	TEST_ASSERT_EQUAL(1, custom_q_executed);
}

static volatile TaskHandle_t reschedule_ran_on;

static void reschedule_queue_handler(struct k_work *work)
{
	(void)work;
	reschedule_ran_on = xTaskGetCurrentTaskHandle();
	custom_q_executed++;
}

static void test_work_reschedule_for_queue(void)
{
	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, reschedule_queue_handler);
	custom_q_executed = 0;
	reschedule_ran_on = NULL;

	/* Schedule on system queue for 500ms, then reschedule onto custom queue at 50ms */
	k_work_schedule(&dwork, K_MSEC(500));
	k_msleep(10);
	k_work_reschedule_for_queue(&custom_wq, &dwork, K_MSEC(50));

	k_msleep(200);
	TEST_ASSERT_EQUAL(1, custom_q_executed);
	TEST_ASSERT_EQUAL(custom_wq.thread, reschedule_ran_on);

	/* Wait past the original 500ms deadline to confirm it was cancelled */
	k_msleep(400);
	TEST_ASSERT_EQUAL(1, custom_q_executed);
}

static volatile int race_executed;

static void race_handler(struct k_work *work)
{
	(void)work;
	race_executed++;
}

static void test_work_submit_cancel_race(void)
{
	/* Hammer submit/cancel for ~500ms to shake out lock/atomicity
	 * bugs in the dlist+sem implementation. The exact executed
	 * count is nondeterministic; we only assert no crash, no leak
	 * (final state has no QUEUED/RUNNING flags), and that *some*
	 * work made it through. */
	struct k_work work;
	k_work_init(&work, race_handler);
	race_executed = 0;

	int64_t deadline = k_uptime_get() + 200;
	while (k_uptime_get() < deadline) {
		k_work_submit(&work);
		k_work_cancel(&work);
	}

	k_work_queue_drain(&k_sys_work_q, false);
	TEST_ASSERT_EQUAL(0, k_work_busy_get(&work));
	/* Loose lower bound -- some submits must have completed before
	 * a competing cancel reached them. */
	TEST_ASSERT_GREATER_THAN(0, race_executed);
}

static void test_work_init_macro_static(void)
{
	/* K_WORK_INIT static initializer (struct-literal form). */
	struct {
		struct k_work w;
	} container = {.w = K_WORK_INIT(test_work_handler)};
	TEST_ASSERT_EQUAL_PTR(test_work_handler, container.w.handler);
	TEST_ASSERT_EQUAL(0, container.w.flags);
}

/* Regression for the deadlock that occurred when k_work_cancel removed a
 * QUEUED item without releasing any flush waiter on work->sync. Helper
 * thread calls k_work_flush; main thread cancels the victim. Without the
 * fix, the flush helper blocks forever and this test times out. */
K_THREAD_STACK_DEFINE(flush_helper_stack, 4096);
static struct k_thread flush_helper;
static volatile bool flush_returned;
static struct k_work flush_victim;
static struct k_work_sync flush_sync;

static void flush_helper_entry(void *p1, void *p2, void *p3)
{
	(void)p1;
	(void)p2;
	(void)p3;
	k_work_flush(&flush_victim, &flush_sync);
	flush_returned = true;
	vTaskSuspend(NULL);
}

static void test_work_flush_unblocked_by_cancel(void)
{
	struct k_work blocker;
	k_work_init(&blocker, blocking_handler);
	k_work_init(&flush_victim, test_work_handler);
	k_sem_init(&block_sem, 0, 1);
	flush_returned = false;

	/* Block worker, then submit victim -- it stays QUEUED. */
	k_work_submit(&blocker);
	k_msleep(20);
	k_work_submit(&flush_victim);
	TEST_ASSERT_TRUE(k_work_busy_get(&flush_victim) & K_WORK_QUEUED);

	/* Helper thread blocks on k_work_flush. */
	k_thread_create(&flush_helper, flush_helper_stack,
			K_THREAD_STACK_SIZEOF(flush_helper_stack), flush_helper_entry, NULL, NULL,
			NULL, 5, 0, K_NO_WAIT);
	k_msleep(50); /* let helper enter k_sem_take */
	TEST_ASSERT_FALSE_MESSAGE(flush_returned, "flush returned before cancel -- bad test setup");

	/* Cancel must release the flush waiter; item idle -> 0 */
	TEST_ASSERT_EQUAL(0, k_work_cancel(&flush_victim));
	k_msleep(50);
	TEST_ASSERT_TRUE_MESSAGE(flush_returned,
				 "k_work_flush deadlocked: cancel didn't release sync waiter");

	/* Cleanup: unblock worker, drain, abort helper. */
	k_sem_give(&block_sem);
	k_work_queue_drain(&k_sys_work_q, false);
	k_thread_abort(&flush_helper);
}

/* ----------------------------------------------------------------
 * Upstream return-code parity (submit/schedule/reschedule families)
 * ---------------------------------------------------------------- */

static void test_work_return_codes_submit_running(void)
{
	/* Upstream: submitting a RUNNING item queues it again, retval 2. */
	struct k_work work;
	k_work_init(&work, blocking_handler);
	k_sem_init(&block_sem, 0, 2);

	TEST_ASSERT_EQUAL(1, k_work_submit(&work)); /* idle -> queued */
	k_msleep(20);                               /* worker parks in handler */
	TEST_ASSERT_TRUE(k_work_busy_get(&work) & K_WORK_RUNNING);

	TEST_ASSERT_EQUAL(2, k_work_submit(&work)); /* running -> queued again */

	/* Release both executions (the second pops after the first ends). */
	k_sem_give(&block_sem);
	k_sem_give(&block_sem);
	k_work_queue_drain(&k_sys_work_q, false);
	TEST_ASSERT_EQUAL(0, k_work_busy_get(&work));
}

static void test_work_return_codes_schedule(void)
{
	struct k_work_delayable dwork;
	k_work_init_delayable(&dwork, test_work_handler);
	work_executed = 0;

	/* Arm -> 1; second schedule while armed is a no-op -> 0 (and must
	 * NOT shorten the original deadline). */
	TEST_ASSERT_EQUAL(1, k_work_schedule(&dwork, K_MSEC(200)));
	TEST_ASSERT_EQUAL(0, k_work_schedule(&dwork, K_MSEC(10)));
	k_msleep(50);
	TEST_ASSERT_EQUAL(0, work_executed); /* 10ms re-arm would have fired */

	/* Reschedule cancels and re-arms -> 1, fires at the new deadline. */
	TEST_ASSERT_EQUAL(1, k_work_reschedule(&dwork, K_MSEC(10)));
	k_msleep(100);
	TEST_ASSERT_EQUAL(1, work_executed);

	/* K_NO_WAIT path delegates to submit: idle -> 1. */
	TEST_ASSERT_EQUAL(1, k_work_schedule(&dwork, K_NO_WAIT));
	k_msleep(50);
	TEST_ASSERT_EQUAL(2, work_executed);
}

static void test_work_return_codes_errors(void)
{
	/* Queue not started -> -ENODEV (upstream). */
	static struct k_work_q unstarted; /* zero-initialized, never started */
	struct k_work work;
	k_work_init(&work, test_work_handler);
	TEST_ASSERT_EQUAL(-ENODEV, k_work_submit_to_queue(&unstarted, &work));

	/* No handler -> -EINVAL (upstream). */
	struct k_work no_handler;
	k_work_init(&no_handler, NULL);
	TEST_ASSERT_EQUAL(-EINVAL, k_work_submit(&no_handler));
}

/* ----------------------------------------------------------------
 * CANCELING enforcement + k_work_cancel_delayable_sync
 * ---------------------------------------------------------------- */

static struct k_work_delayable self_dwork;
static volatile int self_ticks;

/* Periodic-service pattern, tick-then-reschedule variant. */
static void self_resched_tick_first(struct k_work *work)
{
	struct k_work_delayable *d = CONTAINER_OF(work, struct k_work_delayable, work);

	self_ticks++;
	k_work_reschedule(d, K_MSEC(20));
}

/* Reschedule-then-tick variant (re-arm happens before the visible
 * side effect, the worst case for a racy cancel). */
static void self_resched_arm_first(struct k_work *work)
{
	struct k_work_delayable *d = CONTAINER_OF(work, struct k_work_delayable, work);

	k_work_reschedule(d, K_MSEC(20));
	self_ticks++;
}

static void run_self_rescheduler_stop(k_work_handler_t handler)
{
	struct k_work_sync sync;

	k_work_init_delayable(&self_dwork, handler);
	self_ticks = 0;

	TEST_ASSERT_EQUAL(1, k_work_schedule(&self_dwork, K_MSEC(10)));
	k_msleep(100); /* let it tick several times */
	TEST_ASSERT_GREATER_OR_EQUAL(2, self_ticks);

	/* One call stops the loop, no matter where in its cycle it is. */
	TEST_ASSERT_TRUE(k_work_cancel_delayable_sync(&self_dwork, &sync));

	int snapshot = self_ticks;
	k_msleep(150); /* > 2 re-arm periods */
	TEST_ASSERT_EQUAL(snapshot, self_ticks);
	TEST_ASSERT_FALSE(k_work_delayable_is_pending(&self_dwork));
	TEST_ASSERT_EQUAL(0, k_work_busy_get(&self_dwork.work));
}

static void test_work_cancel_delayable_sync_stops_tick_first(void)
{
	run_self_rescheduler_stop(self_resched_tick_first);
}

static void test_work_cancel_delayable_sync_stops_arm_first(void)
{
	run_self_rescheduler_stop(self_resched_arm_first);
}

static void test_work_cancel_delayable_sync_idle(void)
{
	struct k_work_delayable dwork;
	struct k_work_sync sync;

	k_work_init_delayable(&dwork, test_work_handler);

	/* Idle work: nothing to cancel -> false */
	TEST_ASSERT_FALSE(k_work_cancel_delayable_sync(&dwork, &sync));
	TEST_ASSERT_EQUAL(0, k_work_busy_get(&dwork.work));
}

/* Probe the CANCELING window: a helper thread blocks in
 * k_work_cancel_sync while the handler is parked; main asserts that
 * submission is rejected with -EBUSY for the duration. */
K_THREAD_STACK_DEFINE(cancel_helper_stack, 4096);
static struct k_thread cancel_helper;
static struct k_work canceling_probe_work;
static struct k_work_sync canceling_probe_sync;
static volatile bool cancel_helper_result;
static volatile bool cancel_helper_done;

static void cancel_helper_entry(void *p1, void *p2, void *p3)
{
	(void)p1;
	(void)p2;
	(void)p3;
	cancel_helper_result = k_work_cancel_sync(&canceling_probe_work, &canceling_probe_sync);
	cancel_helper_done = true;
}

static void test_work_submit_during_cancel_returns_ebusy(void)
{
	k_work_init(&canceling_probe_work, blocking_handler);
	k_sem_init(&block_sem, 0, 1);
	cancel_helper_done = false;
	cancel_helper_result = false;

	/* Park the handler. */
	TEST_ASSERT_EQUAL(1, k_work_submit(&canceling_probe_work));
	k_msleep(20);
	TEST_ASSERT_TRUE(k_work_busy_get(&canceling_probe_work) & K_WORK_RUNNING);

	/* Helper blocks in cancel_sync waiting out the handler. */
	k_thread_create(&cancel_helper, cancel_helper_stack,
			K_THREAD_STACK_SIZEOF(cancel_helper_stack), cancel_helper_entry, NULL, NULL,
			NULL, 5, 0, K_NO_WAIT);
	k_msleep(30);
	TEST_ASSERT_FALSE(cancel_helper_done);
	TEST_ASSERT_TRUE(k_work_busy_get(&canceling_probe_work) & K_WORK_CANCELING);

	/* The CANCELING window rejects submission... */
	TEST_ASSERT_EQUAL(-EBUSY, k_work_submit(&canceling_probe_work));
	/* ...and a plain cancel reports the still-busy state. */
	TEST_ASSERT_TRUE(k_work_cancel(&canceling_probe_work) & K_WORK_RUNNING);

	/* Release the handler; the helper's cancel_sync completes. */
	k_sem_give(&block_sem);
	TEST_ASSERT_EQUAL(0, k_thread_join(&cancel_helper, K_SECONDS(2)));
	TEST_ASSERT_TRUE(cancel_helper_done);
	TEST_ASSERT_TRUE(cancel_helper_result); /* was pending */
	TEST_ASSERT_EQUAL(0, k_work_busy_get(&canceling_probe_work));
}

void test_k_work_group(void)
{
	RUN_TEST(test_work_submit);
	RUN_TEST(test_work_submit_idempotent);
	RUN_TEST(test_work_cancel);
	RUN_TEST(test_work_is_pending);
	RUN_TEST(test_work_delayable);
	RUN_TEST(test_work_cancel_delayable);
	RUN_TEST(test_work_reschedule);
	RUN_TEST(test_work_delayable_remaining);
	RUN_TEST(test_work_submit_to_queue);
	RUN_TEST(test_work_flush);
	RUN_TEST(test_work_flush_not_pending);
	RUN_TEST(test_work_cancel_while_pending);
	RUN_TEST(test_work_busy_get_states);
	RUN_TEST(test_work_cancel_sync_running);
	RUN_TEST(test_work_submit_from_timer_callback);
	RUN_TEST(test_work_custom_queue_with_cfg);
	RUN_TEST(test_work_schedule_for_queue);
	RUN_TEST(test_work_reschedule_for_queue);
	RUN_TEST(test_work_submit_cancel_race);
	RUN_TEST(test_work_init_macro_static);
	RUN_TEST(test_work_flush_unblocked_by_cancel);
	RUN_TEST(test_work_return_codes_submit_running);
	RUN_TEST(test_work_return_codes_schedule);
	RUN_TEST(test_work_return_codes_errors);
	RUN_TEST(test_work_cancel_delayable_sync_stops_tick_first);
	RUN_TEST(test_work_cancel_delayable_sync_stops_arm_first);
	RUN_TEST(test_work_cancel_delayable_sync_idle);
	RUN_TEST(test_work_submit_during_cancel_returns_ebusy);
}
