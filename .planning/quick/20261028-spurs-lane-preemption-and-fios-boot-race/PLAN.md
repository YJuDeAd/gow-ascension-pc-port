# Quick Plan: SPURS Lane Preemption & FIOS Boot Race Fix

## Context & Objectives
1. **Task 1: What wakes an idle lane on real hardware**
   - Query Graphify and inspect `ps3recomp` (`spu_channels.c`, `spurs_policy.c`, `spu_workload.c`, `cellSpurs.c`) for writes to LS 0x12A0/0x12F0 and SPU interrupt (event 0x00A2C / vector 0x0040).
   - Inspect upstream / reference patches 0026, 0037, 0044, 0041 for "workload ready / preempt" signals and identify missing hunks.
2. **Task 2: Implement lane preemption / idle wake-up**
   - When readyCount/signal for a higher-priority workload becomes non-zero, trigger preemption / interrupt so idle lane exits wws_job_pm idle loop or invokes spursModulePoll.
   - Export to patches/ and rebuild. Verify with 5 cold x 150s runs without GOW_DIAG_SKIP_WIDS.
3. **Task 3: Boot race at func_0061B918 (0x0061BA44-0x0061BA94)**
   - Analyze loop `vm_read32(r10+0x24) == vm_read32(r9+0x10)` and timeout in r29.
   - Fix root cause in runtime (FIOS completion latency, sleep granularity, event signaling).
   - Verify with 10 cold x 120s: count passing registration and tid=1 LR.
4. **Task 4: The next wait**
   - Trace tid=1 after registration and wid 9 wait: 5s LR history over 60s, wait predicate (address, compare, writer) at 0x00467030, threads created.
   - Dump every 60th frame (max 10 PNGs) with honest nonblack pixel count and dominant colour.
5. **Task 5: Scoreboard & Deliverables**
   - Scoreboard with live-draw frame counter for total_presents, pm_entries_wid8_14, lanes_held_by_wid0_3.
   - Re-score 9 runs from last report + 5 new smoke rows.
   - One-line verdict on wids 4-14 lane assignment without GOW_DIAG_SKIP_WIDS.
