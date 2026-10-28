# Quick Plan: SPURS Lane Retention, Idle Path, WID Isolation, Intercept Root Cause & Boot Determinism

## Objectives
1. Task 1: Audit whether workloads 0-3 ever release lanes 0-5.
   - For each lane 0-5, per second for 60s: current wid, time assigned, job-loop iterations, cellSpursModulePoll (0x9D0) calls & returned wid, sampled PC histogram, DMA tag/size of last 5 MFC commands.
   - Identify spinning loops (0x2390-0x239C, 0x2160, 0x1950) and what they wait on (LS address, expected value, writer).
2. Task 2: Idle path analysis.
   - Inspect lifted C and SPU instructions for 0x02160, 0x01950, 0x02390, and storeJobNum < 0 handling.
   - Trace wake-up mechanisms (signal, event flag, mailbox, reservation, readyCount) and determine what runtime delivers vs drops.
3. Task 3: Diagnostic isolation (GOW_DIAG_SKIP_WIDS).
   - Implement GOW_DIAG_SKIP_WIDS in spurs_poll_select and kernel selector.
   - Run 3 cold x 120s with GOW_DIAG_SKIP_WIDS=0-7.
   - Run 2 cold x 120s with GOW_DIAG_SKIP_WIDS=0-3.
   - Report pm_entries for wids 8-14, tid=1 state, first_draw_s, groups, dl_head, new behavior.
4. Task 4: Root cause of $r2=0 at 0x02D70.
   - Print $r2/$r4 at 0x02D50 and 0x02378.
   - Trace why $r2=0 at 0x02D70 without intercept (check spu_img_restore, trampoline, spu_drain_call, or lifting bugs).
   - Fix at source if found, verify with GOW_NO_PM_INTERCEPT=1 for 3 x 120s.
5. Task 5: Boot determinism across 10 cold runs.
   - Track 0x0046700C vs registration stall divergence (timeline, threads, FIOS requests).
6. Report: One table per task + scoreboard + honest render workload verdict.
