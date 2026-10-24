---
status: complete
date: 2026-10-21
description: Fix stale ring-buffer step-over in cellGcmSys.c and unblock func_00416A00 sub-queue wait in GoW Ascension
---

# Fix Stale Ring-Buffer Step-Over & Unblock func_00416A00 Sub-Queues

## Objective
1. Prevent stale ring-buffer step-over when `dl_head == dl_tail` and prevent dummy sentinel `0x00A80000` from ever corrupting `s_fifo_getoff` in `ps3recomp/libs/video/cellGcmSys.c`.
2. Unblock `func_00416A00` / `func_00416328` (`lr = 0x00416A3C`) in `src/compat/ascension_hle.cpp` by resolving type 2 SPU dependency wait entries and sub-queue dependency counters.

## Tasks
1. Update `ps3recomp/libs/video/cellGcmSys.c`:
   - In `gcm_fifo_resync_why`, suppress resync when `put == 0x00A80000u`.
   - In parked self-jump step-over, only step over when `dl_head != dl_tail` (`vm_read32(0x009711D4u) != vm_read32(0x009711D8u)`).
   - Rebuild `ps3recomp` runtime.
2. Update `src/compat/ascension_hle.cpp`:
   - Implement `gow_unblock_416_subqueues(uint32_t master_ea)`.
   - Implement `hook_func_00416328` and `hook_func_00416A00` and register them.
   - Implement `hle_cellSpursReadyCountStore` and register on `0xF843818Du`.
   - In `hook_sys_timer_usleep`, check `mlr == 0x00416A3Cu || (mlr >= 0x00416328u && mlr <= 0x00416B50u)` and unblock subqueues and clear spinning sub-queue counters after 5 ticks.
   - Rebuild `gow_ascension.exe`.
3. Run 180s verification test with `tools/run_port.ps1 -LiveDraw -TimeoutSeconds 180`.
