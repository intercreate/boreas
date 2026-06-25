# Changelog

Notable and breaking changes for downstream projects. Entries reference
the merge PR.

## Unreleased

## 0.1.0 — 2026-07-01

First tagged release. The 2026-06 hardening series is complete; the API
surface below is the baseline downstream projects pin to.

### Migration checklist (bumping across the 2026-06 hardening series)

1. **Add `sdkconfig.boreas` to your defaults list** (#41) — in your
   project `CMakeLists.txt`, before `project.cmake`:
   ```cmake
   set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;components/boreas/sdkconfig.boreas")
   ```
   Then regenerate config once: `rm -rf build sdkconfig && idf.py
   set-target <target>`. A compile-time error names the file if missing.
2. **Audit `k_work_submit*` / `k_work_schedule*` / `k_work_reschedule*`
   return checks** (#37) — success is no longer `0`. Upstream-parity
   codes: `0` = no-op (already queued/scheduled), `1` = queued/armed,
   `2` = was running and queued again, negative = error (`-ENODEV` for
   an unstarted queue, previously `-EINVAL`). `< 0` error checks are
   unaffected; `== 0` success checks must change.
3. **Audit boolean uses of `k_work_cancel`** (#38) — **polarity
   inverted**: it now returns the remaining busy state (`int`), so old
   `true` meant *cancelled* while new nonzero means *still busy*.
   `if (k_work_cancel(&w))` flips meaning. `k_work_cancel_sync` now
   returns `bool` was-pending (was `int 0`).
4. **Collapse triple-cancel workarounds** (#38) — self-rescheduling
   delayables are now stopped reliably with one
   `k_work_cancel_delayable_sync()` call; submission during a cancel
   is rejected with `-EBUSY`.
5. **Do not use task-notification index 1** in application code (#41)
   — reserved for zkernel blocking primitives.
6. **Do not abort threads blocked in `k_sem_take`** (#41) — see the
   `@note` on `k_sem_take`; upstream unpends aborted threads, Boreas
   cannot.

### Changed

- **k_sem is notification-backed** (#41): no FreeRTOS control block;
  `K_SEM_DEFINE` is a true compile-time initializer (usable from any
  constructor); `k_sem_reset` wakes waiters with `-EAGAIN`;
  `k_sem_give` wakes the highest-priority waiter; `k_sem_init` returns
  `-EINVAL` for invalid limits. When `k_sem_take` returns, the kernel
  holds no references into the caller's struct.
- **k_work cancel family enforces `K_WORK_CANCELING`** (#38) and
  removes a queued-again-while-running instance on cancel.
- **k_work return codes match upstream Zephyr** (#37); the schedule
  no-op window and schedule-while-running semantics now match upstream.
- **k_thread lifecycle honors the caller-owned-memory contract** (#18):
  a returning entry function terminates the thread; `k_thread_join`
  reclaims it (codes: `-EBUSY` no-wait, `-EDEADLK` self-join,
  `-EAGAIN` timeout) and no longer false-joins suspended threads.

### Fixed

- The 2026-04 stack-local `k_sem` scheduler corruption was root-caused
  to the pre-#18 k_thread zombie defect and is fixed since #18; the
  trigger shapes are permanent regression tests (#39, issue #21).
- **`k_sem_give` no longer cache-faults from an IRAM ISR** (#54, closes
  #53): `z_sem_pop_waiter` and the other `K_ISR_SAFE` helpers are now
  IRAM-resident (`.iram0.text`), so giving a semaphore or submitting work
  from an `ESP_INTR_FLAG_IRAM` interrupt during a flash-cache-disabled
  window no longer faults. A build-time guard
  (`tools/check_iram_symbols.sh`) asserts the placement on the target ELF.
