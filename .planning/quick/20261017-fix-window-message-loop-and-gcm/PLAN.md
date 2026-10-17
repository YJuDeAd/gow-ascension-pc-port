---
slug: fix-window-message-loop-and-gcm
date: 2026-10-17
description: Separate HWND message loop from RSX FIFO processing, disable GCM_GET_EQ_PUT, and enable live draw by default
---

# Plan: Fix Window Message Loop Freeze & GCM Command Execution

## Root Cause Analysis
1. **HWND Message Loop Blocked**: `src/main.cpp` ran `rsx_null_backend_pump_messages()` inside `vblank_ticker` after calling `cellGcm_rsx_process_fifo()`, which can run up to 240 times per tick. When FIFO processing takes more than a few milliseconds, the Win32 message pump starves, causing Windows to mark the window "Not Responding" and freeze GDI/D3D12 updates.
2. **`GCM_GET_EQ_PUT=1` Bypassed Draw Packets**: `cellGcmSys.c` line 2180 sets `vm_write32(GCM_CONTROL_GUEST_ADDR + 4, put)` when `GCM_GET_EQ_PUT=1`. This caused the libgcm FIFO walker to report that `get` immediately reached `put`, bypassing FIFO draw and flip command execution.
3. **Default LiveDraw Configuration**: `live_draw` was not defaulted to `true` in `tools/run_port.ps1`.

---

## Tasks

### Task 1: Refactor `src/main.cpp` for Dedicated Message Threading
- Create `window_message_thread` (Thread A) to initialize the Win32 window (`rsx_null_backend_init`), signal `s_hwnd_ready_event`, and run a dedicated `rsx_null_backend_pump_messages()` loop with `Sleep(4)` that never touches `cellGcm_rsx_process_fifo()`.
- Create `rsx_vblank_thread` (Thread B) to wait for `s_hwnd_ready_event`, initialize `rsx_live_draw_init(hwnd, ...)`, suppress GDI present, issue initial clear present (`rsx_live_draw_present(0)`), and run 60 Hz paced FIFO processing and presentation.
- Spawn both threads from `main()`.

### Task 2: Configure Environment & Remove `GCM_GET_EQ_PUT`
- Update `tools/run_port.ps1` to default `$cfgLiveDraw = $true` and remove `$env:GCM_GET_EQ_PUT = "1"`.
- Ensure `config.toml` has `live_draw = true`.

### Task 3: Build & Verify Live Frame Progression
- Rebuild `gow_ascension.exe` via `cmake --build build`.
- Run `powershell -ExecutionPolicy Bypass -File .\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 20`.
- Verify in `build/run_port_stderr.log`:
  - `[rsx] live-draw engine up (D3D12)`
  - Window does not freeze ("Not Responding")
  - `[live] frames=` progresses past 0.
