# Quick Task: Honest SPURS Measurement, Fake Opt-In & Yield Path Audit

## Objective
1. Gate all synthetic completion writes behind `GOW_FAKE_DONE=1` (default off) and tag with `[FAKE-USED]`.
2. Measure 20 cold x 90s runs with defaults only (no fakes).
3. Audit lr progression, GPU FIFO status, RsxSync (wid 18) and RenJobSet (wid 10).
4. Audit cooperative SPU yield path and workload preemption.

## Tasks
- [x] Task 1: Implement `GOW_FAKE_DONE` gating and document WWS-FAST-DONE.
- [ ] Task 2: Execute 20 cold x 90s verification runs with telemetry logging.
- [ ] Task 3: Analyze main thread lr history, GPU FIFO get/put, and draw state.
- [ ] Task 4: Audit SPU cooperative yield and scheduler preemption.
- [ ] Report: Compile full telemetry tables and conclusions.
