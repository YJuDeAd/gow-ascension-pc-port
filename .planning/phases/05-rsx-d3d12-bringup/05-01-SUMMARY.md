# Plan 05-01: D3D12 Window, GCM IO Memory Mapping & Flip Presentation — Summary

**Execution Date:** 2026-10-17  
**Status:** Completed and Verified  

## Accomplishments
1. **Added `-LiveDraw` Profile to `tools/run_port.ps1`:**
   - Implemented `[switch]$LiveDraw` parameter.
   - Sets `$env:RSX_LIVE_DRAW = "1"`, `$env:GCM_RECDBG = "1"`, `$env:GCM_REFLOG = "1"`, `$env:LD_TRACE_PSO = "1"`, and `$env:GCM_SEMA_ACQUIRE = "0"`.
2. **GCM IO Memory Mapping Audit:**
   - Validated strict IO mapping registration via `test_gcm_map_flags.py`.
   - Verified that `gow_live_guest_ptr` resolves main memory (`location=1`) and RSX local video memory (`location=0`).
3. **D3D12 Window & Swapchain Presentation:**
   - Rebuilt `gow_ascension.exe` and verified live execution.
   - `rsx_live_draw` successfully initialized D3D12 device with DRED auto-breadcrumbs, C-compact vertex decoding, and buffered FP constants.
   - Registered 3x 720p display buffers into `live-draw` (`0x0`, `0x384000`, `0x708000`).
   - Created `docs/rsx-d3d12-bringup.md`.

## Verification
- Verified `VERIFY_LIVEDRAW_PROFILE_OK`, `VERIFY_GCM_IO_MAPPING_OK`, and `VERIFY_D3D12_SWAPCHAIN_UP_OK`.
