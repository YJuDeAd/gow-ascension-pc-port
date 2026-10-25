# Quick Plan: 20261024-vblank-present-anatomy-fixes

## Goal
Fix 3 verified bugs in one batch, then perform ONE build and ONE 120s run with `GOW_PROFILE=1` and `LD_FRAME_ANATOMY=1` to verify.

## Tasks
1. **Bug 1 & Bug 2**:
   - In `ps3recomp/libs/video/cellGcmSys.c`:
     - Expose `u32 cellGcm_current_display_buffer_id(void) { return s_current_display_buffer_id; }`.
     - In `cellGcm_take_flip_pending_synced()`, allow `put == 0x00A80000u` so GOW display-list jumps do not falsely reject synced flips.
   - In `src/main.cpp`:
     - Declare `extern "C" uint32_t cellGcm_current_display_buffer_id(void);` and `extern "C" unsigned cellGcm_flip_request_count(void);` and `extern "C" int cellGcm_take_flip_pending_synced(void);`.
     - Implement bounded catch-up loop in `rsx_vblank_thread`:
       `int fired = 0; while ((long long)(now - next_tick) >= 0 && fired < 240) { cellGcm_request_tick(); cellGcmTickFlip(); if (s_vblank_tick_event) SetEvent(s_vblank_tick_event); if (rsx_ok) { cellGcm_rsx_process_fifo(); if (cellGcm_take_flip_pending_synced() || cellGcm_flip_request_count() == 0) { ... present ... } } next_tick += 16; fired++; }`
     - Only present when `cellGcm_take_flip_pending_synced()` returns non-zero, OR before first guest flip (`cellGcm_flip_request_count() == 0`).
     - Remove redundant `gow_vblank_tick_thread` thread creation in `main.cpp:1019` so ticks are driven exclusively by `rsx_vblank_thread`.
     - Pass `cellGcm_current_display_buffer_id()` to all `rsx_live_draw_present(...)` calls instead of hardcoded 0.

2. **Bug 3**:
   - In `ps3recomp/libs/video/rsx_live_draw.c`:
     - Fix `gow_anatomy_note_draw` and `gow_anatomy_note_clear` to record passes for every frame when `LD_FRAME_ANATOMY=1` is active (not gated by `g_ld_frames != 150 && g_ld_frames != 250`).
     - In `gow_anatomy_on_present(u32 frame)`, check if `frame == 150 || frame == 250 || (!s_first_5_draws_dumped && total_draws_in_frame >= 5)` to dump anatomy passes and reset `s_gow_anatomy_pass_count = 0`.
     - In `sink_end_impl` (`0x1808` / `on_begin_end`), add 4 QPC sub-timers under `GOW_PROFILE=1`:
       (a) vertex/index buffer fetch & upload
       (b) texture lookup/decode/upload
       (c) shader decompile / PSO lookup
       (d) command list recording / flush
     - Print the 4 sub-stage totals in `gow_profile_print()`.

3. **Submodule Patch & Single Build**:
   - Export all `ps3recomp` changes to `patches/vblank-pacing.patch`.
   - Build runtime and main executable: `cmake --build ps3recomp/build && cmake --build build`.

4. **120s Verification Run**:
   - Run executable for 120s with `GOW_PROFILE=1` and `LD_FRAME_ANATOMY=1`.
   - Verify and report:
     - `cellGcmTickVBlank` and `vblank_handler` rates (~60/s).
     - Flip words/s and presents/s, and confirm alternating display buffer IDs 0 and 1.
     - `0x1808` 4 sub-stage breakdown.
     - `[FRAME-ANATOMY]` for first frame with `>= 5` draws and frames 150/250.
