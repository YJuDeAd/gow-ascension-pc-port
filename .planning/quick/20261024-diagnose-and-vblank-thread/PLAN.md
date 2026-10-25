# Quick Task Plan: Diagnose Stall, VBlank Dedicated Thread & RSX Diagnostics

## Objective
Diagnose `tid=1` execution loop and `process_fifo` time, evaluate dedicated VBlank delivery thread (`GOW_VBLANK_THREAD=1`), and capture surface dumps to inspect rendering output.

## Tasks
1. **Disk Cleanup & Automation**:
   - Delete `build/frames/*.ppm` and keep only 3 newest runs in `build/logs`.
   - Update `tools/run_port.ps1` to automatically prune `build/logs` to keep 3 newest runs.
   - Verify free disk space > 15 GB.
2. **Task 2 Instrumentation (tid=1 inspection)**:
   - Analyze `func_00349110` (enclosing lr `0x003491DC`), loop condition, and call chain.
   - Instrument memory watch around the target address (print 64 words 1/s for 20s, log writers).
   - Telemetry: Print per-wid dispatched/started deltas and `cellSpursReadyCountStore` calls/s per tid every 5s.
3. **Task 3 Instrumentation (process_fifo breakdown)**:
   - Instrument `cellGcm_rsx_process_fifo`: time breakdown for parked-spin, method decode, `rsx_live_draw_method`, fence publish, and lock waits.
   - Record calling host threads and invocation frequencies.
   - Explain why `process_fifo` ms/s exceeds 1000 ms/s (parallel thread execution / multi-core accumulation).
4. **Task 4 (VBlank Delivery & Dedicated Thread)**:
   - Print ticks/s and handler invocations/s side-by-side in `[TELEMETRY 5s]`.
   - Implement `GOW_VBLANK_THREAD=1`: dedicated host thread with its own guest `ppu_context` calling the VBlank handler OPD via `g_ps3_guest_caller` once per tick.
   - Run 150s with `GOW_VBLANK_THREAD=0` (default) and 150s with `GOW_VBLANK_THREAD=1`.
   - Report comparative metrics (invocations/s, dl_head/tail, flips/s, first-handler wall time, crash status).
5. **Task 5 (Screen Surfaces & Frame Dumps)**:
   - Configure `LD_SURF_DUMP=build/surfs`, `LD_SURF_DUMP_FRAME=200`, `LD_FRAME_DUMP_EVERY=20`, `LD_FRAME_DUMP_FROM=150`, capped at 20 frames.
   - Convert PPMs to PNG and delete PPMs.
   - Report scanout fallback count and nonblack surfaces.
