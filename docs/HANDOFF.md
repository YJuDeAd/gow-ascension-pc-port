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
   - Status: **OPEN** on our tree (35% pass); **RESOLVED** on Asian build (100% pass, 20/20).
   - Telemetry: Main thread (tid=1) spins at LR `0x0061B9FC` awaiting WWS SPU job completion flag; `[0x747D3510]` remains unwritten (`-1`), while `brbDriver` (tid=9) rests at LR `0x00468A40`.
   - Root Cause: SPU policy module `wws_job_pm` latency/structure (our lifter omits `--merge-chunks` and `--xregs`).
2. Run 07 Abort on Asian Tree:
   - Status: **RETRACTED ROOT CAUSE**. The earlier claim of Task 0 sleeping at `cellSpurs.c:1142` on `!spurs_bitset_test(0x15E65140,0)` was untraceable (`dbg_Asia_Run_07.txt.out` is 0 bytes; path does not exist).

## Binary Naming Convention
- **Main tree**: Executable `build/gow_ascension.exe`, runtime `build/ps3recomp_runtime.lib` (MSVC) or `libps3recomp_runtime.a` (MinGW). Target name is `gow_ascension`.
- **Asian tree**: Executable `AscensionRecomp.exe`, runtime `libps3recomp_runtime.a`.

## Experiments & Results
- **Cells 1–3 Runtime Hunks**: SPU_LL_STRIPE, SPURS priority sched, GCM park/report -> RESULT: no effect.
- **Part 1 MLAA Toggle**: Disable MLAA ON (8/20) vs MLAA OFF (7/20), Fisher p=0.748 -> RESULT: no effect.
- **Part 2 2x2 Matrix**:
  - Cell 1 (Our rtm + Our SPU): 7/20 (35%) pass
  - Cell 2 (Their rtm + Their SPU): 18/20 (90%) pass (20/20 in baseline)
  - Cell 3 (Their rtm + Our SPU): 0/20 runnable (layout/symbol incompatibility: base 0x0 vs 0xA00)
  - Cell 4 (Our rtm + Their SPU): 20/20 (100%) pass (20 cold x 60s, MLAA off) -> RESULT: confirmed.
- **wws_job_pm Generation**: Ours omits `--merge-chunks` and `--xregs`; theirs merges blocks and promotes SPU registers to C locals (`--xregs hya`).
- **Part 1 Asian Baseline**: 20 cold x 90s runs -> 20/20 alive (100%), 0 crashes, 0 stalls -> RESULT: confirmed.

## Next 3 Steps
1. Execute Part 3: Create minimal Asian-support layer diff on `asia-support` branch.
2. Verify BCAS25016 title-ID allowlist, MLAA default, DXC documentation, mod entry, README note.
3. Answer the 3 concluding questions (90s survival, stall carrier, remaining steps for playable level 1).
