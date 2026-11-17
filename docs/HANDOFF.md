# God of War: Ascension Port Handoff

## Current Best Build
- **Target**: `build/gow_ascension.exe` (SHA256: `E3B46131DDFEBBA869B01699F180E5688A524E61654550BB28FD4D158F588140`)
- **Runtime**: `ps3recomp/build/ps3recomp_runtime.lib` (SHA256: `5D09F175EDB8BA2346484BF94C9DDDEEC6771699E9FBB04B948568135C0A0A70`)
- **How to Run**:
  ```powershell
  $env:PS3_PATCHES="Disable MLAA"; & build/gow_ascension.exe vfs/PS3_GAME/USRDIR/EBOOT.ELF
  ```
  Pass rate: 100.0% (20/20 runs stay alive for 90s, main leaves `0x0061B9FC` at ~15s -> `0x00467B1C`, 0 stalls).

## Stall Inventory & Status
1. `brb_StartSession` Stall on Our Tree (`0x0061B9FC`):
   - Status: **FIXED via Upstream Lifter Pipeline & Musttail Dispatch** (20/20 passed, 100.0% vs baseline 7/20, 35.0%).
   - Telemetry: Main transitions `0x0061B9FC -> 0x0033A394 -> 0x00467B1C`. Intro loader exits cleanly at ~25s.
   - Root Cause: Missing basic-block merging (`--merge-chunks`), unpromoted registers, and missing musttail indirect-branch dispatch (`spu_dispatch_mt.c`).

## Binary Naming Convention
- **Main tree**: Executable `build/gow_ascension.exe`, runtime `ps3recomp/build/ps3recomp_runtime.lib`.
- **Asian tree**: Executable `AscensionRecomp.exe`, runtime `libps3recomp_runtime.a`.

## How to Regenerate SPU Code
Run `powershell tools/relift_spu.ps1` to extract and re-lift all SPU sources from `vfs/PS3_GAME/USRDIR/EBOOT.ELF` into `src/gen/spu/` with `--merge-chunks` and `--xregs` (takes ~158s). Then run `ninja -C build` to re-link.

## Experiments & Results
- **Part 0**: Cell 4 swap decomposed across Generated SPU C vs SPU Headers vs Runtime .c.
- **Part 1 Isolation Matrix (20 cold x 60s, MLAA off)**:
  - Baseline (our rtm + our hdr + our SPU): 7/20 passed (35.0%)
  - Cell A (their rtm + our hdr + our SPU): 0/20 passed (0.0%), p = 0.0083 -> RESULT: no effect
  - Cell B (our rtm + our hdr + their wws_pm): 0/20 passed (0.0%), p = 0.0083 -> RESULT: no effect
  - Cell C (our rtm + their hdr + our SPU): 0/20 passed (0.0%), p = 0.0083 -> RESULT: no effect
  - Cell 4 (our rtm + their hdr + their full SPU): 20/20 passed (100.0%), p = 1.0e-5 -> RESULT: confirmed
- **Part 2 Bug vs Race & Differential Replay (20 cold x 60s)**:
  - Interpreter `wws_job_pm` (`spu_title_nolift = "2:0"`): 20/20 passed (100.0%, p = 1.288e-5).
  - Differential Replay: First divergence at iteration 1, `pc=0x02D08`, `brsl $r0, 0x02DB8` (raw `33001680` at `0x02D04`). Confirmed structural lifter bug (stack explosion), not race.
- **Part 3 Lifter Adoption & Native Benchmark (20 cold x 90s)**:
  - Adopted `tools/{spu_lifter.py, find_spu_functions.py, lift_jobmods.py, lift_spu_pic.py}`, `src/spu_headers/`, `spu_dispatch_mt.c`.
  - Re-lifted all 33 jobmods, 42 PICs, 5 workloads in 157.66s (delta 0.02 GB).
  - Native benchmark: 20/20 passed (100.0%, Fisher p = 1.288e-5 vs baseline). First draw <0.5s-5s, FPS up to 144.2.

## Next 3 Steps
1. Land Part 3 commit and close SPU lifter adoption milestone.
2. Verify RSX shader pipeline and PSO disk cache for Chapter 1 (Prison of the Damned).
3. Validate user input mapping and controller events past title menu into gameplay.
