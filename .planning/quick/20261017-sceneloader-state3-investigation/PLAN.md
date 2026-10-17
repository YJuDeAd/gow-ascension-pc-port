# Quick Task: SceneLoader State 3 Investigation & IntroLoader Completion

## Problem Statement
`main` (`tid=1`) successfully reached the Main Engine Loop (`0x003CA688` -> `SceneLoader::Update` at `0x003C9FBC`), mounting 20 `.PSARC` archives and executing 46 Edge Zlib decompressions. It advanced from `state=1` -> `state=2` -> `state=3`, but is currently looping in `state=3`. `IntroLoaderThread` (`tid=12`) continues flipping/clearing awaiting scene load completion before yielding to the main render submission loop.

## Execution Steps

### 1. Disassemble and Map SceneLoader State Machine
- Disassemble `0x003CA688` (Main Engine Loop) and `0x003C9FBC` (`SceneLoader::Update`).
- Disassemble all state transition handlers in `0x003C9FBC` (`state=1`, `state=2`, `state=3`, `state=4`, `state=5`, etc.) and helper sub-functions (`0x003C9634`, `0x003C9DB8`, `0x003C9518`, `0x003C93D8`).
- Map exact triggers for `state 1 -> 2`, `state 2 -> 3`, and what `state == 3` requires to transition to `state == 4` (or `state == 0` / complete).
- Identify where `main` signals `IntroLoaderThread` (`tid=12`) to exit.

### 2. Check Edge Zlib, FIOS, and SPURS `Wws_Job` Progress
- Inspect the last 10 Edge Zlib inflate tasks, FIOS requests, and SPURS workload dispatches.
- Verify whether Edge Zlib inflations completed all requested packages or if any pending completion queue or event flag remained unset.
- Verify whether `Wws_Job` policy module or secondary SPU jobs are awaiting a progress counter or DMA tag.

### 3. Clean Up `brb_IsSpuWorkloadDone` (`0x00466B88`) Directly
- In `hook_sys_timer_usleep`, read `ctx->gpr[30]` (the true workload buffer pointer `r3 + 256`) rather than fixed address `0x009FE180`, ensuring `tid=13` (`brbDriver`) and `brb_StartSession` complete immediately without burning 10 seconds of timeout.

### 4. Implement State 3 Fix, Rebuild, and 45-Second Verification
- Implement the required HLE / synchronization / asset completion fix in `src/compat/ascension_hle.cpp`.
- Monitor disk space before and after build.
- Rebuild `build/gow_ascension.exe`.
- Execute `.\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 45`.
- Analyze logs and report on draw packets, SPURS counts, and scene load completion.
