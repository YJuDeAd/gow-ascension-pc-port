# Quick Task Plan: GCM Immediate Display List Step-Over & SPURS Task Throttle

## Objective
Eliminate the two primary performance bottlenecks throttling Display List draining and frame rates:
1. Remove `GCM_PARK_GRACE_MS` 50 ms delay for ready Display Lists in `ps3recomp/libs/video/cellGcmSys.c`.
2. Throttle `cellSpursCreateTask2` logging and call `gow_unblock_spurs_tasks_3a5()` directly upon task creation in `src/compat/ascension_hle.cpp`.

## Tasks

### Task 1: Immediate Display List Step-Over in `ps3recomp/libs/video/cellGcmSys.c`
- **Location**: `ps3recomp/libs/video/cellGcmSys.c` (lines ~1817-1855)
- **Change**: When `tgt == s_fifo_getoff` and `put != s_fifo_getoff`:
  - Check immediately if `vm_read32(0x009711D4u) != vm_read32(0x009711D8u)` (`dl_head != dl_tail`) and `s_fifo_getoff + 4 < GCM_MAX_IO_SIZE`.
  - If `vm_read32(gcm_io2ea(s_fifo_getoff + 4)) == 0x00040064u` (`NV4097_SET_SEMAPHORE_OFFSET`), immediately step over (`s_fifo_getoff += 4; park_off = 0; continue;`).
  - Throttle log output for step-over.
- **Verification**: `cmake --build ps3recomp/build`

### Task 2: Throttle `cellSpursCreateTask2` & Instant Unblock in `src/compat/ascension_hle.cpp`
- **Location**: `src/compat/ascension_hle.cpp`
- **Change**:
  - Throttle `cellSpursCreateTask2` log to first 10 calls or every 500 calls.
  - Call `gow_unblock_spurs_tasks_3a5()` inside `cellSpursCreateTask2` on `tid=1`.
- **Verification**: `cmake --build build --target gow_ascension`

### Task 3: 180-Second LiveDraw Verification & Telemetry Audit
- **Command**: `powershell -ExecutionPolicy Bypass -File .\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 180`
- **Verify**:
  - Display Lists drained at high cadence without 50 ms pause.
  - 3D draw groups and flips executed.
  - Check if new shaders/textures or UI elements appear.
  - Watchdog position of `tid=1`.
