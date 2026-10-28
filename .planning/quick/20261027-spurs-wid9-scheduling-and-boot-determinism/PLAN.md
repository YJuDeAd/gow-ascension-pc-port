# Quick Task Plan: SPURS Workload 9 Scheduling, Environment Matrix, Loop Exit Analysis & Boot Determinism

## Objective
Diagnose why SPURS workload 9 ("RenJobSet") is never started despite `readyCount=8`, evaluate the environment matrix (SPURS_MAXCONT, SPURS_PIN, SPURS_READY_LEVEL), analyze the exact loop exit conditions of `func_00466E28`, measure boot determinism across 5 runs of 120s, and clean up scratch files.

## Inviolable Rules
- LAW 1: Use Graphify first to locate symbols. Never grep repo-wide.
- LAW 2: Submodules (`./ps3recomp/`, `./twistedmetal/`) remain read-only reference or patched via clean patch files.
- LAW 3: Never hand-edit `src/gen/`. Use `show_func.py` for function disassemblies.
- LAW 4: Maintain free disk space > 15 GB on C: at all times.
- LAW 5: Deterministic Windows CLI commands and verification steps. Omit "Community Wisdom". Facts with `file:line` or `log:line`.

## Tasks

### Task 1: Why is wid 9 never started?
1. Instrument `ps3recomp/libs/spurs/cellSpurs.c` and `ps3recomp/runtime/ppu/ppu_loader.cpp`:
   - Add 1 Hz telemetry (t=1..60s) for:
     - SPU lanes 0-5: state, last selected wid, poll iterations/s.
     - Workloads 0-14: HLE readyCount, guest readyCount, current contention, min/max contention, per-lane priority (lanes 0-5), flags, pm_entries, last rejection reason.
     - Wid 9: cumulative rejection counts per reason.
   - Add exact writer detection for wid 9's readyCount (`0x7001AD09`):
     - `cellSpursReadyCountStore` / exchange logging.
     - Direct `vm_write8` logging with tid, lr, guest_fn, val, wall time.
2. Build and run smoke test (`tools/smoke.ps1 -Seconds 60 -Cache cold`).
3. Verify telemetry and identify who set wid 9's readyCount to 8 and why scheduler rejects wid 9.

### Task 2: Environment Matrix
Run 2 cold runs x 60s for each configuration:
- a) baseline
- b) SPURS_MAXCONT unset
- c) SPURS_PIN=0 PS3_PIN_PPU=0
- d) SPURS_READY_LEVEL=0
- e) SPURS_MAXCONT unset + SPURS_PIN=0 PS3_PIN_PPU=0
Report: pm_entries 0-14, tid=1 leaves func_00466E28, SPU halts, max dl_head.
Report logical core count and affinity masks.
Map SPURS_MAXCONT keys (3, 13, 15, 16, 17) to OUR wid names and assess fit.

### Task 3: Exact Loop Exit
Disassemble `func_00466E28` via `ps3recomp/tools/show_func.py --asm 0x00466E28`.
Document instructions of loop `0x00466FE8..0x00467010` and preceding code `0x00466EB0..0x00466FE8`.
Identify exact registers, offsets, condition checks, and writer functions.

### Task 4: Boot Determinism
Run 5 cold runs x 120s using `tools/smoke.ps1 -Seconds 120 -Cache cold`.
Categorize runs (reaches `0x0046700C` vs loader loop `0x0041F4F0/0x003C9900`).
Inspect FIOS request states, ring_sz, worker states, and thread timeline divergence point.

### Task 5: Hygiene
Move one-off scripts to `tools/scratch/`.
Append all runs to `docs/smoke_history.csv`.
