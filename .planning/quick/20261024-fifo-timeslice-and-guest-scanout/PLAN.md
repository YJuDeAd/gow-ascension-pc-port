# Quick Plan: FIFO Time-Slice, Guest Scanout, and Audio Diagnostics

## Overview
1. Make dedicated VBlank thread default on (`GOW_VBLANK_THREAD=0` opts out).
2. FIFO instrumentation: log >200ms calls with get/put/words/nops/why/last8; 5s stats (calls/s, words/s, max_ms); put distinct-method loop behind `GCM_METHOD_STATS`; add 8ms time slice every 4096 words.
3. Guest scanout: track buffers 0, 1, 2 on flip (non-zero % and 64-bit hash); PPM dump at 3 presents past 100 & 300; distinct `SET_SURFACE_COLOR_AOFFSET` vs display buffers; add `GOW_PRESENT_GUEST=1` via `rsx_live_draw_present_rgba`.
4. Audio measurement: 5s counts by tid for cellAudio calls; non-zero PCM check in port rings; lwcond `0x009FDD20` & `0x009FDD50` wait/signal and creator/signaler tracing.
5. Runs: 150s with `GOW_PRESENT_GUEST=0`, 150s with `GOW_PRESENT_GUEST=1`. Auto-delete PPMs, keep 3 newest logs, maintain free disk > 15 GB.
