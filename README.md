# God of War: Ascension (PS3) — Native PC Port

A native x86-64 static recompilation port of **God of War: Ascension** (PlayStation 3) for Windows, powered by the [`ps3recomp`](https://github.com/sp00nznet/ps3recomp) runtime and toolchain.

---

## Overview

This project decompiles and statically recompiles the original PlayStation 3 PowerPC 64 (PPU) and Synergistic Processing Element (SPU) binaries into native x86-64 machine code, executing directly on modern PC hardware without traditional emulation overhead.

### Key Architecture & Subsystems

- **PPU Static Recompilation:** The decrypted `EBOOT.ELF` is translated into native C++ functions partitioned into parallel compilation chunks and wired to high-level emulation (HLE) handlers.
- **SPU & WWS Job Scheduling:** Embedded SPU workloads, including Sony's World Wide Studios (WWS) Job System Policy Module and Edge Geometry programs, are lifted into native C and scheduled across host threads.
- **RSX Graphics Subsystem (Direct3D 12):** Emulates the PS3 RSX (NV4097) command FIFO buffer, rendering via a high-performance Direct3D 12 backend with hardware vertex fetch, pipeline state object (PSO) caching, and double-buffered swapchain presentation.
- **FIOS Asynchronous File Streaming:** High-throughput streaming virtual file system (VFS) capable of streaming game archives and disk assets at native NVMe/SSD speeds.
- **Thread Separation:** Independent host threads for Win32 message processing (preventing window freezes) and a 60 Hz RSX presentation ticker.
- **Input & Controls:** Native XInput controller support with keyboard and automated input fallbacks.

---

## Current Status & Roadmap

| Subsystem | Status | Description |
|---|---|---|
| **PPU Binary Lifting** | ✅ Complete | All 8 code chunks (>1.8M lines of generated C++) compiled and linked. |
| **SPU Workload Lifting** | ✅ Complete | Embedded SPU modules extracted, lifted, and registered (`WID 0..15`). |
| **VFS Streaming** | ✅ Complete | Asynchronous streaming from host storage active (~23 MB/s transfer rate). |
| **Win32 & Presentation** | ✅ Complete | Dedicated window thread and 60 Hz D3D12 RSX present ticker initialized. |
| **RSX FIFO Ring Buffer** | ✅ Complete | Memory-mapped GCM command buffer, jump/call validation, and fence drains. |
| **FIOS Synchronization** | 🔄 In Progress | Re-architecting condition variable semantics (`sys_lwcond`) to prevent lost signals. |
| **RSX Shaders & Draw Calls** | 📋 Planned | NV4097 vertex/fragment shader translation to HLSL / DXIL. |
| **Audio Engine** | 📋 Planned | Multi-stream CellAudio / ATRAC3+ decoding. |

Detailed specifications and daily progress notes can be found in [docs/](file:///C:/IfItWorksItWorks/Project/gowassension/docs/) and [.planning/](file:///C:/IfItWorksItWorks/Project/gowassension/.planning/).

---

## Prerequisites

- **Operating System:** Windows 10 / 11 (64-bit)
- **Compiler:** Visual Studio 2022 (MSVC v143 or `clang-cl`) with C++20 support
- **Build System:** [CMake](https://cmake.org/download/) (>= 3.24) and [Ninja](https://ninja-build.org/)
- **Scripting:** Python 3.10+ (for automation and analysis scripts)
- **Game Files:** Legally acquired copy of *God of War: Ascension* (PS3). You must provide your own decrypted `EBOOT.ELF` and game assets.

---

## Quickstart & Build Instructions

### 1. Clone the Repository (with Submodules)

Clone the repository and recursively fetch the upstream `ps3recomp` runtime and reference repositories:

```bash
git clone --recursive https://github.com/<your-username>/<your-repo-name>.git
cd gowassension
```

If you already cloned without submodules, initialize them:

```bash
git submodule update --init --recursive
```

### 2. Provide Game Assets

Place your decrypted game executable and assets into the Virtual File System (VFS) directory:

```text
./vfs/PS3_GAME/USRDIR/EBOOT.ELF
./vfs/PS3_GAME/USRDIR/EBOOT.BIN
./vfs/PS3_GAME/PARAM.SFO
```

*(Note: The `vfs/` directory is git-ignored to prevent distributing copyrighted material.)*

### 3. Apply Upstream Runtime Patches

Apply project patches to the local `ps3recomp` submodule:

```powershell
git -C ps3recomp apply ../patches/ps3recomp_current.patch
```

### 4. Build the Project

#### Step A: Build the ps3recomp runtime library

```powershell
cmake -S ps3recomp -B ps3recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ps3recomp/build
```

#### Step B: Build the God of War: Ascension executable

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The resulting binary will be located at `build/gow_ascension.exe`.

---

## Running the Port

Use the included PowerShell runner script to execute the port with custom profiles and telemetry:

```powershell
# Run with Direct3D 12 LiveDraw graphics enabled
.\tools\run_port.ps1 -LiveDraw

# Run interactively (disables timeout and auto-controller pulses)
.\tools\run_port.ps1 -Interactive

# Run with custom render resolution and timeout
.\tools\run_port.ps1 -LiveDraw -Width 1920 -Height 1080 -TimeoutSeconds 30
```

Configuration overrides can also be specified in `config.toml`:

```toml
[video]
width = 1280
height = 720
vsync = "on"
live_draw = true

[input]
auto_pad_start = false
deadzone = 7849
```

---

## Project Structure

```text
gowassension/
├── .planning/       # Milestone roadmap, phase requirements, and progress tracking
├── analysis/        # Function maps, import tables, and SPU/PPU triage data
├── docs/            # Technical documentation, bring-up logs, and architecture specs
├── patches/         # Unified diff patches for upstream ps3recomp runtime
├── ps3recomp/       # Upstream ps3recomp runtime and lifter tools (submodule)
├── src/
│   ├── boot_main.cpp      # Standalone initialization harness
│   ├── main.cpp           # Main application entry, thread orchestration, Win32 HWND loop
│   ├── hle_extra.cpp      # Extra HLE function implementations
│   ├── compat/            # God of War-specific HLE overrides (GCM, FIOS, SPURS, Syscall sync)
│   └── gen/               # Statically recompiled PPU chunks and lifted SPU workloads
├── tools/           # Automation scripts (Python & PowerShell) for testing and triage
├── twistedmetal/    # Reference upstream port repository (submodule)
├── vfs/             # Virtual file system root for game data (user-supplied)
└── CMakeLists.txt   # Root project build configuration
```

---

## Contributing

Contributions are welcome! Please follow these project rules when submitting pull requests:

### Project Guidelines

1. **Sub-repo Isolation:** Treat `./ps3recomp/` and `./twistedmetal/` as read-only upstream references. Implement all port-specific fixes, overrides, and shims in `./src/` (e.g., `src/compat/ascension_hle.cpp`).
2. **Never Hand-Edit `src/gen/`:** Files in `src/gen/` are machine-generated by the lifters. Improvements to code generation must be made in lifting scripts under `./tools/` or upstream.
3. **Cross-Platform Scripts:** All tooling in `./tools/` must be cross-platform Python (`.py`) or PowerShell (`.ps1`).
4. **Compile Flags:** Machine-generated files must be compiled in Release mode (`-O2` / `/O2`) without full debug PDBs to avoid exhausting disk space.

### Key Areas Needing Help

- **Host Condition Variables:** Implementing atomic signal/wait primitives (`CONDITION_VARIABLE` / `SRWLOCK`) in `src/compat/ascension_hle.cpp` for Sony's `sys_lwcond` to prevent lost wakeups in multi-threaded worker pools.
- **RSX Shaders:** Decompiling NV4097 microcode to clean HLSL shaders for complex geometry passes.
- **Edge Geometry:** Optimizing SPU-assisted vertex decompression and skinning pipelines.
- **Audio Decoding:** Implementing native backends for CellAudio callbacks and ATRAC3+ decoders.

---

## Legal & Disclaimer

- This project is an independent research and preservation effort.
- It is **not** affiliated with, endorsed by, or associated with Sony Interactive Entertainment, Santa Monica Studio, or any of their subsidiaries.
- This repository contains **no copyrighted game assets, proprietary Sony code, encryption keys, or game discs**.
- Users must dump and decrypt their own legally purchased copy of *God of War: Ascension* to use this port.
