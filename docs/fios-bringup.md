# docs/fios-bringup.md — God of War: Ascension Boot, FIOS & Graphics Bring-Up Log

## Executive Summary

Phase 4 of the native x86-64 static recompilation of **God of War: Ascension** (*BCAS25016* / *BCUS98232*) achieved the project's primary **Milestone 1** target:
- Booting natively through the CRT entrypoint (`0x00808360`).
- Initializing the Virtual File System (VFS) with zero-disk-copy direct asset streaming from the mounted decrypted ISO on `E:`.
- Reading game metadata via `cellGameBootCheck` and `PARAM.SFO`.
- Loading and decompressing package tables from `GAME.PSARC` via FIOS and the Sony Edge Zlib SPU decompression pipeline.
- Initializing the 8-channel low-latency audio pipeline via `cellAudio`.
- Registering SPURS multi-threaded rendering and physics workloads (`Wws_Job/RenderJobs`, `MeshProcJobs`, `RenJobSet`).
- Reaching and completing graphics initialization via `cellGcmInit` / `_cellGcmInitBody`, configuring tile regions, Zcull memory, and allocating 720p triple-buffered display surfaces.

---

## Technical Architecture & Milestones Achieved

### 1. Zero-Copy VFS Asset Streaming
- **Virtual Disk Drive:** Decrypted game ISO mounted directly as virtual drive `E:\` (`PS3VOLUME`).
- **Configuration:** Harness scripts and `main.cpp` derive and enforce `$env:PS3_VFS_ROOT = "E:"`.
- **Disk Guardrail:** 35 GB of `.psarc` and `.wad` game archives are streamed directly across the virtual drive without duplicating large files onto the `C:` drive, preserving 40+ GB of free host disk space.
- **File System HLE:** Cleanly intercepts `cellFsOpen` / `cellFsRead` / `cellFsReadWithOffset`, translating guest `/dev_bdvd/` paths to `E:/PS3_GAME/USRDIR/` paths.

### 2. Edge Zlib Decompressor & FIOS Bring-Up
During initial archive reading of `GAME.PSARC`, Santa Monica Studio's engine streams package tables through Sony's Edge Zlib SPU decompression pipeline (`edgezlib`, SPU image 3 at `0x00693380`). Several recompilation hurdles were diagnosed and resolved in `src/compat/ascension_hle.cpp` and `src/compat/gow_inflate.cpp`:

1. **In-Place Buffer Overlap Fix:**
   - Compressed streams (e.g. `src_ea = 0x110539F1`, length 18,703 bytes) are mapped inside destination uncompressed buffers (`0x11048300..0x11058300`).
   - In-place decoding was overwriting uncompressed output over its own source data mid-stream.
   - Resolved by inflating into a temporary host heap buffer (`malloc(dstlen)`), then copying via `memcpy(vm_base + dst_ea, tmp, n)`.

2. **32-Bit Completion Status Clearing:**
   - Decompression requests pushed to SPURS LFQueue `0x15E66A80` pass a work-counter pointer `user_ea` formatted as `(request_slot + 0x28) | 1`.
   - The PPU polling loop in `func_0047F984` and `func_0047D5AC` polls `vtable[4]` of `EdgeZlibDecompressor` (`func_005A8D6C`), which checks `clz(vm_read32(slot + 0x28)) == 32` (i.e. status == 0).
   - The HLE previously performed a single-byte write `vm_write8(user_ea, 0)` at an odd address, leaving the big-endian 32-bit word non-zero (`0x00000001`), spinning forever at `0x004803B4`.
   - Fixed by computing `status_ea = user_ea & ~3u` and executing `vm_write32(status_ea, 0)`, immediately breaking the spinlock.

3. **Sub-Chunked Slice Caching:**
   - Santa Monica Studio's package reader splits large uncompressed streams into consecutive 64-byte slices using `skipBegin` (`data + 0x1A`) and `skipEnd` (`data + 0x1C`), requesting over 600 consecutive queue items for the same compressed stream.
   - Implemented an in-memory stream cache in `_cellSpursLFQueuePushBody` keyed by `(src_ea, srclen)`: the stream is inflated once, and all subsequent slice requests are served instantly via `memcpy(vm_base + dst_ea, s_cached_uncomp + skipBegin, dstlen)` in < 1 millisecond.

### 3. Audio Subsystem Initialization
- Successfully passed through `cellSysmoduleLoadModule(CELL_SYSMODULE_AUDIO)`.
- Initialized audio hardware:
  ```text
  [cellAvconfExt] AudioOutConfigure(audioOut=0) -> ok
  [cellAudio] Init()
  [cellAudio] PortOpen(nChannel=8, nBlock=8)
  [cellAudio] Mixing thread started
  [cellAudio] PortStart(port=0)
  ```

### 4. Graphics & RSX Hardware Initialization
- **`_cellGcmInitBody`:** Executed cleanly with:
  - Command buffer size: `0x10000` (64 KB)
  - IO memory size: `0x5200000` (82 MB)
  - IO address: `0x11000000`
- **Tile Memory Configuration:** Configured and bound 11 tile regions (`index 0..10`) with specified pitch and dimensions.
- **Zcull Configuration:** Bound Zcull regions (`0..4`).
- **Display Buffer Allocation:** Successfully registered 720p triple-buffered display buffers:
  ```text
  [cellGcmSys] SetDisplayBuffer(id=0, offset=0x0, pitch=5120, 1280x720)
  [cellGcmSys] SetDisplayBuffer(id=1, offset=0x384000, pitch=5120, 1280x720)
  [cellGcmSys] SetDisplayBuffer(id=2, offset=0x708000, pitch=5120, 1280x720)
  ```
- **SPURS Workload Dispatches:** Engine spawned parallel worker workloads:
  - `Wws_Job/RenderJobs` (wid=13)
  - `Wws_Job/MeshProcJobs` (wid=14)
  - `Wws_Job/RenJobSet` (wid=9, 10, 11, 12)
  - `Wws_Job/BrBDblBufWrkld` (wid=8)

---

## Stability & Verification Matrix

| Verification Metric | Target | Result | Status |
|---|---|---|---|
| Binary Compilation | 0 compiler / linker errors | Built in 2.0s via `clang-cl`/Ninja | **PASS** |
| Memory Protection | 0 ACCESS_VIOLATION crashes | 0 crashes across 30s runtime | **PASS** |
| Disk Usage (`C:`) | $\ge 35$ GB free | 39.9 GB free | **PASS** |
| FIOS Archive Streaming | Read and inflate WadGraph / package tables | 34+ package blocks inflated | **PASS** |
| Audio Initialization | `cellAudioPortStart` active | 8-ch mixing thread up | **PASS** |
| Graphics Initialization | Reach `cellGcmInit` & display buffers | 3x 720p buffers registered | **PASS** |
