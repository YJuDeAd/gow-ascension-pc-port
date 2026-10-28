# Quick Plan: FIOS Real Completion & SPURS Priority Preemption

## Context & Objectives
1. **Task 1: Revert the fake completion**
   - Revert the `write_be32(r10 + 0x24u, sub);` in `ps3recomp/runtime/syscalls/sys_timer.c:119-146`, keeping only measurement/logging.
   - Run 10 cold x 120s runs with `tools/smoke.ps1`, record whether wids 9-13 registered and tid=1's lr.
2. **Task 2: Why the FIOS wait at 0x0061BA44 is late**
   - Identify who writes `completed` (`r10+0x24`), wall time submit vs complete, FIOS request, thread executing it, blocked state / lr in between.
   - Fix the root delay (missed signal, usleep granularity, cache lock, SPU Edge zlib completion).
   - Show evidence before and after.
3. **Task 3: Priority direction and which workloads hold lanes**
   - Print per-lane priority byte (`wkl_info_off(w)+0x18+spu`) for wids 0-14.
   - Document whether lower numbers mean higher priority in real SPURS with citations from `SPURS_SCHEDULING.md` and code.
   - List which workloads are idle in `wws_job_pm` idle loop vs ready.
4. **Task 4: Preempt wids 0-3, not only wid>=4**
   - Revert the `cur_wid >= 4` gate.
   - Analyze `wws_job_pm` (0x02358/0x02360/0x02390/0x01950) accepted wake events (event mask, signal notification, mailbox, lock-line lost).
   - Raise the exact event expected at handler `0x00A2C` (using patches 0026/0041/0044 as reference).
   - Verify with 5 cold x 150s runs: pm_entries wids 0-14, lanes held per wid at 60s, tid=1 lr, wid 9 ready over time, first_draw_s, groups, dl_head, nonblack count.
5. **Task 5: Scoreboard**
   - Include `pm_entries_wid8_14` and `lanes_held_by_wid0_3` columns, watchdog real thread names.
   - Final verdict: do wids 8-14 get a lane without `GOW_DIAG_SKIP_WIDS`.
