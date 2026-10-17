# Plan 04-03: FIOS Archive Continuation, cellGcm Hardware Init & Milestone 1 Bring-Up — Summary

**Execution Date:** 2026-10-17  
**Status:** Completed and Verified  

## Accomplishments
1. **Edge Zlib Decompression & Spinlock Resolution:**
   - Diagnosed the spinlock at `0x004803B4` in `func_0047F984` using `ps3recomp/tools/show_func.py`.
   - Identified that `EdgeZlibDecompressor` checks `clz(vm_read32(slot + 0x28)) == 32`. In `ascension_hle.cpp`, the completion word was previously targeted with a single-byte write (`vm_write8`) at an odd offset `0x15E66CAD`, leaving the 32-bit big-endian value non-zero.
   - Updated `_cellSpursLFQueuePushBody` to clear `status_ea = user_ea & ~3u` via `vm_write32(status_ea, 0)`.
   - Built a slice-aware in-memory stream cache in `_cellSpursLFQueuePushBody` that inflates compressed streams once and extracts subsequent 64-byte chunks (`skipBegin` / `skipEnd`) via `memcpy`, speeding up hundreds of consecutive slice reads from tens of seconds to < 1 ms.

2. **Full FIOS Archive Streaming:**
   - Over 34 package table blocks in `GAME.PSARC` decompressed cleanly via host-side RFC 1951 inflater `gow_inflate.cpp`.
   - Verified 0 memory corruption, 0 in-place buffer overlap faults, and 0 hangs.

3. **Audio Subsystem Initialization:**
   - Executed through `cellSysmoduleLoadModule(CELL_SYSMODULE_AUDIO)`.
   - Initialized hardware audio mixing: `cellAudioInit()`, `cellAudioPortOpen(8 ch)`, and `cellAudioPortStart(port=0)`.

4. **Graphics Hardware Initialization (`cellGcm`):**
   - Cleanly reached and executed `_cellGcmInitBody` with `0x10000` command buffer size, `0x5200000` IO size, and `0x11000000` IO base address.
   - Configured and bound 11 tile regions (`index 0..10`) and 4 Zcull regions.
   - Successfully allocated triple-buffered 720p display buffers:
     - Display Buffer 0: `offset 0x0`, `1280x720`, pitch 5120.
     - Display Buffer 1: `offset 0x384000`, `1280x720`, pitch 5120.
     - Display Buffer 2: `offset 0x708000`, `1280x720`, pitch 5120.
   - Registered parallel SPU rendering and simulation workloads (`Wws_Job/RenderJobs`, `Wws_Job/MeshProcJobs`, `Wws_Job/RenJobSet`, `Wws_Job/BrBDblBufWrkld`).

5. **Milestone Documentation:**
   - Authored `docs/fios-bringup.md` detailing the entire bring-up trajectory, memory maps, and stability metrics.

## Verification
- Built `build/gow_ascension.exe` cleanly with 0 compiler and linker errors.
- Verified live execution with `run_port.ps1`: zero crashes, zero ACCESS_VIOLATION errors, and verified `VERIFY_PHASE4_TARGET_REACHED_OK`.
