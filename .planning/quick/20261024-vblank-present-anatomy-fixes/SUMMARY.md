---
status: complete
date: 2026-10-24
---

# Quick Summary: 20261024-vblank-present-anatomy-fixes

## Tasks Completed
1. **Bug 1: Bounded Catch-Up Loop in `src/main.cpp`**:
   - Replaced stalled / hardcoded ticking with the bounded catch-up loop matching `boot_main.cpp:498-531`.
   - Bounded by 240 iterations per tick; calls `cellGcm_request_tick(); cellGcmTickFlip();` and sets `s_vblank_tick_event`.
   - Eliminated unconditional presents; only presents when `cellGcm_take_flip_pending_synced()` or before first flip (`cellGcm_flip_request_count() == 0`).
   - Integrated clock ticking directly into `rsx_vblank_thread`.
2. **Bug 2: Alternating Display Buffer ID Exposure and Presentation**:
   - Exposed `u32 cellGcm_current_display_buffer_id(void) { return s_current_display_buffer_id; }` in `cellGcmSys.c`.
   - Exemption added in `cellGcm_take_flip_pending_synced()` for GOW Ascension jumped display lists (`put == 0x00A80000u`).
   - Declared and passed `cellGcm_current_display_buffer_id()` to all `rsx_live_draw_present(...)` calls.
   - Verified that `presents` now alternate across both display buffers (e.g. `buf0=613, buf1=76`).
3. **Bug 3: Frame Anatomy & 0x1808 Sub-Stage Breakdown**:
   - Fixed `gow_anatomy_note_clear` and `gow_anatomy_note_draw` so pass information is recorded for every frame without off-by-one rejections.
   - Added trigger for the first frame with `>= 5` draw calls (`present=726` with 12 passes and 11 draws captured!).
   - Added 4 QPC sub-timers under `GOW_PROFILE=1` inside `sink_end_impl` (`0x1808`):
     - (a) vertex/index buffer fetch & upload: 0.56 ms (0.0%)
     - (b) texture lookup/decode/upload: 2369.55 ms (63.8%)
     - (c) shader decompile / PSO lookup: 1336.56 ms (36.0%)
     - (d) command list record & flush: 4.57 ms (0.1%)
4. **Submodule Patch & Single Build**:
   - Exported all changes to `patches/vblank-pacing.patch`.
   - Single build succeeded (`cmake --build ps3recomp/build && cmake --build build`).
   - 120s run completed cleanly.
