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
- **Main tree**: Executable `build/gow_ascension.exe`, runtime static library `ps3recomp/build/libps3recomp_runtime.a` (Clang/LLVM). Note: `ps3recomp_runtime.lib` is a dormant legacy MSVC artifact.
- **Asian tree**: Executable `AscensionRecomp.exe`, runtime `libps3recomp_runtime.a`.

## How to Regenerate SPU Code
Run `powershell tools/relift_spu.ps1` to extract and re-lift all SPU sources from `vfs/PS3_GAME/USRDIR/EBOOT.ELF` into `src/gen/spu/` with `--merge-chunks` and `--xregs` (takes ~158s). Then run `ninja -C build` to re-link.

## Experiments & Results
- **Part 1 Verification & Raw Log Audit**:
  - Part 3 Relift: 20/20 passed barrier `0x0061B9FC` (100.0%, main -> `0x0033A394` @ 15s -> `0x00467B1C`). 144 pres/s is the 144 Hz display swapchain rate during intro playback; intro video terminates deterministically at ~22.5s yielding 3227-3233 total frames.
  - Depth guard (`host_depth=2000`): In unmerged build, fires in BOTH passing and stalling runs (12-14 lane 5 halts in passing runs vs 6-13 in stalling runs); depth guard kill does NOT explain the stall. In merged build, `host_depth <= 2`, 0 halts.
  - Cells A, B, C Audit: `0/20` was an artifact of runner script bug (`status_3510 == -1` check); in reality, all runs in Cells A, B, C also left `0x0061B9FC` at 15s. The runner hashed dormant `.lib` (5D09F175) instead of real `.a` (48AE715E vs 7631D612).
- **Part 2 Next Stall (`0x00467B1C`)**: Workload admission / title menu readiness to be resolved.

## Next 3 Steps
1. Land Part 3 commit and close SPU lifter adoption milestone.
2. Verify RSX shader pipeline and PSO disk cache for Chapter 1 (Prison of the Damned).
3. Validate user input mapping and controller events past title menu into gameplay.
