# Quick Task: Fix Wws_Job Stall-and-Notify & cellSpursGetWorkloadInfo Bottlenecks

## Problem Statement
After `IntroLoaderThread` (`tid=12`) exits and all 5 WAD packages are loaded, `tid=1` (`main`) walks the 3D scene graph (`0x00385078`), but encounters two bottlenecks:
1. `cellSpursGetWorkloadInfo` (NID `0x4E153E3E`) is called over 9.45M times in a spinloop (`0x00466FF8..0x00467030`) because `readyCount` (`+0x23`) and `contention` (`+0x20`) are not zeroed or properly handled when idle.
2. SPU `Wws_Job` (`img=2`, `pm=0x0077CA80`) issues a DMA list with stall-and-notify (`cmd=0x45`), triggering interrupt `0x00A2C`, but `stallstat=0x1` and `parked=0x1` never clear because `MFC_RdListStallStat` (ch25) and `MFC_WrListStallAck` (ch26) are not acknowledged. Meanwhile, `tid=1` spins forever in `func_00466E28` at `lr=0x00467B1C`.

## Execution Steps

### 1. Inspect `img=2`'s Interrupt Handler (`0x00A2C`) and SPU Lifted Code
- Locate lifted SPU source for `img=2` (e.g. `src/gen/spu/` or `src/spu_gen/` or `src/gen/spu_*.cpp`).
- Inspect `0x00A2C` (interrupt handler) and `0x0228C` / `0x02358` / `0x02DA8`.
- Check channel 25 (`MFC_RdListStallStat`) and channel 26 (`MFC_WrListStallAck`) reads/writes and DMA list execution.
- Check why `[mfc] REJECTED malformed transfer: img=2 pc=0x0228C cmd=0x40 lsa=0x00B00 ea=0x00000001 size=256` occurs.

### 2. Inspect `func_00466E28` (Called from `func_00467B08` at `lr=0x00467B1C`)
- Check disassembly of `func_00466E28` using `python ./ps3recomp/tools/show_func.py --dir ./src/gen 0x00466E28` or inspect `src/gen/ppu_recomp_*.cpp`.
- Understand the exact completion flag / memory condition `tid=1` waits on.

### 3. Implement Targeted Fixes
- Implement `cellSpursGetWorkloadInfo` (`0x4E153E3E`) in `src/compat/ascension_hle.cpp` ensuring `CellSpursWorkloadInfo` (`+0x20` contention, `+0x23` readyCount) is properly set to 0 when idle and returns `CELL_OK`.
- Fix the `img=2` (`Wws_Job`) stall-and-notify / job execution pipeline so `MFC_WrListStallAck(0)` (`[mfc-list] RESUME`) fires, `loadJobState` advances, and job completion signals back to guest memory.

### 4. Verification
- Compile `gow_ascension.exe` (`cmake --build build`).
- Run `powershell -ExecutionPolicy Bypass -File .\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 60`.
- Verify:
  (a) `cellSpursGetWorkloadInfo` spinloop is resolved.
  (b) `[mfc-list] RESUME` fires and `tid=1` advances past `lr=0x00467B1C`.
  (c) NV4097 draw calls (`0x1808` / `0x1814`) and active `[live-draw]` packets appear.
