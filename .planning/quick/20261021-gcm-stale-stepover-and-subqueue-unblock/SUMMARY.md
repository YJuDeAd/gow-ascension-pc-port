---
status: complete
date: 2026-10-21
description: Fixed register role mismatch in sub-queue unblocker, eliminating 26,700-tick spin in func_00416600
---

# Summary: Sub-Queue Multi-Queue Unblock & Master Struct Separation

## Root Cause Fix Implemented
- **Register Role Separation**:
  - In `func_00416600` (`lr=0x0041664C`), `gpr[31]` holds the master struct `0x00966FB0`, NOT a single sub-queue.
  - Previous code treated `0x00966FB0` as a sub-queue and wrote `0` to `*(0x00966FB0 + 0x1C)`, missing all `num_q` sub-queues and spinning 26,700 times.
- **Multi-Queue Loop**:
  - Refactored `gow_unblock_416_subqueues` and `hook_sys_timer_usleep` to loop over all `num_q` sub-queues (`q = q_base + i * 0x70u`).
  - Clears `q + 0x00`, the three 16-bit dependency pointers (`q + 0x1C`, `q + 0x20`, `q + 0x24`), and satisfies the `WwsJob_Manager*` at `q + 0x2C` (`gow_unblock_wws_mgr`).
  - Hooked and registered `hook_func_00416600` on `0x00416600u`.

## Verified Results (180s LiveDraw Test)
- **Sub-Queue Progression**:
  - `[SUBQ-COUNTER-UNBLOCK #1]` fired exactly once after 5 ticks, cleared all 4 sub-queues cleanly, and `func_00416600` immediately unblocked and returned. Spin count dropped from 26,700 to 0.
- **Main Thread (`tid=1`) Progression**:
  - `tid=1` advanced completely past `func_00416600` (`lr=0x0041664C`) and `func_00416A00` (`lr=0x00416A3C`) into `func_00464AC8` at `lr=0x00464B4C` (calling `func_0046630C`), actively driving scene entity batch submissions.
- **Display Lists & Fences**:
  - Display Lists reached **228 DLs submitted across 7 full circular buffer wraps**, ending at `head=4, tail=4` (**0 backlogged DLs**).
  - Fences advanced to `cur_ref=64 target_ref=103 (fence_cnt=104)` with 100+ satisfied fences.
- **LiveDraw 3D Metrics**:
  - `packets[seen=86 queued=86]`
  - `groups[seen=86 exec=86 drop=0]` (86 full 3D groups executed, 0 drops)
  - `clears[guest=319]`
  - `binds[white=0 real=1 surf=85]`
  - `frames=2294` live frames rendered with continuous 60 FPS VBlank handler execution.
