---
status: complete
date: 2026-10-28
---

# Complete Scoreboard and SPURS Analysis Summary

## Summary of Accomplishments
1. Task 1: Complete scoreboard for the 5 runs of patch 0053 re-parsed and verified. Disassembled and documented wait conditions for tid=1 at 0x004a0334, 0x00466740, and 0x00436718.
2. Task 2: Analyzed 0x02D68 redirect and SPU lifting semantics. Proved that bi $r0 at 0x02DB0 is a helper return and that jumping to 0x02D68 bypasses 0x02D50 (prologue loading SpursKernelContext 0x1E0), corrupting $r4. Replaced 0x00010203 drop with logged warning once per distinct EA.
3. Task 3: Compared cellSpurs.c:2041-2070 against upstream/gow-ascension-pc patches 0011/0026. Proved that priority preemption in spurs_poll_select is our addition, whereas patch 0011 never preempts active workloads.
4. Task 4: Executed next 5 cold runs x 150 s with thread table every 10s and 10 dumped PNG frames. Captured full scoreboard and new thread lifecycle.
