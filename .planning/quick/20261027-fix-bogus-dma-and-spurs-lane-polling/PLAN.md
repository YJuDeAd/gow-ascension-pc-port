# Quick Plan: Fix Bogus DMA on Lane 0 and Enable SPURS Lane Polling

## Context
In God of War: Ascension recompilation, Workload 9 ("RenJobSet") ready count (set to 8 by tid=1 at 55.3s via `cellSpursReadyCountSwap`) is never consumed because all 6 physical SPU lanes remain locked in `running pm` on workloads 0..3 with `poll_iter/s = 0`. Lane 0 is continuously issuing MFC GET requests with bogus EA `0x00010203` at PC `0x0228C` in `wws_job_pm`.

## Tasks
1. **Task 1: Trace Bogus DMA on Lane 0**:
   - Trace writes to MFC_EAL (ch18) with `0x00010203` (or any byte pattern).
   - Find SPU PC, instruction that produced register, function call chain / drain depth, and previous legitimate value.
   - Compare with first legitimate GET at pc `0x01BE8` (lsa `0x00D00`, ea `0x70029D00`).
   - Determine root cause: lifted semantic bug, uncompleted DMA LS store, or state lost at unwind.
2. **Task 2: Why Do Lanes Never Poll**:
   - Trace policy module main loop at `wws_job_pm` `0x02230..0x02D78` and find caller reaching `cellSpursModulePoll` / `spurs_poll_select`.
   - Measure per lane per second: job loop iterations, poll calls, return value, current wid, priority.
   - Determine why poll is skipped or returns 0x20.
3. **Task 3: Evidence-Based Minimal Fix**:
   - Implement the smallest change to fix bogus DMA at source and allow idle lanes to poll and pick ready workloads.
   - If in `ps3recomp`, export to `patches/`.
   - Run 5 cold runs x 120s (`GOW_FIOS_CACHE=off`), reporting all metrics.
4. **Task 4: Tidy Up**:
   - Remove unread `SPURS_PIN` and `PS3_PIN_PPU` from `ascension_defaults.cpp`.
   - Keep `SPURS_MAXCONT` if justified; report.
