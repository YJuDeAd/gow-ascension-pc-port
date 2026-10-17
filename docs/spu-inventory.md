# docs/spu-inventory.md — God of War: Ascension SPU Workload Catalog

## Overview

God of War: Ascension (*BCAS25016* / *BCUS98232*) leverages the PS3's 6 Synergistic Processing Units (SPUs) managed via Sony's SPURS (SPU Runtime System v2) task and jobqueue framework.

Unlike some first-party titles that distribute SPU job modules in external SDAT archives (`patch.sdat` / `data.sdat`), Santa Monica Studio packages all SPU executables directly inside the main `EBOOT.ELF` in the `.rodata` segment between offset `0x00671880` and `0x0069C854`.

All 4 embedded SPU programs are valid 128-bit SPU ELF32 binaries (`e_machine = 23`). They are extracted by `ps3recomp/tools/extract_spu_images.py` and indexed by their 64-bit FNV-1a content fingerprints, exactly matching `runtime/spu/spu_workload.c`.

---

## SPU Image Inventory

| Image File | EBOOT Offset | Size | Entry Point | Detected Functions | FNV-1a-64 Fingerprint | Identified Role |
|---|---|---|---|---|---|---|
| `spu_0000_at_00671880.elf` | `0x00671880` | 56,484 B | `0x00003070` | 506 functions (99.0% coverage) | `0x06F293A98B57A34C` | **Edge Geometry & Skinning Pipeline (`libedgegeom`)**: processes vertex streams, matrix skinning, and frustum clipping. |
| `spu_0001_at_0067F580.elf` | `0x0067F580` | 39,508 B | `0x00003050` | 874 functions (94.5% coverage) | `0xF4C2A58BDE92CE5E` | **Audio Mixer & Physics Engine**: high function density performing low-latency DSP audio mixing and rigid-body constraint solving. |
| `spu_0002_at_00689000.elf` | `0x00689000` | 41,796 B | `0x00003070` | 403 functions (98.0% coverage) | `0x4DA69B4166D997AD` | **Particle Simulation & Render (`ParticleRender`)**: Santa Monica Studio custom SPU particle compute, emitter simulation, and vertex generation. |
| `spu_0003_at_00693380.elf` | `0x00693380` | 38,100 B | `0x00003050` | 661 functions (91.4% coverage) | `0x00E729804474449A` | **Sony Edge Zlib Decompressor Task (`edgezlib`)**: version `1.2.3.0-PS3-SPU-EDGE`. Primary streaming decompressor for `.wad` game archives. Dispatched at boot. |

---

## Detailed Module Breakdown

### 1. `spu_0003_at_00693380.elf` — Sony Edge Zlib Decompressor
- **File Size:** 38,100 bytes (extent: 38,100 bytes)
- **Content Fingerprint:** `0x00E729804474449A`
- **Text Segment:** `0x3000 .. 0xC350` (37,712 bytes, 9,428 instructions)
- **Functions:** 661 functions detected with 91.4% function coverage.
- **Embedded String Signatures:**
  - `edgezlib_inflate_task.cpp`
  - `edgezlib_fetch_and_inflate_raw_data.cpp`
  - `1.2.3.0-PS3-SPU-EDGE`
  - `EDGE ASSERTION FAILURE: %s(%d)`
  - `EDGE ZLIB ERROR: outputUncompSkipBeginSize(%d) + outputUncompSkipEndSize(%d) > expectedUncompSize(%d)`
  - `EDGE ZLIB ERROR: edgeZlibFetchAndInflateLargeRawData failed (%d)`
- **Runtime Significance:** When God of War: Ascension boots and initializes SPURS via `cellSpursInitializeWithAttribute2`, it immediately spawns a taskset with `cellSpursCreateTask2` requesting workload `0x00E729804474449A`. Recompiling this workload enables the game to inflate compressed package tables without stalling.

### 2. `spu_0002_at_00689000.elf` — Particle Simulation & Compute
- **File Size:** 41,796 bytes
- **Content Fingerprint:** `0x4DA69B4166D997AD`
- **Text Segment:** `0x3000 .. 0xD130` (41,264 bytes, 10,316 instructions)
- **Functions:** 403 functions detected with 98.0% function coverage.
- **Embedded String Signatures:**
  - `ParticleRender`
- **Role:** Handles high-volume particle emitters, physical particle bouncing, color ramps, and quad vertex generation offloaded from the PPU.

### 3. `spu_0000_at_00671880.elf` — Edge Geometry & Mesh Processing
- **File Size:** 56,484 bytes
- **Content Fingerprint:** `0x06F293A98B57A34C`
- **Text Segment:** `0x3000 .. 0x10AA0` (55,968 bytes, 13,992 instructions)
- **Functions:** 506 functions detected with 99.0% function coverage.
- **Role:** Implements Sony's `libedgegeom` SPU pipeline. Decompresses high-density vertex buffers, skinning weights, and indices using 128-bit SIMD, outputting clean vertex arrays directly into RSX command buffers.

### 4. `spu_0001_at_0067F580.elf` — Audio & Physics Co-Processing
- **File Size:** 39,508 bytes
- **Content Fingerprint:** `0xF4C2A58BDE92CE5E`
- **Text Segment:** `0x3000 .. 0xC900` (39,168 bytes, 9,792 instructions)
- **Functions:** 874 functions detected with 94.5% function coverage.
- **Role:** Real-time multi-channel audio voice mixing, filtering, and constraint solving.

---

## SDAT Archive Audit
An audit performed using `ps3recomp/tools/extract_jobmods.py` against `./vfs/PS3_GAME/USRDIR/` confirmed:
- `patch.sdat`: **Not present**
- `data.sdat`: **Not present**

Santa Monica Studio did not employ loose SDAT WWS JobQueue binaries in the root USRDIR; all SPU workloads are statically bundled into `EBOOT.ELF` and dynamically staged into SPU Local Storage via SPURS task contexts.

---

## Recompilation Strategy
All 4 modules are lifted via `ps3recomp/tools/build_spu_workloads.py`:
1. **Symbol Namespacing:** Each SPU ELF is compiled under a unique C prefix (e.g., `spu_0003_at_00693380_*`) so that common SPU CRT symbols (`spu_func_*`, local function tables) link together with 0 collision.
2. **Debased Includes:** `#include "../../runtime/spu/spu_context.h"` is transformed to `#include "spu_context.h"`, resolved by CMake include paths.
3. **Automated Registration:** `src/gen/spu/spu_workloads.c` uses an `__attribute__((constructor))` hook `gow_spu_register_all()` to register all 4 fingerprints with `spu_workload_register()` before `main()` begins.
