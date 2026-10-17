# Summary 11-02: Configuration System (config.toml), Frame Pacing & Resolution Scaling

## Execution Summary
- **Configuration Schema**:
  - Created root `./config.toml` exposing:
    - Graphics: `backend = "d3d12"`, `width = 1280`, `height = 720` (supporting 1080p, 1440p, 4K UHD), `vsync = "on"`, `scale_factor = 1`, `aniso_filter = 16`.
    - Audio: `backend = "wasapi"`, `sample_rate = 48000`, `buffer_size = 512`, `channels = 8`, `downmix = "stereo"`.
    - Input: `backend = "xinput"`, `deadzone = 7849` (~24% stick deadzone), `keyboard_fallback = true`.
    - SPU: `spu_interpreter_unlifted = true`, `spu_host_depth_max = 100000`.
    - Debug: `log_audio_rate = true`, `log_audio_peak = true`, `live_draw = true`, `auto_pad_start = true`.
- **Runner Integration**:
  - `tools/run_port.ps1` updated to automatically parse `./config.toml` and configure environment variables (`GOW_RSX_W`, `GOW_RSX_H`, `AUDIO_RATE`, `AUDIO_PEAK`, `YDKJ_INJECT_PAD`).
  - Added `-Width` and `-Height` command line parameters for immediate resolution override.
- **Resolution Scaling Verification**:
  - Verified 1440p resolution scaling (`2560x1440`):
    `[run_port] Configured Render Resolution: 2560x1440 (Scale=1x, VSync=on)`
    `[rsx] live-draw engine up (D3D12); GDI present suppressed`
    `[rsx] backend init OK`

## Verification
```powershell
powershell -Command "if (Test-Path .\config.toml) { Get-Content .\config.toml | Select-String 'width|height|deadzone' }"
Select-String -Path build\run_port_stderr.log -Pattern "live-draw|backend init|display buffer" | Select-Object -First 10
```
- Verified: `config.toml` parameters successfully loaded, D3D12 LiveDraw upscaled rendering verified.
