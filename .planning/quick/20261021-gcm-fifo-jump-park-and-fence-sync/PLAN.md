---
status: complete
date: 2026-10-21
description: Fix GCM FIFO jump-park resync and func_00375A50 fence sync in GoW Ascension
---

# Fix GCM FIFO Jump-Park Resync & func_00375A50 Fence Sync

## Objective
Prevent `s_fifo_getoff` from jumping to dummy put sentinel `0x00A80000` on parked self-jumps, scan ahead through NOP padding, and satisfy `func_00375A50` / `hook_sys_timer_usleep` reference fences so Display Lists continue processing past batch 14 and render continuously.

## Tasks
1. Update `ps3recomp/libs/video/cellGcmSys.c`:
   - Step over / scan ahead up to 16 words (+4..+64) for non-zero valid commands across NOP padding.
   - Guard `gcm_fifo_resync_why("jump-park", ...)` so it never jumps to `put == 0x00A80000` or invalid command memory. Leave parked at `tgt`.
   - Rebuild `ps3recomp` runtime library.
2. Update `src/compat/ascension_hle.cpp`:
   - Add `hook_func_00375A50` and register on `0x00375A50u`.
   - Handle `lr == 0x00375A78u` and `lr == 0x00375AB4u` in `hook_sys_timer_usleep`: pump GCM, check `ctrl->ref` against `target_ref = fence_cnt + (int32_t)r31`, and satisfy if lagging.
   - Rebuild `gow_ascension.exe`.
3. Run verification with `tools/run_port.ps1 -LiveDraw -TimeoutSeconds 180` and verify Display List and frame advancement.
