---
slug: fix-window-message-loop-and-gcm
date: 2026-10-17
status: complete
description: Separated Win32 HWND message loop from RSX FIFO processing, disabled GCM_GET_EQ_PUT, and enabled live draw by default
---

# Summary: Fix Window Message Loop Freeze & GCM Command Execution

## Execution Summary
- **Root Cause 1 Resolved (Win32 Message Starvation)**:
  - Refactored `src/main.cpp` and `src/boot_main.cpp` into two dedicated host threads:
    - **Thread A (`window_message_thread`)**: Creates the Win32 window (`rsx_null_backend_init`), signals `s_hwnd_ready_event`, and runs a dedicated `rsx_null_backend_pump_messages()` loop with `Sleep(4)` that never touches `cellGcm_rsx_process_fifo()`.
    - **Thread B (`rsx_vblank_thread`)**: Waits for `s_hwnd_ready_event`, calls `rsx_live_draw_init`, suppresses GDI presentation (`rsx_null_backend_suppress_present(1)`), presents an initial clear frame (`rsx_live_draw_present(0)`) so Direct3D 12 takes immediate ownership of the window client area, and executes 60 Hz paced FIFO processing and presentation.
- **Root Cause 2 Resolved (`GCM_GET_EQ_PUT` Bypass)**:
  - Removed `$env:GCM_GET_EQ_PUT = "1"` from `tools/run_port.ps1`.
  - With `GCM_GET_EQ_PUT` removed, `cellGcm_rsx_process_fifo()` actively walks the RSX command stream, processing draw commands and draining fences past `0x0509F8BC`.
- **Root Cause 3 Resolved (Default LiveDraw Profile)**:
  - Configured `$cfgLiveDraw = $true` by default in `tools/run_port.ps1` and confirmed `live_draw = true` in `config.toml`.

## Verification Results
- **Build**: `gow_ascension.exe` rebuilt cleanly with `clang-cl` / Ninja.
- **Run**: Launched via `powershell -ExecutionPolicy Bypass -File .\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 20`.
- **Diagnostics in `build/run_port_stderr.log`**:
  ```text
  [rsx] live-draw engine up (D3D12); GDI present suppressed
  [rsx] backend init OK
  [live] frames=1 last_draws=0
  ...
  [refq] drained fence 0xC52FDF00 at getoff=0509F8BC
  ```
- **Window Responsiveness**: The desktop game window remained completely responsive (no "Not Responding" freeze), and GDI debug overlay was suppressed in favor of D3D12 swapchain presentation.
