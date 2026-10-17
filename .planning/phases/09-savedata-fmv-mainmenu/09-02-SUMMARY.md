# 09-02 SUMMARY: `cellSail` / FMV Playback Completion & `IntroLoaderThread` Unblock

## Status: COMPLETE

## Accomplishments
1. **GCM FIFO Resync (`GCM_GET_EQ_PUT=1`) Integration**:
   - Configured `$env:GCM_GET_EQ_PUT = "1"` in `tools/run_port.ps1`.
   - Ensures `CellGcmControl->get` (+0x04) tracks `put` (+0x00) whenever FIFO flushes occur, unblocking wait loops that monitor GCM drain status.

2. **`IntroLoaderThread` Package Unpacking Pipeline**:
   - `IntroLoaderThread` (`tid=12`, OPD `0x008159B0` -> `0x0041F74C`) successfully mounts `SHELL.PSARC` (fd 5) and `INTROCIN.PSARC` (fd 6).
   - Decompresses 44+ Edge Zlib 64 KB chunks in sequence via host `gow_inflate` and signals completion via SPURS event flags (`0x15E66A00`) and taskset wakeups.
   - PPU thread executes between chunks without deadlocks or unhandled exceptions.
