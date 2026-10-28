# Quick Plan: Honest Frame Metrics, Audio Audit, and Scheduler Revert

## Tasks
1. Task 1: Measure frames honestly
   - Pixel-level RGB inspection of all dumped frames (PNG/PPM): nonblack pixel count, most common color %, file size.
   - Fix total_presents log parsing bug in smoke_helper.py.
   - Describe each dumped frame of Run 1 in one sentence.
   - Re-score runs 20261028_032146 to _033246.
2. Task 2: Audio data flow and zero-level audit
   - Inspect cellAudioAddData: calls/s, buffer pointers, float count, channels, non-zero float % in guest buffer vs mixer ring.
   - Check if guest writes silence or if mixer drops data (file:line).
   - Sound API call counts and calling threads.
3. Task 3: Investigate waits at 0x00622F8C and 0x0041F4F0
   - show_func.py on function containing 0x00622F8C and 0x0041F4F0.
   - Extract predicate address, compare value, writer function, call chain from game loop.
   - Dump 32 words around predicate.
4. Task 5: Intercept Necessity Audit (run before permanent revert of intercept in Task 4)
   - Add GOW_NO_PM_INTERCEPT toggle to spu_channels.c.
   - Test 3 x 120s with GOW_NO_PM_INTERCEPT=1.
   - Trace r4 at 0x02378, 0x02DA8, 0x02D68 and identify where clobbered.
5. Task 4: Revert priority preemption in spurs_poll_select
   - Revert cellSpurs.c:2041-2070 preemption to match upstream patch 0011 exactly.
   - Keep/drop 0x02D74 intercept based on Task 5 findings.
   - Build once and execute 5 cold runs x 150s with honest scoreboard.
