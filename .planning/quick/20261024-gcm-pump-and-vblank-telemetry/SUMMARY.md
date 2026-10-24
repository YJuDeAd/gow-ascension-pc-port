---
status: complete
task: Add 5s telemetry counters and timestamped run logs
date: 2026-10-24
---

# Quick Task Summary: GCM Pump & VBlank Telemetry

## Accomplishments
1. Updated `tools/run_port.ps1` to automatically write timestamped logs to `build/logs/run_<yyyyMMdd_HHmmss>_{stdout,stderr}.log` and copy them to `build/run_port_{stdout,stderr}.log`.
2. Added 5s interval telemetry counters in `cellGcmSys.c`, `ppu_hle.cpp`, and `src/main.cpp`:
   - `ppu_gcm_pump` calls/sec per tid
   - `cellGcmTickVBlank` ticks/sec
   - VBlank handler invocations/sec
   - wall-clock elapsed seconds
   - `dl_head` / `dl_tail`
   - current fence reference (`ref`)
   - exact caller tracking with `file:line`
3. Successfully executed 60s run (`run_20261024_035814_stderr.log`).
