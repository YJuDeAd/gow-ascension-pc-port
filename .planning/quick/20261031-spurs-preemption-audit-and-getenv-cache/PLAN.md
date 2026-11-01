# Quick Task: SPURS Preemption Audit, Getenv Cache & 0x00467B1C Diagnosis

## Context
Diagnose and resolve the getenv lock contention and analyze the wait loop at `0x00467B1C` (func_00466E28).
Verify reference repo hygiene, eliminate getenv CRT lock contention in SPU and GCM hot paths, measure HEAD vs reverted baseline, and inspect the WWS job manager progress counters and workload state at `0x00467B1C`.

## Tasks
1. **Task 0: Reference Repo Hygiene**:
   - Inspect `..\gow-ascension-pc` status and commits.
   - Export patch to `docs/reference-notes/gow-ascension-pc-changes.patch`.
   - Provide command for user to reset `..\gow-ascension-pc` (user decision).
2. **Task 1: Fix getenv at Source**:
   - Cache `getenv("SPU_PCSAMPLE")` in `spu_channels.c` `mfc_for` so it runs once.
   - Cache other hot path getenvs (`SPU_ARGWATCH`, `SPURS_JOB_DUMP`, `SPURS_JOB_DESCDUMP`, `SPURS_PM_DUMP`, `FLIP_DBG`, `GCM_DRAINDBG`, `CELLMARK_BLINKDBG`, `GCM_DEFAULT_CTX_REPOINT`).
   - Remove getenv IAT hook from `upstream_compat.cpp` and `WATCHDOG_AT` override.
   - Verify Stage E does not reoccur.
3. **Task 2: Measure Current HEAD vs Reverted**:
   - Run 20 cold x 60 s runs on current HEAD (`dde3f83` + Task 1).
   - Run 20 cold x 60 s runs with `dde3f83` reverted on top of `a22fa3b` + Task 1.
   - Compare stage distributions and determine which configuration is better.
4. **Task 3: Diagnose 0x00467B1C Wait**:
   - Analyze `func_00466E28` assembly and 16-bit completion counters (`r3 + (r4<<4) + 0x40 + 2..0xE`).
   - Print the 32 words of the job manager structure once per second for 20 s and target count in r5.
   - Inspect workloads `wid=11` and `wid=18` (names, in_use, data pointer, ReadyCountStore).
   - Inspect SPU lanes running workloads, pm_entries per wid.
5. **Task 4: Comprehensive Report**:
   - Emit tables with log file names and stage distributions.
   - Conclude on best configuration and exact root cause of `0x00467B1C`.
