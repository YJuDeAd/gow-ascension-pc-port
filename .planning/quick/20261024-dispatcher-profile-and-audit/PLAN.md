# Quick Plan: Dispatcher Profile, Frame Anatomy, Audio Stall, and Other Repo Audit

## Objective
Profile the RSX live draw dispatcher with an accurate 64-word 8ms timeslice, audit frame anatomy around presents 150 & 250, analyze FIOS boot timeline, diagnose the audio stream parsing thread stall, perform a comprehensive read-only comparison with `gow-ascension-pc`, and evaluate upstream `ps3recomp` on a test branch.

## Tasks
1. **Dispatcher Profile & 64-word Timeslice (Task 1)**:
   - In `ps3recomp/libs/video/cellGcmSys.c`, update the timeslice check interval from 4096 words to 64 words.
   - In `ps3recomp/libs/video/rsx_live_draw.c`, implement `GOW_PROFILE=1`: track per-NV4097 method call count, total time, max time; PSO creation count & time; GPU fence wait time. Export/print top 10 by total and max every 10s.
   - Add `-Quiet` parameter to `tools/run_port.ps1` to un-set verbose debug env vars (`GCM_RECDBG`, `GCM_REFLOG`, `LD_TRACE_PSO`, `LD_ALIAS_DBG`).
   - Run 150 s with verbose flags, and 150 s with `-Quiet`. Record metrics (flips/s, max_call_ms, log MB/min, top 10 tables).
2. **Frame Anatomy (Task 2)**:
   - Implement `LD_FRAME_ANATOMY=1` around present 150 and 250: dump ordered pass list, color surface offset & size, clear/draw counts, shader hashes, bound textures, pass activity. Compute whole-run average draws per frame.
3. **Boot State & FIOS Timeline (Task 3)**:
   - Extract FIOS archive open and read timeline from log, identify last read timestamp, determine if game is in main loop vs loading.
4. **Audio Stall Diagnosis (Task 4)**:
   - Disassemble/inspect `func_00615BDC` and `func_00615CA8`.
   - Identify loop condition of `snd_stream_parsing_thread` (predicate address, wait condition, timeout, signaling function).
   - Log 32 words around the predicate address once per second for 20 s.
5. **Read-only Audit of `gow-ascension-pc` (Task 5)**:
   - Inspect `C:\IfItWorksItWorks\Project\gow-ascension-pc` for license, ps3kit diff, main loop / threading, launcher env vars, skip-intro patch, SPU job handling, and HLE handling (`sysPrxForUser`, `lwcond`, `cellSail`, `cellAudio`).
   - Compile comparative analysis table.
6. **Submodule Upstream Update Check (Task 6)**:
   - In `ps3recomp`, create a test branch, fetch upstream, calculate commits behind.
   - Test applying `patches/vblank-pacing.patch`, list conflicts, verify build status.
