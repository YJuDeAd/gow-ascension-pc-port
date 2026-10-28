# Quick Plan: Complete Scoreboard and SPURS Analysis

## Tasks
1. Task 1: Complete scoreboard for the 5 runs of patch 0053 (parse existing logs).
   - Metrics: first_draw_s, groups_exec, max_dl_head, presents, flips/s, nonblack frames, audio_adds, audio peak, wid 9 readyCount timing, pm_entries per wid 0-14.
   - Thread states: last 5 HLE calls and lr for tid=1 and all created threads.
   - Disassembly / analysis of wait conditions for tid=1 at 0x004a0334, 0x00466740, and 0x00436718.
2. Task 2: Analysis of wws_job_pm 0x02D68 redirect and lifting semantics.
   - Inspect instructions and C lines for 0x02370..0x023A8, 0x02D50..0x02D78, 0x02DA0..0x02DB0.
   - Verify if branch to 0x02D68 at 0x02DA8 (bi $r0 with $r0=0x2D68) is a function return or indirect jump.
   - Trace callers reaching 0x02D68 with _tg==0.
   - Replace 0x00010203 drop with logged warning (once per distinct EA).
3. Task 3: Compare SPURS scheduler policy.
   - Compare cellSpurs.c:2041-2070 against upstream/gow-ascension-pc patches 0011/0026.
   - List differences in priority, readyCount, signals, contention, sig.
   - Identify preemption origin.
4. Task 4: Next 5 cold runs x 150 s.
   - Frame dumping every 60th frame (max 10 PNGs).
   - Thread table every 10 s.
   - Collect and format full scoreboard for all 5 runs.
