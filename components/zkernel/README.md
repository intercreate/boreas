# zkernel

Zephyr-compatible kernel primitives over FreeRTOS. All objects are statically allocated (no malloc). All blocking APIs handle `K_NO_WAIT`, `K_FOREVER`, and finite timeouts. ISR-safe variants are used automatically where applicable.

## Design principle: object lifetime must be Zephyr-shaped

Upstream Zephyr kernel objects are caller-owned plain structs whose lifecycle
ends are *synchronous*: when `k_thread_abort`/`k_thread_join`/`k_timer_stop`/
`k_work_cancel_sync` return, the kernel holds **zero references** into the
caller's memory — which is what makes stack allocation idiomatic in Zephyr
code. FreeRTOS assumes the opposite: long-lived objects, handle-based
ownership, asynchronous teardown (idle-task reaping), and kernel list nodes
threaded *through* control blocks with no "the kernel is done with this
memory" signal.

Boreas embeds FreeRTOS control blocks (`StaticTask_t`, `StaticSemaphore_t`,
…) inside caller-owned Zephyr-shaped structs, importing Zephyr's memory model
— so every Boreas API that ends an object's kernel involvement MUST sever all
kernel references before returning (or document loudly where a target port
makes that impossible). Violations are not theoretical: a parked task whose
control block outlived its stack frame caused months of intermittent
scheduler corruption (issues #18, #21 — kernel list operations writing
through dangling nodes into reused frames). New APIs must be designed against
this principle, and any divergence belongs in an `@note` on the declaration.

Current status: `k_thread` severs synchronously on silicon and documents the
linux best-effort window; `k_work`/`k_work_sync` unlink synchronously via
their own state machine; `k_sem` is notification-backed (nothing of the
caller's memory ever enters a kernel list); the k_timer linux backend
dequeues synchronously on stop; `k_mutex`/`k_msgq`/`k_event` waiters are
unlinked by FreeRTOS
before the blocking call returns (a *blocked* caller's frame is necessarily
live, and the control-block updates that wake a waiter complete before the
waiter runs — but the giving/sending context may be preempted by the woken
waiter before its own call returns, and can still touch the control block
afterwards, especially under SMP. Do not let a stack-allocated object's
frame die while another context may still be inside a give/send on it;
prefer static storage for objects signaled from other contexts).

## Headers

| Header | Contents |
|--------|----------|
| `zephyr/kernel.h` | All kernel primitives (include this) |
| `zephyr/sys/time_units.h` | `k_timeout_t`, `K_MSEC`, `K_FOREVER`, `K_NO_WAIT` |
| `zephyr/sys/util.h` | `BIT`, `CONTAINER_OF`, `ARRAY_SIZE`, `BUILD_ASSERT`, etc. |
| `zephyr/sys/dlist.h` | Intrusive doubly-linked list (`sys_dlist_t`) |
| `zephyr/sys/slist.h` | Intrusive singly-linked list (`sys_slist_t`) |
| `zephyr/sys/atomic.h` | Atomic ops (`atomic_t`, `atomic_get`/`set`/`or`/...) -- transitively included by `kernel.h` |
| `zephyr/sys/byteorder.h` | Byte-buffer LE/BE access (`sys_get_le16`/`be16`/`le32`/`be32` + `put` variants) -- transitively included by `kernel.h` |
| `zephyr/init.h` | `SYS_INIT` ordered initialization framework |
| `zephyr/fatal.h` | `k_fatal_error`, assertion macros |

## Timeout API

Timeouts store **microseconds** internally. Conversion to FreeRTOS ticks happens at the point of use (quantized to tick rate), and microsecond access for esp_timer is lossless.

```c
K_USEC(500)       // 500us (lossless for esp_timer)
K_MSEC(15)        // 15000us
K_SECONDS(2)      // 2000000us
K_NO_WAIT         // non-blocking (0)
K_FOREVER         // block indefinitely (-1)

k_timeout_to_us(t)     // direct microsecond value (lossless)
k_timeout_to_ms(t)     // microseconds / 1000
k_timeout_to_ticks(t)  // for FreeRTOS APIs (quantized to tick rate)
```

## Semaphore (`k_sem`)

```c
struct k_sem sem;
k_sem_init(&sem, 0, 10);        // initial=0, limit=10
k_sem_take(&sem, K_MSEC(100));  // returns 0, -EBUSY, or -EAGAIN
k_sem_give(&sem);               // ISR-safe
k_sem_reset(&sem);
k_sem_count_get(&sem);
```

| Return | Meaning |
|--------|---------|
| `0` | Success |
| `-EBUSY` | Not available (K_NO_WAIT) |
| `-EAGAIN` | Timeout expired, or the semaphore was reset while waiting |
| `-EINVAL` | `k_sem_init` with `limit == 0` or `initial > limit` |

**Notification-backed** (no FreeRTOS control block): the count and waiter
list live in the caller-owned struct; blocking rides direct-to-task
notifications on **reserved index 1** (requires
`CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES >= 2`, enforced at compile
time). Consequences:

- `K_SEM_DEFINE` is a true compile-time initializer (usable from any
  constructor — upstream parity)
- `k_sem_reset` wakes all waiters with `-EAGAIN` (upstream parity)
- `k_sem_give` wakes the highest-priority waiter (upstream parity)
- when `k_sem_take` returns, the kernel holds zero references into the
  caller's struct — the design principle above, by construction

**ISR-safe:** `k_sem_give` (uses `vTaskNotifyGiveIndexedFromISR`
automatically). Do not use task-notification index 1 directly in
application code.

## Mutex (`k_mutex`)

```c
struct k_mutex mtx;
k_mutex_init(&mtx);
k_mutex_lock(&mtx, K_FOREVER);  // re-entrant (same thread can lock again)
k_mutex_unlock(&mtx);
```

Returns `-EBUSY` (K_NO_WAIT), `-EAGAIN` (timeout), `-EWOULDBLOCK` (called from ISR), `-EPERM` (unlock not owner).

Re-entrant (same thread can lock multiple times) with priority inheritance. Thin wrapper over a FreeRTOS recursive mutex, which provides both.

## Message Queue (`k_msgq`)

```c
K_MSGQ_DEFINE(my_q, sizeof(struct msg), 16, 4);
k_msgq_init(&my_q, ...);       // or use K_MSGQ_DEFINE for static

k_msgq_put(&my_q, &msg, K_MSEC(10));  // ISR-safe
k_msgq_get(&my_q, &msg, K_FOREVER);   // ISR-safe
k_msgq_peek(&my_q, &msg);
k_msgq_purge(&my_q);
k_msgq_num_used_get(&my_q);
k_msgq_num_free_get(&my_q);
```

Returns `-ENOMSG` (empty/full + K_NO_WAIT), `-EAGAIN` (timeout).

Nearly 1:1 mapping to FreeRTOS queues. ISR-safe on both put and get.

## Event (`k_event`)

```c
struct k_event evt;
k_event_init(&evt);
k_event_set(&evt, BIT(0) | BIT(3));           // ISR-safe
k_event_wait(&evt, BIT(0), true, K_FOREVER);  // wait for ANY, auto-reset
k_event_wait_all(&evt, 0x07, false, K_MSEC(100));  // wait for ALL
k_event_clear(&evt, BIT(0));                   // ISR-safe
```

**Notification-backed** (no FreeRTOS event group): the full 32-bit events
word is available (the old event-group backend reserved bits 24-31).
Upstream semantics throughout: `set` replaces (use `post` to merge),
mutators return the previous value, `reset` zeroes the whole tracked set
before waiting, `wait_all` returns 0 on timeout, and the `_safe` wait
variants atomically consume matched bits. Same blocking architecture and
caveats as `k_sem` (reserved notification index; do not abort a blocked
waiter).

## Timer (`k_timer`)

```c
void my_expiry(struct k_timer *t) { /* periodic callback */ }
void my_stop(struct k_timer *t)   { /* called on stop */ }

struct k_timer timer;
k_timer_init(&timer, my_expiry, my_stop);
k_timer_start(&timer, K_MSEC(100), K_MSEC(1000));  // first=100ms, period=1s
k_timer_stop(&timer);
k_timer_status_get(&timer);     // expiry count since last read (atomic)
k_timer_status_sync(&timer);    // block until next expiry or stop, then return count
k_timer_remaining_get(&timer);  // ms until next expiry
k_timer_remaining_ticks(&timer); // FreeRTOS ticks until next expiry
k_timer_expires_ticks(&timer);  // absolute uptime ticks of next expiry
k_timer_user_data_set(&timer, ptr);
```

Backed by `esp_timer` for microsecond resolution (not limited by FreeRTOS tick rate). Supports different first-interval vs periodic interval via internal state machine.

**BEHAVIORAL DELTA:** Callbacks run in `ESP_TIMER_TASK` context (a normal task), not ISR context as in Zephyr. Callbacks CAN call blocking APIs but ARE preemptible.

## Work Queue (`k_work`)

```c
// Immediate work (system work queue is auto-initialized)
struct k_work work;
k_work_init(&work, my_handler);
k_work_submit(&work);           // ISR-safe, non-blocking

// Delayed work
struct k_work_delayable dwork;
k_work_init_delayable(&dwork, my_handler);
k_work_schedule(&dwork, K_SECONDS(2));
k_work_reschedule(&dwork, K_MSEC(500));  // cancel + reschedule
k_work_cancel_delayable(&dwork);

// Flush -- block until a running work item completes
struct k_work_sync sync;
k_work_flush(&work, &sync);

// Cancel + wait for completion
k_work_cancel_sync(&work, &sync);

// Inspect state (K_WORK_QUEUED | RUNNING | CANCELING bitmask)
uint32_t flags = k_work_busy_get(&work);
```

Backed internally by an intrusive `sys_dlist_t` of pending items + a counting `k_sem` to wake the worker. Mutations are guarded by a per-queue spinlock (`portMUX_TYPE`), ISR-safe. **`k_work_cancel` synchronously removes the item from the queue** -- matches upstream Zephyr's contract.

### System work queue

Auto-initialized before `main()` via constructor. Priority and stack size come from Kconfig:

- `CONFIG_SYSTEM_WORKQUEUE_PRIORITY` (default 5)
- `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` (default 4096)
- `CONFIG_SYSTEM_WORKQUEUE_NO_YIELD` (default n)

Ready to use from `app_main`, `SYS_INIT` callbacks, and constructors that run after zkernel's.

### Custom work queues

Use the upstream Zephyr pattern -- there is no `K_WORK_QUEUE_DEFINE` macro:

```c
K_THREAD_STACK_DEFINE(my_wq_stack, 4096);
static struct k_work_q my_wq;

const struct k_work_queue_config cfg = {
    .name = "my_wq",
    .no_yield = false,
};
k_work_queue_start(&my_wq, my_wq_stack, K_THREAD_STACK_SIZEOF(my_wq_stack),
                   K_PRIO_PREEMPT(5), &cfg);

k_work_submit_to_queue(&my_wq, &work);
k_work_schedule_for_queue(&my_wq, &dwork, K_MSEC(100));

// Block until pending work has completed (submits a sentinel internally)
k_work_queue_drain(&my_wq, false);
```

`struct k_work_queue_config` matches upstream: `name`, `no_yield`, `essential`. The `essential` field is currently ignored on Boreas. The `bool plug` parameter on `k_work_queue_drain` is reserved for upstream parity but currently a no-op (Boreas does not implement plugging).

Priorities can be specified with `K_PRIO_PREEMPT(p)` (mirrors upstream Zephyr's preemptible-priority macro). On ESP-IDF/FreeRTOS this maps to a numeric task priority; cooperative threads (`K_PRIO_COOP`) map identically since FreeRTOS does not distinguish.

## Thread (`k_thread`)

```c
K_THREAD_STACK_DEFINE(my_stack, 4096);
struct k_thread my_thread;

// Start immediately
k_thread_create(&my_thread, my_stack, K_THREAD_STACK_SIZEOF(my_stack),
                my_entry, arg1, arg2, arg3,
                K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

// Start suspended -- must call k_thread_resume to begin
k_thread_create(&my_thread, ..., K_FOREVER);
k_thread_resume(&my_thread);

// Start after delay (uses esp_timer internally)
k_thread_create(&my_thread, ..., K_MSEC(500));

k_thread_name_set(&my_thread, "worker");
k_thread_join(&my_thread, K_FOREVER);
k_thread_suspend(&my_thread);
k_thread_resume(&my_thread);
k_thread_abort(&my_thread);
k_thread_stack_space_get(&my_thread, &unused);

k_tid_t me = k_current_get();
```

Thread entry takes 3 void* params (Zephyr convention). On return, thread suspends (safe for static TCBs). Deferred start uses self-suspend (race-free on dual-core ESP32).

## Uptime and Sleep

```c
int64_t ms = k_uptime_get();          // monotonic milliseconds
int64_t delta = k_uptime_delta(&ref); // delta since last call

k_msleep(100);                // sleep 100ms
k_sleep(K_SECONDS(1));        // sleep with timeout type
k_usleep(500);                // sub-ms: busy-wait via esp_rom_delay_us
k_yield();                    // yield to same-priority tasks
```

## SYS_INIT

```c
static int my_init(void) { /* ... */ return 0; }
SYS_INIT(my_init, /*level=*/0, /*priority=*/50);
```

Ordered initialization. Entries are emplaced into the `.sys_init_entries` linker section at compile time and iterated by `sys_init_run_all()`. Lower level runs first, then by priority within level. `SYS_INIT` callsites must live in `main/` or any TU with an externally-referenced symbol. See `docs/linker-section-registration.md`.

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_ZKERNEL_SYS_INIT` | y | Enable SYS_INIT framework |
| `CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES` | 32 | Max SYS_INIT registrations |
| `CONFIG_ZKERNEL_FATAL_CAPTURE` | n | Save fatal context to NVS |

Required configuration ships in **`sdkconfig.boreas`** at the repo root —
add it to your project's defaults list (before `project.cmake`):

```cmake
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;path/to/boreas/sdkconfig.boreas")
```

It currently sets `CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES=2` —
zkernel reserves task-notification index 1 for its blocking primitives;
index 0 stays free for ESP-IDF internals. A compile-time `#error` backstops
the requirement (Kconfig cannot set another component's int symbol
automatically: `select` is bool-only and cross-component int defaults lose
the parse-order race).
