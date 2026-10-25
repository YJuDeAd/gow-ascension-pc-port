---
status: in-progress
task: Decouple VBlank tick pacing, fix display buffer selection, enable non-blocking present, and measure rendering progression
date: 2026-10-24
---

# Plan: VBlank Pacing & Buffer Fixes for Faster Frame Rendering

## Objective
Decouple `cellGcm_request_tick()` / `cellGcmTickFlip()` from the RSX FIFO walker into a high-priority 60 Hz ticker thread.
Fix display buffer ID selection in `main.cpp` using `cellGcm_get_current_display_buffer()`.
Wire non-blocking present (`LD_PRESENT_INTERVAL=0`) via `config.toml` (`vsync = "off"`).
Measure and report all frame, flip, vblank, and timing metrics over a 240-second live run.

## Steps
1. Edit `config.toml`: set `vsync = "off"`.
2. Edit `tools/run_port.ps1`: pass `vsync = "off"` as `$env:LD_PRESENT_INTERVAL = "0"`.
3. Edit `ps3recomp/libs/video/cellGcmSys.c`: export `cellGcm_get_current_display_buffer(void)`.
4. Edit `ps3recomp/libs/video/rsx_live_draw.c`: use `LD_PRESENT_INTERVAL` in `g.swap->Present(..., sync_interval, 0)`.
5. Export `patches/vblank-pacing.patch` containing all `ps3recomp` changes without committing inside the submodule.
6. Rebuild `ps3recomp_runtime` (`cmake --build ps3recomp/build`).
7. Edit `src/main.cpp`:
   - Add `gow_vblank_tick_thread` (priority HIGHEST, 60 Hz QPC clock, request_tick/TickFlip catchup loop).
   - Update `rsx_vblank_thread` (drain and flip-gated present only, wait 1ms with kick_wait, 500ms fallback present).
   - Pass `cellGcm_get_current_display_buffer()` to `rsx_live_draw_present()`.
   - Expand `[TELEMETRY 5s]` with ticks/s, vblank handler/s, flip words/s, presents/s, presents per buf, rejected flips, and time spent in fifo drain vs present.
8. Rebuild root executable `build/gow_ascension.exe`.
9. Run `tools/run_port.ps1 -LiveDraw -TimeoutSeconds 240` with `LD_PRESENT_DBG=1`, `LD_FRAME_DUMP="build\frames"`, `LD_FRAME_DUMP_EVERY=300`.
10. If PPM frames dumped, convert up to 5 non-black frames to PNG with a stdlib script and delete PPMs.
11. Check if rejected flips / decoded flips > 10%; if so, test unsynced `cellGcm_take_flip_pending()`.
12. Report all required facts with log filename:line citations.
