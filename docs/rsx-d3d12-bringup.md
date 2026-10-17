# docs/rsx-d3d12-bringup.md — RSX-to-D3D12 Bring-Up & Display Pipeline Log

## Executive Summary

Phase 5 transitions **God of War: Ascension** (*BCAS25016* / *BCUS98232*) from headless hardware initialization into an active Direct3D 12 live rendering pipeline:
- The Win32 rendering window and DXGI swapchain initialize cleanly attached to the host HWND.
- The D3D12 live-draw engine (`rsx_live_draw.c`) is brought up with DRED auto-breadcrumbs and buffered fragment program constants.
- The 3 triple-buffered 720p display buffers allocated during `cellGcmInit` are registered into `live-draw`:
  - Display Buffer 0: `loc0:0x00000000`, pitch 5120, 1280x720
  - Display Buffer 1: `loc0:0x00384000`, pitch 5120, 1280x720
  - Display Buffer 2: `loc0:0x00708000`, pitch 5120, 1280x720
- GCM IO memory mapping tables pass validation against `ps3recomp/tools/`.
- Command FIFO methods and flip requests route directly into D3D12 swapchain presentation.

---

## 1. D3D12 Live Engine Initialization & Window Pump

With `$env:RSX_LIVE_DRAW = "1"` configured in `tools/run_port.ps1`:
```text
[rsx-fp-constants] mode=buffered (startup-selected) d3dcompile_flags1=0x0 d3dcompile_flags2=0x0
[rsx-vertex] mode=C-compact features=0x7 (startup-selected)
[dred] auto-breadcrumbs and page-fault tracking enabled
[rsx] live-draw engine up (D3D12); GDI present suppressed
[rsx] backend init OK
```

The game's 3 display surfaces register into `live-draw`:
```text
[live-draw] display buffer 0 = loc0:0x00000000 pitch=5120 1280x720
[live-draw] display buffer 1 = loc0:0x00384000 pitch=5120 1280x720
[live-draw] display buffer 2 = loc0:0x00708000 pitch=5120 1280x720
```

---

## 2. GCM IO Memory Mapping Verification

The GCM IO memory aperture accommodates the game's allocations:
- Command buffer: `0x10000` (64 KB)
- IO memory: `0x5200000` (82 MB) mapped at `0x11000000`
- Guest address translation: `gow_live_guest_ptr` in `main.cpp` resolves both main memory allocations (`location=1`) and RSX local video memory (`location=0`).

---

## 3. RSX Label Auditing & Semaphore Synchronization

### Label Allocation Audit (`cellGcmGetLabelAddress`)
During boot, God of War: Ascension queries synchronization labels via `cellGcmGetLabelAddress`:
- `index=0` $\rightarrow$ guest EA `0x20000000` (primary frame/flip synchronization label)
- `index=1` $\rightarrow$ guest EA `0x20000010` (SPURS geometry/lighting worker unblock label)

Both labels map cleanly to `GCM_LABEL_GUEST_BASE` (`0x20000000u`) with 16-byte strides and are directly accessible by guest PPU threads, recompiled SPU tasksets, and the host RSX FIFO decoder.

### Frame Flip Presentation
- The engine issues frame flips via `_cellGcmSetFlipCommand(buf=0)`.
- The flip marker is consumed by `cellGcm_take_flip_pending_synced()` and routes directly to `rsx_live_draw_present(0)`.
- The FIFO command processor resynchronized beyond initial unmapped alignment jumps and drained over 100 reference fences (`0x000497D0` through `0x001F4040`) without blocking or deadlocking.
