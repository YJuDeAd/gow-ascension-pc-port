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
1. `brb_StartSession` Stall on Our Tree:
   - Status: **OPEN** on our tree (35% pass); **RESOLVED** on Asian build (90% pass).
   - Telemetry: Main thread (tid=1) spins at LR `0x0061B9FC` awaiting WWS SPU job completion flag; `[0x747D3510]` remains unwritten (`-1`), while `brbDriver` (tid=9) rests at LR `0x00468A40`.
   - Root Cause: SPU policy module `wws_job_pm` latency/structure (our lifter omits `--merge-chunks` and `--xregs`).
2. Run 07 Abort on Asian Tree:
   - Status: **RETRACTED ROOT CAUSE**. The earlier claim of Task 0 sleeping at `cellSpurs.c:1142` on `!spurs_bitset_test(0x15E65140,0)` was untraceable (`dbg_Asia_Run_07.txt.out` is 0 bytes; path does not exist).
   - Real Status: Intermittent early launch crash (0 bytes stdout) on 1/10 runs.

## Binary Naming Convention
- **Main tree**: Executable `build/gow_ascension.exe`, runtime `build/ps3recomp_runtime.lib` (MSVC) or `libps3recomp_runtime.a` (MinGW). Target name is `gow_ascension`.
- **Asian tree**: Executable `AscensionRecomp.exe`, runtime `libps3recomp_runtime.a`.

## Experiments & Results
- **Cells 1–3 Runtime Hunks**: SPU_LL_STRIPE, SPURS priority sched, GCM park/report -> RESULT: no effect.
- **Part 1 MLAA Toggle**: Disable MLAA ON (8/20) vs MLAA OFF (7/20), Fisher p=0.748 -> RESULT: no effect.
- **Part 2 2x2 Matrix**: Our tree 7/20 (35%) vs Their tree 18/20 (90%), p=0.000772 -> RESULT: confirmed.
- **Part 2 Runtime Swap**: Swapped our 7 runtime files into their build -> 5/5 passes -> RESULT: confirmed lifted code.
- **Part 3 SPU Lifter Audit**: 9 instructions compliant -> difference is `--merge-chunks` / `--xregs`.
- **Part 0 Telemetry & Launcher**: Verified `run-asia.ps1` with pristine ELF; retracted Run 07 fake predicate.

## Next 3 Steps
1. Execute Part 1: 20 cold x 90s runs on Asian build to establish rigorous baseline.
2. Execute Part 2: Complete 20x60s runs for Cell 3 (their runtime + our SPU) and Cell 4 (our runtime + their SPU).
3. Execute Part 3: Package minimal Asian support layer on `asia-support` branch.
