# Quick Task: Fix All Known Bugs (Bugs 1-8) & Parity Stabilization

**Directory:** `.planning/quick/20261103-fix-all-known-bugs`
**Branch:** `fix/all-known-bugs`
**Baseline:** `ac31f35` (restored `0198d37`) vs `wip-344d308`

---

### Task 0: Baseline 10x60s Benchmarks (`ac31f35` vs `wip-344d308`)
- Build and execute 10 cold x 60 s runs on `ac31f35`.
- Build and execute 10 cold x 60 s runs on `wip-344d308`.
- Record full stage distribution (final main lr, groups, presents, milestone).

### Task 1: BUG 1 - Host Depth Leak in `wws_job_pm`
- Instrument per-lane host_depth over time (every 5s), top 5 call sites, committed stack memory.
- Implement shadow stack of return PCs in `spu_drain_call` with longjmp unwind to depth d.
- Bounded depth < 50; remove `SPU_HOST_DEPTH_MAX=1000000` and `SPU_HOST_STACK_BIG`.
- Measure depth histograms and run 10 cold x 60s.

### Task 2: BUG 2 - List Stall-and-Notify (cmd 0x45, tag 0, parked mask 0x1)
- Verify STALL vs RESUME counts per run.
- Verify patch 0057 (per-context SN defer).
- Verify vector 0x00A2C -> 0x40 -> 0x80 -> 0x3FEC0 round trip (print r0, r1, srr0, and 128 gprs before/after).
- Ensure overrides are restricted strictly to image 2.

### Task 3: BUG 3 - Lifter Instruction Switches
- Read `ps3recomp/tools/ppu_lifter.py` `--insn-switches` handling and `ppu_switches.json`.
- Backup `src/gen` to an external directory outside git.
- Re-lift natively using the lifter's own `--insn-switches` on original clean ELF.
- Diff the 12 sites, verify DOF/MB/MLAA are off by default.

### Task 4: BUG 4 - Writable Guest Filesystems
- Map `/dev_hdd0`, `/dev_hdd1`, `/dev_flash`, `/app_home` to writable paths under `vfs/`.
- Ensure directory creation on write.
- Log and audit `[fs]` open FAIL entries.

### Task 5: BUG 5 - Unresolved Imports
- Resolve NID `0xA73BF47E` and `0xC765B995`.
- Determine caller LR and return value semantics.
- Implement correct HLE stubs.

### Task 6: BUG 6 - Boot Nondeterminism
- Log `[r28+0x64]` bit 29 and timeout word at `r30+0x2B50` across 10 runs.
- Output symbolized thread table and host stacks at 15s for 0-group hangs.

### Task 7: BUG 7 - Window & Diagnostics
- Confirm message pump thread responsiveness (`IsHungAppWindow` false over 120s).
- Verify TM symbol encoding in window title.
- Make watchdog opt-in (`GOW_WATCHDOG=1`) and silence noisy lock traces.

### Task 8: BUG 8 - After the Stall & Final Reporting
- Track engine loop metrics (next wait predicate/writer, audio ring %, cellVdec/cellSail).
- Compile before/after comparison tables and final report.
