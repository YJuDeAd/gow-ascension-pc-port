---
status: complete
date: 2026-10-21
description: Fixed GCM FIFO jump-park resync and func_00375A50 fence sync in GoW Ascension
---

# Summary: GCM FIFO Jump-Park Resync & Fence Sync Fix

## Root Cause Fixes Implemented
1. **GCM FIFO Parked Self-Jump Scan-Ahead & Sentinel Guard (`ps3recomp/libs/video/cellGcmSys.c`)**:
   - Implemented a 16-word scan-ahead (+4..+64) for non-zero valid commands (`(w_ahead >> 29) <= 2`) across NOP padding.
   - Guarded `gcm_fifo_resync_why("jump-park", ...)` against teleporting `s_fifo_getoff` to `put == 0x00A80000`, keeping `s_fifo_getoff` parked safely in `0x0028xxxx`.
2. **Fence Synchronization Hook (`src/compat/ascension_hle.cpp`)**:
   - Added and registered `hook_func_00375A50` on `0x00375A50u`.
   - Updated `hook_sys_timer_usleep` to pump GCM and satisfy pending `ctrl->ref` fences (`target_ref = fence_cnt + r31`) when `tid == 1` at `lr == 0x00375A78u || lr == 0x00375AB4u`.

## Verified Results (180s LiveDraw Test)
- **FIFO Progression**: Stepped over parked self-jumps 25 times up to `0x0029DFB4` / `0x0029E018`. Zero `jump-park` drops to `0x00A80000`.
- **Ring Buffer Progression**: `dl_head` advanced through 68+ Display Lists, wrapping the 32-entry circular buffer twice (0..30 -> 0..30 -> 0..6). `dl_tail` chased `dl_head` and drained all 68+ DLs completely (`dl_head=6, dl_tail=6`).
- **LiveDraw 3D Metrics**:
  - `packets[seen=35 queued=35]` (7x increase)
  - `groups[seen=35 exec=35 drop=0]` (7x increase, 35 full 3D groups executed)
  - `clears[guest=161]`
  - `binds[white=0 real=1 surf=34]` (34 surface binds)
  - `frames=3990` live frames rendered with 3,799 VBlank handler invocations.
- **Watchdog State**: `tid=1` reached `lr=0x00467B1C` (`func_00467B08` -> `func_00466E28` for buffer set 3).
