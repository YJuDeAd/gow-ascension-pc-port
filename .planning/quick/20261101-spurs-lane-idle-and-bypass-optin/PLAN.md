# Quick Task: SPU Lane Idle Protocol & 0x00467B1C Bypass Opt-In

## Objective
1. Make 0x00467B1C bypass and cellSpursGetWorkloadInfo generalized spin guards strictly opt-in (`GOW_7B1C_BYPASS=1`, default off). Label and exclude runs using bypass from stage statistics.
2. Investigate the `0x02F7C` loop in `wws_job_pm` (SPU assembly and lifted C for `0x02F40-0x02FB0`): cache line, values, PUTLLC failure causes, exit conditions. Fix per-lane atomic counters and EA line tracking.
3. Investigate the idle protocol in `wws_job_pm`: analyze `SPU_WrEventMask` and `SPU_RdEventStat` (blocking vs immediate return, why `mask=0x2`, file:line).
4. Implement the smallest change so an idle lane sleeps until the event it waits for (no fake timer completion), rebuild, and benchmark 20 cold runs x 60s without bypass.
5. Probe post-bypass downstream behavior with `GOW_7B1C_BYPASS=1` (PPU lr history, why live_draws=0 with put=get=0x00A80000, next wait predicate).

## Verification Gates
- Task 0: With no env vars, binary runs with NO bypass triggered (`GOW_7B1C_BYPASS=1` required).
- Task 1: Disassemble `0x02F40..0x02FB0` and callers, per-lane atomic stats working for all 6 lanes.
- Task 2: Exact file:line and mechanism for `SPU_WrEventMask` / `SPU_RdEventStat` idle sleep.
- Task 3: Idle lanes block on host thread condition/event instead of 100% CPU spinning. 20 cold x 60s runs measured.
- Task 4: Diagnostic trace of post-0x00467B1C main thread execution.
