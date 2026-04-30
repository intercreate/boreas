# zkernel

Zephyr-compatible kernel primitives over FreeRTOS. All objects are statically allocated (no malloc). All blocking APIs handle `K_NO_WAIT`, `K_FOREVER`, and finite timeouts. ISR-safe variants are used automatically where applicable.

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
| `-EAGAIN` | Timeout expired |

**ISR-safe:** `k_sem_give` (uses `xSemaphoreGiveFromISR` automatically).

## Mutex (`k_mutex`)

```c
struct k_mutex mtx;
k_mutex_init(&mtx);
k_mutex_lock(&mtx, K_FOREVER);  // re-entrant (same thread can lock again)
k_mutex_unlock(&mtx);
```

Returns `-EBUSY` (K_NO_WAIT), `-EAGAIN` (timeout), `-EWOULDBLOCK` (called from ISR), `-EPERM` (unlock not owner).

Re-entrant (same thread can lock multiple times) with priority inheritance. Uses a non-recursive FreeRTOS mutex (which has PI) with manual re-entrancy tracking.

**Extension:** `CONFIG_ZKERNEL_MUTEX_DEBUG` enables lock-ordering assertions and hold-time warnings.

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

**BEHAVIORAL DELTA:** FreeRTOS reserves bits 24-31 for internal use. Only bits 0-23 are available (vs Zephyr's 32 bits).

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
| `CONFIG_ZKERNEL_MUTEX_DEBUG` | n | Lock-ordering assertions + hold-time warnings |
| `CONFIG_ZKERNEL_MUTEX_HOLD_WARNING_MS` | 0 | Hold-time warning threshold (0=disabled) |
| `CONFIG_ZKERNEL_SYS_INIT` | y | Enable SYS_INIT framework |
| `CONFIG_ZKERNEL_SYS_INIT_MAX_ENTRIES` | 32 | Max SYS_INIT registrations |
| `CONFIG_ZKERNEL_FATAL_CAPTURE` | n | Save fatal context to NVS |
