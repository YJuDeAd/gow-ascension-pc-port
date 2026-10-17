---
slug: implement-real-sync-primitives
date: 2026-10-17
description: Implement real PS3 LV2 sys_lwmutex and sys_lwcond semantics in src/compat/ without polling
---

# Plan: Implement Real sys_lwmutex & sys_lwcond Semantics

## Root Cause Analysis
In `ps3recomp/runtime/ppu/ppu_sysprx.cpp`, `sys_lwcond_signal` and `sys_lwcond_signal_all` are literal no-ops (`ctx->gpr[3] = 0`), and `sys_lwcond_wait` executes a non-atomic `Sleep(1)` polling loop over a Win32 semaphore with maxCount 1. When multiple threads wait concurrently, semaphore release overflows and subsequent threads block forever. Signaled events are completely lost when no waiter is active at the instant of signal, causing FIOS scheduler (`tid=4`) and media threads (`tid=3`) to starve and leaving main (`tid=1`) spinning.

## Proposed Architecture in `src/compat/ascension_hle.cpp`

### 1. Host Structures & Mapping
- **`GowLwMutex`**:
  - Keyed by guest address `uint32_t`.
  - Flags: `recursive` (`SYS_SYNC_RECURSIVE` = 0x10 vs `SYS_SYNC_NOT_RECURSIVE` = 0x20).
  - State: `owner_tid` (`uint32_t`), `recursion_count` (`uint32_t`), `waiters_count` (`uint32_t`).
  - Waiter queue: FIFO linked list of `GowSyncWaiter` nodes with thread IDs and auto-reset Win32 events.
  - Consistent guest memory mirror: `+0x00` owner, `+0x04` waiter count, `+0x08` attribute, `+0x0C` recursive count.
- **`GowLwCond`**:
  - Keyed by guest address `uint32_t`.
  - State: associated `lwmutex_addr` (`uint32_t`).
  - Waiter queue: FIFO linked list of `GowSyncWaiter` nodes.
  - Telemetry: waits, wakeups, signals, signal_all, signal_to, zero-waiter signals.
- **Global Mutex `g_gow_sync_cs`**:
  - `CRITICAL_SECTION` protecting all lookups and state transitions atomically.

### 2. Operational Semantics
- **`sys_lwmutex_create(mutex*, attr*)`**: Register host object, record recursive flag, zero guest fields, return 0.
- **`sys_lwmutex_lock(mutex*, timeout_usec)`**:
  - 0 timeout = infinite.
  - If owned by caller: if recursive, `count++`, return 0; if non-recursive, return `CELL_EDEADLK` (0x80010008).
  - If unowned: acquire, `owner = caller`, `count = 1`, return 0.
  - If owned by another: queue waiter, leave CS, wait on event with timeout, re-enter CS. Return `CELL_ETIMEDOUT` (0x8001000B) on timeout.
- **`sys_lwmutex_trylock(mutex*)`**: If unowned or recursive self-owned, acquire/increment. If owned by another, return `CELL_EBUSY` (0x8001000A).
- **`sys_lwmutex_unlock(mutex*)`**: If not owned, return `CELL_EPERM` (0x80010009). Decrement count; if 0, wake one waiter FIFO.
- **`sys_lwmutex_destroy(mutex*)`**: Return `CELL_EBUSY` if waiters or held; free state.
- **`sys_lwcond_create(cond*, lwmutex*, attr*)`**: Record associated `lwmutex`, write guest fields, return 0.
- **`sys_lwcond_wait(cond*, timeout_usec)`**:
  - Extract `lwmutex` from cond object.
  - Verify caller holds mutex; if not, return `CELL_EPERM`.
  - Save `recursion_count`, fully release mutex (wake next mutex waiter if any).
  - Queue on `cond`'s waiter queue.
  - Wait on event with timeout (0 = infinite).
  - Re-acquire `lwmutex` (blocking if needed) and restore `recursion_count`.
  - If timed out, return `CELL_ETIMEDOUT` with mutex re-acquired.
- **`sys_lwcond_signal(cond*)`**: Pop & signal head waiter FIFO. No-op if empty.
- **`sys_lwcond_signal_all(cond*)`**: Pop & signal all waiters. No-op if empty.
- **`sys_lwcond_signal_to(cond*, ppu_tid)`**: Search for `ppu_tid` in waiter queue; wake if found, else return `CELL_ESRCH` (0x80010005).
- **`sys_lwcond_destroy(cond*)`**: Return `CELL_EBUSY` if waiters; free state.

## Verification
1. Rebuild `gow_ascension.exe`.
2. Run `.\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 40`.
3. Verify no thread deadlocks or lost wakeups in `run_port_stderr.log`.
4. Save patch as `patches/sync-primitives.patch`.
