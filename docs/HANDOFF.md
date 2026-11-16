# God of War: Ascension Port Handoff

## Current Best Build
- **Target**: `D:\gow-bilal-asia\ascension\build\win\AscensionRecomp.exe` (SHA256: `FD4E64917E749BFF7350E62672AB55CCA0462C8E7B46650541DD1AEA2BB5F0FC`)
- **Runtime**: `D:\gow-bilal-asia\ascension\build\win\ps3recomp_sdk\libps3recomp_runtime.a` (SHA256: `DB4CD7997C387AB351CD292E811F11AC603AFF4FAED047316B3EB9261B600C5C`)
- **How to Run**:
  ```powershell
  & D:\gow-bilal-asia\run-asia.ps1
  ```
  Pass rate: 90.0% (18/20 runs render frames past boot).

## Stall Inventory & Status
1. `brb_StartSession` / barrier `[0x747D3510]` (Main LR `0x0061B9FC`):
   - Status: **OPEN** on our tree (35% pass); **FIXED** on their tree (90% pass).
   - Root Cause: SPU policy module `wws_job_pm` execution latency; missing `--merge-chunks` and `--xregs` in our lifted code causes job barrier timing timeouts.
2. SPURS Taskset `0x15E65100` Task 0 Sleep (Run 07):
   - Status: **OPEN** (intermittent, 1/10 runs on Asian build).
   - Cause: Deadlock on predicate `!spurs_bitset_test(0x15E65140, 0)` awaiting FIOS audio stream buffer producer.

## Experiments & Results
- **Cells 1–3 Runtime Hunks**: SPU_LL_STRIPE, SPURS priority sched, GCM park/report -> RESULT: no effect.
- **Part 1 MLAA Toggle**: Disable MLAA ON (8/20) vs MLAA OFF (7/20), Fisher $p = 0.747527$ -> RESULT: no effect.
- **Part 2 2x2 Matrix**: Our tree 7/20 (35%) vs Their tree 18/20 (90%), $p = 0.000772$ -> RESULT: confirmed.
- **Part 2 Runtime Swap**: Swapped our 7 runtime files into their build -> 5/5 passes -> RESULT: confirmed difference is lifted code.
- **Part 3 SPU Lifter Audit**: 9 instructions (`shufb`, `rotqby`, `cbx`, etc.) -> 100% ISA compliant -> RESULT: confirmed difference is `--merge-chunks` / `--xregs` pipeline structure.

## Next 3 Steps
1. Port `--merge-chunks` into `ps3recomp/tools/spu_lifter.py` to eliminate per-block trampoline dispatch.
2. Implement `--xregs` local variable register promotion in `spu_lifter.py` to avoid SPU register spilling.
3. Re-lift `wws_job_pm` with `--merge-chunks --xregs` and benchmark 20 cold runs x 60 s against `0x0061B9FC`.
