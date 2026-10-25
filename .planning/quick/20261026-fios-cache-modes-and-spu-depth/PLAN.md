# Quick Plan: FIOS Cache Modes, Scoreboard Fixes, Combined Test, SPU Depth Audit, and Repo Hygiene

## Objective
Implement FIOS cache mode controls (`GOW_FIOS_CACHE=off|tiny|full`), enhance smoke testing parser scoreboard, execute combined 120s test runs, analyze SPU depth and rejoin mechanics, and restore repository hygiene.

## Tasks
1. **Task 1: Fix the Scoreboard**
   - Update `tools/smoke_helper.py` to correctly parse `req1/req2/req3 rem0` timestamps, `intro_exit_s`, and `vblank_reg_s`.
   - Add `first_draw_s`, `max_dl_head`, `groups_executed`, and `tid=1`'s last 3 LR values.
   - Verify by re-parsing existing logs.
2. **Task 2: FIOS Cache Modes**
   - Add `GOW_FIOS_CACHE=off|tiny|full` (default `off`) to `ps3recomp/runtime/ppu/ppu_fs.cpp` / `src/compat/ascension_defaults.cpp`.
   - Run 2x 90s cold runs for each mode (`off`, `tiny`, `full`) on HEAD.
   - Record and report all 6 rows in `docs/smoke_history.csv`.
3. **Task 3: Combined Test**
   - Run 3x 120s with `GOW_FIOS_CACHE=off` and SPU unwind (`spu_restart_dispatch`).
   - Run 1x 120s with unwind reverted (`spu_halt` at depth 2000).
   - Report progression, `dl_head`, `groups`, draws/frame, nonblack frame dumps, audio, and thread states.
4. **Task 4: SPU Depth Analysis**
   - Document effective startup values of `SPU_REJOIN`, `SPU_REJOIN_CALL`, `SPU_HOST_DEPTH_MAX`, `SPU_INTERP_UNLIFTED` with `file:line`.
   - Analyze patches 0023 and 0037 for rejoin conditions and overlay image mapping.
   - Disassemble/inspect `wws_job_pm` call and return generated C at `0x02D04..0x02D08`.
   - Calculate host frame addition per job call per lane.
5. **Task 5: Repo Hygiene**
   - Move scratch scripts to `tools/scratch/` and update `.gitignore`.
   - Organize `patches/series/NNNN-*.patch` and document in `docs/PATCH_ORDER.md`.
   - Verify submodule commit pointer and ensure clean working tree.
