---
status: complete
date: 2026-10-21
description: Fixed extra calculation wrap-around at target >= 32 and CellSpurs* pointer corruption in GoW Ascension
---

# Summary: SPU Wws_Job Target Mask Wrap-Around & CellSpurs* Pointer Fix

## Root Cause Fixes Implemented
1. **Removed `& 0x1Fu` Mask in `extra` Calculation (`src/compat/ascension_hle.cpp`)**:
   - Replaced `((target & 0x1Fu) << 3)` with `((uint32_t)target << 3)` across all 4 locations (`hook_func_00466B88`, `hook_func_00466E28`, `brbDriver`, and `WWS-MAIN-UNBLOCK`).
   - Expanded safety bound from `extra < 32` to `extra < 512`.
2. **Prevented `CellSpurs*` Pointer Corruption**:
   - Removed `vm_write32(mgr_ea + 4, 1)` and `vm_write32(r31_ptr + 4, 1)`. When `spurs_ptr == 0`, `func_00466E28` now exits cleanly at `loc_00466FB8` rather than spinning with `spurs=0x1` in `cellSpursGetWorkloadInfo`.
3. **Synchronized Buffer Set & Desired Target in `WWS-MAIN-UNBLOCK` & `hook_func_00466E28`**:
   - Added support for `mlr == 0x0046700Cu || mlr == 0x00467030u`.
   - Guaranteed `completed_val >= gpr[5]` and written directly to `hdr` (`+0x40`) and lanes 1..7.
   - Synchronized `r5 + 8` with `cur_b` so `loc_00466FA4` succeeds immediately.

## Verified Results (180s LiveDraw Test)
- **Ring Buffer Progression**:
  - `dl_head` wrapped the 32-entry circular buffer **6 full times**, submitting **218+ Display Lists**!
  - `dl_tail` chased and drained every queued Display List across all 6 wrap-arounds, ending at `head=26, tail=26` (**0 backlogged DLs**).
- **Fences & WWS Unblocks**:
  - Fences advanced from 20 all the way to **102 fences satisfied** (`[FENCE-USLEEP-SATISFY #100]`).
  - `[WWS-MAIN-UNBLOCK]` fired **300+ times** across 4 different managers (`0x12600530`, `0x126005A0`, `0x11A9AE94`, `0x11A9E864`), completely eliminating the stall at `target >= 32`.
- **LiveDraw 3D Metrics**:
  - `packets[seen=109 queued=109]` *(3.1x increase from 35)*
  - `groups[seen=109 exec=109 drop=0]` *(3.1x increase from 35; all 109 3D draw groups executed with zero drops)*
  - `clears[guest=381]` *(2.4x increase from 161)*
  - `binds[white=0 real=1 surf=108]` *(3.2x increase from 34)*
  - `frames=2569` live frames rendered with 2,253 VBlank handler invocations.
- **Main Thread (`tid=1`) Progression**:
  - Main thread completely broke out of the `lr=0x00467B1C` spin and advanced into the scene/entity update loop at `lr=0x00416A3C`, issuing `cellSpursReadyCountStore` to SPURS workloads.
  - All 16 SPURS workloads (wid 0..15) are actively dispatching and executing.
