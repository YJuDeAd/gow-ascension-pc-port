# Quick Task Summary: GCM Immediate Step-Over & SPURS Task Throttle

## Context & Objectives
- Prior telemetry showed the engine pausing on parked self-jumps waiting `GCM_PARK_GRACE_MS` (50 ms) before stepping over to ready Display Lists (`dl_head != dl_tail`), capping Display List draining cadence to ~20 segments/sec.
- `cellSpursCreateTask2` was printing 5 unthrottled log entries every frame to stderr.
- Objective:
  1. Bypass `GCM_PARK_GRACE_MS` when `dl_head != dl_tail` and `vm_read32(gcm_io2ea(s_fifo_getoff + 4)) == 0x00040064u` (`NV4097_SET_SEMAPHORE_OFFSET`), immediately stepping over in `ps3recomp/libs/video/cellGcmSys.c`.
  2. Throttle `cellSpursCreateTask2` logging and immediately call `gow_unblock_spurs_tasks_3a5()` in `src/compat/ascension_hle.cpp`.

## Changes Implemented
1. **`ps3recomp/libs/video/cellGcmSys.c`**:
   - Implemented zero-wait immediate step-over: when `tgt == s_fifo_getoff` and `put != s_fifo_getoff`, immediately checks `dl_h != dl_t && vm_read32(gcm_io2ea(s_fifo_getoff + 4)) == 0x00040064u`.
   - Advances `s_fifo_getoff += 4` and resets `park_off = 0` instantly without stalling.
   - Throttled step-over logging to `#1..#15` and every 200 occurrences.
   - Rebuilt `ps3recomp` runtime (`libps3recomp_runtime.a`).
2. **`src/compat/ascension_hle.cpp`**:
   - Throttled `cellSpursCreateTask2` log to first 10 calls or every 500 calls.
   - Called `gow_unblock_spurs_tasks_3a5()` directly on task creation, arming `vm_read32(0x00983168u, target)` so `func_003A5A94` never enters `sys_timer_usleep(30)` spinloops.
   - Rebuilt `gow_ascension.exe`.

## Verification & Telemetry (180s LiveDraw Test)
- **Immediate Step-Overs**: Executed continuously without 50 ms delays (`[cellGcmSys] FIFO immediate step-over ... #1..#15+`).
- **Display List Draining**: Drained 1,053 Display List entries down to **0 backlog** (`dl_head=0 dl_tail=0`) and `ctrl->get == ctrl->put == 0x00A80000`.
- **Live Frame Count**: Rendered **4,064 live frames** (a **4.5x increase** from 892 frames).
- **Flip Commands**: **498 total flips** (`tid=1: 448, tid=12: 50`).
- **New Systems & Memory**:
  - `cellVoice` (`CELL_SYSMODULE_VOICE`) loaded, initialized, and started.
  - New 12 MB main memory block mapped: `[cellGcmSys] MapMainMemory(ea=0x16300000, size=0xC00000)`.
- **Main Thread Status**: `tid=1` active in 60 FPS presentation loop at `lr=0x0046DA70` (calling `_cellGcmSetFlipCommand` directly).
- **Disk Guardrail**: Free space on `C:` verified at **24.24 GB free** (>15 GB requirement).
