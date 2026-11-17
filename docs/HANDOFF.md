# God of War: Ascension Port Handoff

## Current Best Build
- **Target**: `D:\gow-bilal-asia\ascension\build\win\AscensionRecomp.exe` (SHA256: `FD4E64917E749BFF7350E62672AB55CCA0462C8E7B46650541DD1AEA2BB5F0FC`)
- **Runtime**: `D:\gow-bilal-asia\ascension\build\win\ps3recomp_sdk\libps3recomp_runtime.a` (SHA256: `DB4CD7997C387AB351CD292E811F11AC603AFF4FAED047316B3EB9261B600C5C`)
- **How to Run**:
  ```powershell
  & D:\gow-bilal-asia\run-asia.ps1
  ```
  Pass rate: 100.0% (20/20 runs stay alive and render frames cleanly for 90s).

## Stall Inventory & Status
1. `brb_StartSession` Stall on Our Tree:
   - Status: **OPEN** on our tree (35% pass in baseline, 0% in isolated modules); **RESOLVED** with full lifter suite (100% pass, 20/20).
   - Telemetry: Main thread spins at LR `0x0061B9FC` awaiting SPU barrier completion; `[0x747D3510]` remains unwritten (`-1`).
   - Root Cause: Systemic SPU lifter pipeline latency across `wws_job_pm` and downstream jobmods (`--merge-chunks` / `--xregs`).

## Binary Naming Convention
- **Main tree**: Executable `build/gow_ascension.exe`, runtime `build/ps3recomp_runtime.lib` / `libps3recomp_runtime.a`.
- **Asian tree**: Executable `AscensionRecomp.exe`, runtime `libps3recomp_runtime.a`.

## Experiments & Results
- **Part 1 Isolation Matrix (20 cold x 60 s, MLAA off, metric "main leaves 0x0061B9FC before 60 s")**:
  - Baseline (our rtm + our hdr + our SPU): 7/20 passed (35.0%)
  - Cell A (their rtm + our hdr + our SPU): 0/20 passed (0.0%), Fisher p = 0.0083 vs baseline -> RESULT: no effect
  - Cell B (our rtm + our hdr + their wws_pm): 0/20 passed (0.0%), Fisher p = 0.0083 vs baseline -> RESULT: no effect
  - Cell C (our rtm + their hdr + our SPU): 0/20 passed (0.0%), Fisher p = 0.0083 vs baseline -> RESULT: no effect
  - Cell 4 (our rtm + their hdr + their FULL SPU suite): 20/20 passed (100.0%), Fisher p = 0.000010 vs baseline -> RESULT: confirmed
- **Isolation Finding**: Swapping runtime alone (Cell A), headers alone (Cell C), or `wws_job_pm` alone (Cell B) cannot resolve the stall. The stall is flipped ONLY by the entire SPU lifted pipeline (policy module + downstream job modules + pic fragments lifted with `--merge-chunks` and `--xregs`).

## Next 3 Steps
1. Execute Part 2: Force wws_job_pm (image 2) through SPU interpreter on our tree (`SPU_INTERP_UNLIFTED`); test defect vs race via differential replay.
2. Execute Part 3: Adopt their lifter pipeline into our tree with MIT notices and re-lift.
3. Verify CP4 (main menu) and CP5 (first level) progression.
