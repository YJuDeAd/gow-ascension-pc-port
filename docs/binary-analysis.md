# God of War: Ascension (PS3) — Binary Analysis Report

**Target Binary:** `vfs/PS3_GAME/USRDIR/EBOOT.ELF`  
**Title ID:** `BCAS25016` / `BCUS98232`  
**Architecture:** Sony PlayStation 3 (Cell Broadband Engine PPC64, Big-Endian)  
**ELF Type:** ELF64 executable  
**Date Generated:** 2026-10-17  

---

## 1. Executive Summary

This report documents the binary characteristics, memory layout, function inventory, and firmware dependencies of the decrypted `EBOOT.ELF` executable for *God of War: Ascension*. The binary was analyzed using `ps3recomp` toolchain utilities and custom Python inspection tools (`find_code_end.py`, `relift.py`).

- **ELF File Size:** 8,241,152 bytes (8.2 MB)
- **Entry Point Descriptor:** `0x00808360`
  - Code Entry Address: `0x00010410`
  - Table of Contents (`.toc` / `r2`): `0x0081EF60`
- **Total Recovered Functions:** 19,570 functions
- **Firmware Imports:** 345 NIDs across 25 dynamic libraries
- **HLE Dispatch Handlers:** 1,195 symbols generated across 93 runtime modules
- **Generated Recompiled Code Size:** 122.25 MB across 8 chunks (`src/gen/`)

---

## 2. ELF Memory Map & Section Inventory

The binary contains 37 sections. The key program segments and boundaries are:

| Idx | Section Name | Virtual Address | End Address | Size (Bytes) | Flags | Description |
|---|---|---|---|---|---|---|
| 1 | `.text` | `0x00010410` | `0x0062C4CC` | `0x0061C0BC` (6.11 MB) | `AX` (Alloc + Exec) | Executable PPC64 machine instructions |
| 2 | `.rodata` | `0x0062C4D0` | `0x00785080` | `0x00158BB0` (1.35 MB) | `A` (Alloc) | Read-only strings, float constants, lookup tables |
| 3 | `.data` | `0x00785080` | `0x00808360` | `0x000832E0` (524 KB) | `WA` (Write + Alloc) | Initialized mutable global data |
| 4 | `.opd` | `0x00808360` | `0x0081EF60` | `0x00016C00` (91 KB) | `WA` (Write + Alloc) | Official Procedure Descriptors (Function pointers) |
| 5 | `.toc` | `0x0081EF60` | `0x00827180` | `0x00008220` (32 KB) | `WA` (Write + Alloc) | Table of Contents (PPC64 global pointer) |
| 6 | `.bss` | `0x00827180` | `0x00B26D40` | `0x002FFBC0` (3.00 MB) | `WA` (Write + Alloc) | Uninitialized zero-allocated BSS storage |

### Critical Code Boundary Discovery
Using `./tools/find_code_end.py`, the boundary between executable instructions and read-only data was identified:
- **Last Executable Section:** `.text` ending at `0x0062C4CC`
- **First Read-Only Section:** `.rodata` starting at `0x0062C4D0`
- **Enforced Boundary Flag:** `--code-end 0x0062C4CC`

> [!IMPORTANT]
> Passing `--code-end 0x0062C4CC` to `ppu_lifter.py` is strictly required. Without this boundary flag, the disassembler continues past `.text` into `.rodata`, interpreting ASCII strings and lookup tables as PPC branch instructions, causing exponential false-function blowup and gigabytes of corrupt C++ code.

---

## 3. Function Recovery & Code Generation

Function identification was conducted in two stages:

1. **OPD Table Parsing (`ppu_loader.py`):**
   - 7,556 unique function descriptors identified via the `.opd` section.
   - Initial seed JSON: `analysis/EBOOT.functions.json`.

2. **Heuristic Disassembly Scan (`find_functions.py`):**
   - Scanned all basic block branch targets (`b`, `bl`, `bctrl`) and standard PPC64 function prologues (`mflr r0`, `stdu r1, -N(r1)`).
   - Recovered 11,643 additional unreferenced internal static functions.
   - Total function catalog: 19,199 unique function entry points (`analysis/functions.json`).

3. **PPU Recompilation (`ppu_lifter.py`):**
   - Generated 19,570 functions (including HLE stub dispatchers) partitioned into 8 compilation chunks.
   - Code output directory: `src/gen/`.

### Source Chunks Breakdown

| Filename | Purpose | Size | Function Range |
|---|---|---|---|
| `ppu_recomp_000.cpp` | PPU Recompiled Chunk 0 | 16.77 MB | Functions 0 – 2,446 |
| `ppu_recomp_001.cpp` | PPU Recompiled Chunk 1 | 16.62 MB | Functions 2,447 – 4,892 |
| `ppu_recomp_002.cpp` | PPU Recompiled Chunk 2 | 16.31 MB | Functions 4,893 – 7,338 |
| `ppu_recomp_003.cpp` | PPU Recompiled Chunk 3 | 16.12 MB | Functions 7,339 – 9,784 |
| `ppu_recomp_004.cpp` | PPU Recompiled Chunk 4 | 16.23 MB | Functions 9,785 – 12,230 |
| `ppu_recomp_005.cpp` | PPU Recompiled Chunk 5 | 14.56 MB | Functions 12,231 – 14,676 |
| `ppu_recomp_006.cpp` | PPU Recompiled Chunk 6 | 14.84 MB | Functions 14,677 – 17,122 |
| `ppu_recomp_007.cpp` | PPU Recompiled Chunk 7 | 14.69 MB | Functions 17,123 – 19,570 |
| `ppu_recomp_table.cpp` | Global Function Dispatch Table | 1.11 MB | Master lookup table |
| `ppu_recomp.h` | Recompiled Function Headers | 772 KB | C++ declarations |
| `ppu_hle_nids.cpp` | Firmware HLE NID Dispatch Table | 160 KB | 1,195 NID stubs |
| **Total** | **All Generated Sources** | **122.25 MB** | **11 files** |

---

## 4. Firmware Imports & Dependency Characterization

The executable imports 345 NIDs across 25 PS3 system libraries:

| Library Module | Imports Count | Role in Engine |
|---|---|---|
| `sys_lwmutex` | 7 | Low-latency lightweight mutex synchronization |
| `sys_lwcond` | 6 | Lightweight condition variables (FIOS dependency) |
| `sys_mutex` | 6 | Standard kernel mutexes |
| `sys_cond` | 6 | Standard condition variables |
| `sys_ppu_thread` | 14 | PPU thread creation, priority, joins |
| `sys_spu` | 10 | Direct SPU thread group management |
| `cellSpurs` | 26 | SPURS taskset and jobqueue orchestration |
| `cellGcmSys` | 18 | Low-level RSX graphics memory & display initialization |
| `cellFs` | 22 | File system I/O (disc, cache, WAD archives) |
| `cellAudio` | 12 | 7.1 / 5.1 multi-channel audio port output |
| `cellPad` | 7 | DualShock 3 controller polling |
| `cellResc` | 9 | Resolution scaling and format conversion |
| `cellSysutil` | 21 | System OS callbacks and display mode queries |
| `cellVoice` | 8 | Voice chat (multiplayer) — stub candidate |
| `sceNp` | 24 | PSN network services — stub candidate |
| `sceNpCommerce2` | 6 | PSN in-game store — stub candidate |
| `cellSysutilAvc2` | 5 | Video recording/broadcasting — stub candidate |
| Other System Modules | 138 | Memory, math, string, and utility functions |
| **Total** | **345 Imports** | **Full Firmware Footprint** |

### 4.1 NID Triage & Coverage Analysis
Cross-referencing all 345 imported NIDs against the `ps3recomp` runtime and offline PSN requirements:

- **Total Imports Extracted:** 345 NIDs
- **Covered (in upstream ps3recomp runtime):** 258 NIDs (74.8%)
- **Offline PSN Stubs (stubbed to `CELL_OK`):** 30 NIDs (8.7%)
- **Unresolved NIDs (requiring custom HLE/stubs):** 57 NIDs (16.5%)
- **Effective Coverage (Runtime + PSN Stubs):** 83.5%

#### Missing/Unresolved Imports Summary (57 NIDs)
The 57 unresolved symbols break down by subsystem:
- `cellSpurs` (14 NIDs): SPURS v2 API functions (`cellSpursInitializeWithAttribute2`, `cellSpursCreateTaskset2`, `cellSpursCreateTask2`, `cellSpursJoinTask2`, etc.) forwarded to SPURS v1 handlers in `src/hle_extra.cpp`.
- `sys_net` (11 NIDs): Network socket stubs.
- `cellHttp` (7 NIDs): HTTP client and cookie stubs.
- `cellNetCtl` (6 NIDs): Network state query (`cellNetCtlGetState` returning `CELL_NET_CTL_STATE_IP_OBTAINED = 3`).
- `sysPrxForUser` (6 NIDs): PRX dynamic module loading stubs.
- `sys_fs` (3 NIDs): Extended file system operations (`cellFsReadWithOffset`, `cellFsWriteWithOffset`).
- `cellSsl` (3 NIDs): SSL certificate/context stubs.
- `libvdec` (2 NIDs): Video decoder stubs.
- `cellGcmSys` (1 NID): Internal GCM helper.
- `cellSysutil` (1 NID): Save data delete helper.
- `cellSysmodule` (1 NID): Memory container helper.
- `cellGame` (1 NID): Game patch check helper.
- `cellHttpUtil` (1 NID): HTTP utility helper.

---

## 5. Verification & Disk Guardrail Compliance

- **Disk Space Usage:** Generated sources consume **122.25 MB**, well below the project safety guardrail of 600 MB.
- **Compilation Budget:** 8 chunks allow full parallel compilation using Ninja on multicore hosts without exceeding 4 GB RAM per compilation unit.
- **LAW Compliance:**
  - `AGENTS.md` LAW 2: Sub-repos `./ps3recomp/` and `./twistedmetal/` remained unmodified.
  - `AGENTS.md` LAW 3: No files in `src/gen/` were edited by hand or loaded into context.
  - `AGENTS.md` LAW 4: Release mode `/O2` without `/Zi` full PDB symbols keeps disk overhead under 200 MB.
