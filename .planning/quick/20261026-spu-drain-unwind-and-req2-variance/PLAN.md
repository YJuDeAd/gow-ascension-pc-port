# Quick Task: SPU Drain Unwind and Request 2 Variance Investigation

## Objective
1. Identify the repeating `spu_drain_call` frame in `wws_job_pm` using `drain_stack[64]` tracing up to depth 32.
2. Inspect `func_00466E28` (`lr=0x00467B1C`) and the `+0x42..0x4E` counters in lifted PPU/SPU code.
3. Implement clean C-return unwind for repeating `spu_drain_call` frames to bound `host_depth < 16` with 0 SPU halts.
4. Diagnose `req2` variance across runs and execute 3x 120s smoke tests recording to `docs/smoke_history.csv`.
