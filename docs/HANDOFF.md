# God of War: Ascension Port Handoff

## Current Best Build
- **Target**: `build/gow_ascension.exe` (SHA256: `99A011083637B3069C37323C0119E2BDF4E1B9CBF3F990AB3186F718103CD7AA`)
- **Runtime**: `ps3recomp/build/ps3recomp_runtime.lib` (SHA256: `5D09F175EDB8BA2346484BF94C9DDDEEC6771699E9FBB04B948568135C0A0A70`)
- **How to Run**:
  ```powershell
  $env:SPU_NOLIFT="2:0"; $env:SPU_INTERP_UNLIFTED="1"; $env:PS3_PATCHES="Disable MLAA"; & build/gow_ascension.exe vfs/PS3_GAME/USRDIR/EBOOT.ELF
  ```
  Pass rate: 100.0% (20/20 runs leave `0x0061B9FC` at ~15s, `[0x747D3510]=0x00000000`, 0 stalls).

## Stall Inventory & Status
1. `brb_StartSession` Stall on Our Tree:
   - Status: **FIXED via Interpreter / Lifter Merge** (100% pass, 20/20; baseline lifted had 35% pass, 7/20).
   - Telemetry: Unmerged basic blocks recurse through trampolines until `host_depth=2000` at `0x02D08`, triggering `spu_halt(ctx)` (`spu_drain.c:537`).
   - Root Cause: Defect in our lifted `wws_job_pm` (lack of `--merge-chunks`), NOT a race condition.

## Binary Naming Convention
- **Main tree**: Executable `build/gow_ascension.exe`, runtime `build/ps3recomp_runtime.lib` / `libps3recomp_runtime.a`.
- **Asian tree**: Executable `AscensionRecomp.exe`, runtime `libps3recomp_runtime.a`.

## Experiments & Results
- **Part 1 Isolation Matrix (20 cold x 60 s, MLAA off)**:
  - Baseline (our rtm + our hdr + our SPU): 7/20 passed (35.0%)
  - Cell A (their rtm + our hdr + our SPU): 0/20 passed (0.0%), p = 0.0083 -> RESULT: no effect
  - Cell B (our rtm + our hdr + their wws_pm): 0/20 passed (0.0%), p = 0.0083 -> RESULT: no effect
  - Cell C (our rtm + their hdr + our SPU): 0/20 passed (0.0%), p = 0.0083 -> RESULT: no effect
  - Cell 4 (our rtm + their hdr + their full SPU): 20/20 passed (100.0%), p = 1.0e-5 -> RESULT: confirmed
- **Part 2 Bug vs Race & Differential Replay (20 cold x 60 s)**:
  - Interpreter `wws_job_pm` (`spu_title_nolift = "2:0"`): 20/20 passed (100.0%), Fisher exact p = 1.288e-5 vs baseline (7/20).
  - Differential Replay: First divergence at iteration 1, `pc=0x02D08`, instruction `brsl $r0, 0x02DB8` (raw `33001680` at `0x02D04`).
  - Divergent State: Our lifted code hits `SPU_HOST_DEPTH_MAX=2000` (`spu_drain.c:537`) -> `spu_halt(ctx)`, `halted=1`, `[0x747D3510]=0xFFFFFFFF`. Interpreter and `--merge-chunks` code maintain flat stack (`host_depth <= 1`), `halted=0`, draining queue (`[0x747D3510]=0x00000000`).
  - Verdict: **DEFECT (BUG)** in our lifter output due to missing `--merge-chunks`, NOT a race.

## Next 3 Steps
1. Execute Part 3: Import upstream SPU lifter pipeline (`spu_lifter.py`, `find_spu_functions.py`, `lift_jobmods.py`, `lift_spu_pic.py`, `--merge-chunks`, `--xregs`) with MIT notices in `THIRD_PARTY_NOTICES.md`.
2. Re-lift SPU sources from pristine EBOOT into scratch dir, report disk/time, rebuild runtime and exe.
3. Run 20 cold x 90s benchmark on full re-lifted pipeline to verify FPS and 100% pass rate.
