---
status: in-progress
task: Add 5s telemetry counters (ppu_gcm_pump/tid, vblank ticks/s, vblank handler/s, wall sec, dl_head/tail, ref) and timestamped logging
date: 2026-10-24
---

# Plan: GCM Pump & VBlank 5s Telemetry and Timestamped Logging

## Objective
1. Modify `tools/run_port.ps1` to write each run to `build/logs/run_<yyyyMMdd_HHmmss>_{stdout,stderr}.log` (and mirror to `build/run_port_{stdout,stderr}.log`).
2. Add counters printed every 5 s in the engine:
   - calls/sec of `ppu_gcm_pump` per `tid`
   - `cellGcmTickVBlank` ticks/sec
   - VBlank handler invocations/sec
   - wall-clock seconds since start
   - `dl_head`/`dl_tail`
   - current fence ref
   - track exact callers of `ppu_gcm_pump` with `file:line`
3. Build, run for 60 seconds, and report ONLY:
   - those counters at 10/30/60 s
   - the log filename
   - which tids call `ppu_gcm_pump` (callers with file:line)
   No other analysis.
