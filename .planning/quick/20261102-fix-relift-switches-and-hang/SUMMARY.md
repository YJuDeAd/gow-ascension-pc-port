# Quick Task Summary: Fix Corrupted Instruction Switches and Post-Logo Hang

**Session Slug**: `20261102-fix-relift-switches-and-hang`
**Date**: 2026-11-02
**Branch**: `parity/reference-exact`
**SHA256**: `FF7769B1305C414D48B1FE3D65E05D42C51FC495B5284353B419B370B6E15CD9`

## Results
- **Task 1 (Fix Instruction Switches)**:
  - Disassembled all 12 switch sites across `EBOOT.ELF`. Found that Loop 3 (`func_00466E28`) had only single NOPs (`0x0046700C` and `0x00467030`) after `bl 0x6288DC`. The upstream patches at `0x00467010` and `0x00467034` had overwritten `lbz r0, 0x93(r1)` and `lbz r0, 0x90(r1)` and their `cmpwi` checks.
  - Added strict guardrail in `tools/relift.py` enforcing that yield patches only overwrite `0x60000000` (`nop`).
- **Task 2 (Eliminate Register Clobbering)**:
  - Discovered that `lv2_syscall` clobbers `ctx->gpr[3]` (writing 0), breaking register liveness in `func_00416A00` (`loc_00416AF8`).
  - Added post-processing in `tools/relift.py` preserving `gpr[3]` and `gpr[11]` across all `0x3FC` spin-yields, and cleanly injected non-clobbering yields into `func_00466E28` delay slots.
- **Task 3 (Forensic RVA Resolution & Hang Audit)**:
  - Resolved all BOOT thread watchdog RVAs to exact symbols in `build/gow_ascension.map`. Traced SPU polling in `wws_job_pm` (`0x010D0`, `0x02D38`, `0x02F7C`).
  - Audited `func_0033A0E0`, `func_00466678`, and `func_0061B918`.
- **Task 4 (Benchmark Verification 10 Cold x 45s)**:
  - Rebuilt executable (`FF7769B...`).
  - Verified benchmark suite: eliminated the 0-group regressions, with 8/10 runs rendering hundreds of draw groups (up to 842 groups) and reaching legal splash and Santa Monica Studio logo screens.
