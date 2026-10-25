# Quick Plan: 20261025-ticker-anatomy-audit-upstream

## Objective
Execute 4 tasks with rigorous verification, 1 build per task group, no code changes in ps3recomp submodule on current branch, omitting Community Wisdom, and keeping disk space > 15 GB.

## Tasks
1. **Task 1: Restore the Dedicated Ticker**:
   - Re-enable/restore the dedicated ticker thread in `src/main.cpp` using `QueryPerformanceCounter` clock with bounded catch-up loop (no FIFO drain, no present, only `cellGcm_request_tick()` and `cellGcmTickFlip()`, and signaling `s_vblank_tick_event` for the dedicated vblank delivery thread).
   - In `rsx_vblank_thread`, keep `cellGcm_rsx_process_fifo()`, flip taking with `cellGcm_take_flip_pending_synced()`, and presenting `cellGcm_current_display_buffer_id()`.
   - Build once and run 100s to verify ticks >= 55/s in-engine.
2. **Task 2: Frame Anatomy Analysis & SPU/WID Telemetry**:
   - Save frame 726 pass list + next 3 frames with draws. For every sampled surface offset, audit draws/clears and list unwritten sampled surfaces.
   - Count `gcm_note_spu_put` calls/bytes and FIFO words from SPU DMA vs PPU-written ranges every 5s.
   - Count per-WID dispatched/started deltas and `cellSpursReadyCountStore` calls/s for 120s; identify stalling WIDs.
   - Inspect surface 0x01D90000 PNG from frame 200: dominant RGB color and % coverage.
3. **Task 3: Read-Only Inventory of `gow-ascension-pc`**:
   - Audit LICENSE and commit hash.
   - Categorize and audit 44 patch files (name, purpose, files touched, upstream commit/version, size).
   - Binary comparison: search addresses (0x008135D0, 0x0077CA80, 0x00376DE8, 0x009711D4, 0x808360), compare PARAM.SFO title IDs (BCAS25016 vs BCES01741).
   - Audit 21 default env vars; flag guest addresses and check presence in our EBOOT.
   - Correlate patches to current symptoms.
4. **Task 4: Selective Port on Branch `upstream-port`**:
   - In `ps3recomp` submodule, branch `upstream-port` from `origin/master` (`a6d76c8`).
   - Group-by-group port & 90s test pipeline:
     a) upstream-only baseline
     b) syscalls and audio
     c) spurs
     d) spu-runtime
     e) video
     f) host/env defaults
   - Create `THIRD_PARTY_NOTICES.md` and credit line.
