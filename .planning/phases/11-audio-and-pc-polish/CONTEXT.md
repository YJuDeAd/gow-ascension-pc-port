# Phase 11 Context: Live WASAPI 8-Channel Audio, Frame Pacing & Native PC Enhancements

## Phase Objective
Verify and polish the live 8-channel audio mixing pipeline (`cellAudio` $\rightarrow$ Windows WASAPI) with precise 5.33 ms (187.5 blocks/sec) cadence and zero buffer underruns, expose native PC resolution scaling (`1280x720` up to `2560x1440` / `3840x2160`), vsync, and controller deadzone settings in `./config.toml`, run regression suites, and produce the comprehensive Milestone 3 report in `./docs/milestone3-report.md`.

---

## 1. Upstream Toolchain & Graphify Invariants
- **Graphify Query**:
  `graphify query "cellAudio WASAPI audio_mix_thread_func 8-channel downmix"`
  Resolved upstream locations:
  - `ps3recomp/libs/audio/cellAudio.c`:
    - `audio_mix_thread_func`: Background thread running at 48,000 Hz, consuming 256 samples per block (5.33 ms per block = 187.5 blocks/sec).
    - Pacing: Maintained via `audio_backend_room()` / `audio_backend_wait()` against Windows WASAPI endpoint `IAudioRenderClient`, with high-resolution `QueryPerformanceCounter` fallback when no physical device is attached.
    - 8-Channel Downmix: Big-endian float32 input (`ld_be_f32`) from guest audio ring buffers (`PortOpen(nChannel=8, nBlock=8)`). Downmixes 7.1 surround sound to stereo float:
      `left += center * 0.707f + lfe * 0.5f + rl * 0.5f + sl * 0.3f`
      `right += center * 0.707f + lfe * 0.5f + rr * 0.5f + sr * 0.3f`
    - Ring Buffer Advancement: Increments `port->read_index` and writes back `port->read_index % nblock` to guest memory (`port->read_idx_addr`) so guest sound engines never stall waiting on audio port tags.
    - Event Queue Dispatch: Pushes `CELL_AUDIO_EVENT_MIX` (0) to registered notify queues via `audio_notify_event_queues()`.
    - Diagnostics: `$env:AUDIO_RATE = "1"` reports real-time blocks/sec and dropped frame metrics; `$env:AUDIO_PEAK = "1"` reports peak amplitude per block and audio ring buffer occupancy maps.
  - `ps3recomp/libs/input/cellPad.c`:
    - XInput controller polling with stick deadzone `PAD_STICK_DEADZONE` (7849 = ~24%) and keyboard fallback.
  - `ps3recomp/templates/project/config.toml` & `ps3recomp/config/example.toml`:
    - Standard configuration schema for graphics resolution, vsync, audio settings, input mappings, and debug flags.

---

## 2. Locked Technical Specifications

### A. Live Audio Output (`cellAudio` $\rightarrow$ WASAPI)
- **Mixing Pipeline & Cadence**:
  - The background mixing thread must process 8-channel float blocks at 48 kHz, matching the hardware 5.33 ms (256 samples) period.
  - In-memory big-endian PCM samples are converted using `ld_be_f32` and downmixed into stereo float with standard Dolby 7.1 downmix coefficients.
  - Audio block read index (`CELL_AUDIO_BLOCK_8`) is monotonically tracked and modulo-published to `port->read_idx_addr` in guest memory.
  - Diagnostic monitoring via `$env:AUDIO_RATE = "1"` and `$env:AUDIO_PEAK = "1"`.

### B. Frame Pacing & Native Resolution Scaling in `./config.toml`
- **Configuration Schema**:
  - Root configuration file `./config.toml` defines:
    - `[graphics]`: `width`, `height`, `vsync`, `scale_factor`, `backend`. Supports 720p (native), 1080p, 1440p (2K), and 2160p (4K UHD).
    - `[audio]`: `backend = "wasapi"`, `sample_rate = 48000`, `buffer_size = 512`, `channels = 8`, `downmix = "stereo"`.
    - `[input]`: `backend = "xinput"`, `deadzone = 7849`, `keyboard_fallback = true`.
    - `[spu]`: `spu_interpreter_unlifted = true`, `spu_host_depth_max = 100000`.
    - `[debug]`: `log_audio_rate = true`, `log_audio_peak = true`, `live_draw = true`.
  - Runner integration: `tools/run_port.ps1` parses `./config.toml` to automatically configure runtime environment variables (`GOW_RSX_W`, `GOW_RSX_H`, `AUDIO_RATE`, `AUDIO_PEAK`).

### C. SDK Regression Suite & Milestone 3 Report
- **Validation**:
  - Execute core regression suites (`test_savedata_user_autosave.py`, `test_live_draw_sampler_key.py`, `regress.py list`).
  - Run live port with audio diagnostics active to verify end-to-end execution.
  - Compile the comprehensive Milestone 3 completion report in `./docs/milestone3-report.md`.
  - Update `./docs/bringup-status.md`, `.planning/STATE.md`, and `.planning/ROADMAP.md` (Milestone 3 100% COMPLETE).

---

## 3. Inviolable Laws Check
- **Law 1**: Queried `graphify query`, verified `cellAudio.c`, `cellPad.c`, and `config.toml` reference schemas.
- **Law 2**: Sub-repos `./ps3recomp/` and `./twistedmetal/` remain strictly read-only.
- **Law 3**: Machine-generated `src/gen/` remains completely untouched.
- **Law 4**: Windows host scripts in PowerShell (`.ps1`) and Python (`.py`). Release mode build without full `/Zi` PDBs.
- **Law 5**: Deterministic CLI instructions with deterministic `<verify>` commands.
