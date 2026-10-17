# Phase 6: Wws_Job Policy Module & Dynamic SPU Sub-Job Lifting — Context

**Phase:** 06-wws-job-spu-workloads  
**Milestone:** 2 — Wws_Job Engine, RSX Ring Buffers & Playable Menus  
**Status:** Locked & Ready for Planning  
**Planner & Executor:** Gemini Flash 3.8 High  

---

## 1. Executive Summary & Root Cause from Phase 5

During Phase 5 live boot diagnostics, the native port executed through CRT initialization, mounted all 21 `.PSARC` archives via FIOS, started 8-channel hardware audio output (`cellAudio`), and initialized the Direct3D 12 live swapchain. However, all subsequent graphics workloads and command buffer generators were stalled with the following runtime warning:

```text
[cellSpurs] _WorkloadAttributeInitialize(attr=0x0FEFE4E8 pm=0x0077CA80 size=12288 data=0x157D3480009FDF80 minC=1)
[cellSpurs] WorkloadAttributeSetName(attr=0x0FEFE4E8, "Wws_Job", "BrBDblBufWrkld")
[cellSpurs] AddWorkload(wid=8, pm=000000000077CA80, size=12288)
[cellSpurs] wid=8 PM NOT LIFTED (fp=0xC8D774963A96DCF2 size=12288) -- workload will not run
```

Five critical game workloads share this identical Policy Module:
1. `wid=8`: `Wws_Job/BrBDblBufWrkld` — Big Ring Buffer double-buffered RSX command stream generator.
2. `wid=9`: `Wws_Job/RenJobSet` — Render Job Set dispatcher (quad blits, UI quads, Scaleform menu commands).
3. `wid=10`: `Wws_Job/RenJobSet` — Secondary Render Job Set queue.
4. `wid=13`: `Wws_Job/RenderJobs` — Core scene draw-call command stream builder.
5. `wid=14`: `Wws_Job/MeshProcJobs` — `libedgegeom` vertex decompressor, skinning, and index stream generator.

All 5 workloads reference guest address `pm=0x0077CA80`, length `12,288` bytes (`0x3000`), with FNV-1a-64 image fingerprint `0xC8D774963A96DCF2`. Because `spu_workloads.c` previously only registered embedded SPU ELFs 1 through 4 (fingerprints `0x06F2...`, `0xF4C2...`, `0x4DA6...`, and `0x00E7...`), `cellSpurs` failed to find a lifted handler for `0xC8D774963A96DCF2` and refused to dispatch `wid=8..14`.

---

## 2. Upstream Tooling Architecture (`ps3recomp/tools/`)

The upstream repository provides a battle-tested pipeline for handling raw SPU Policy Modules embedded in guest PPU code:

1. **Extraction:**
   - Raw binary extraction from `./vfs/PS3_GAME/USRDIR/EBOOT.ELF`: `PH[0]` maps virtual address `0x10000` to file offset `0x0`. Therefore, guest EA `0x0077CA80` is located at file offset `0x0076CA80` (`0x0077CA80 - 0x10000`).
   - The 12,288 bytes at this offset have been verified to produce the exact FNV-1a-64 fingerprint `0xC8D774963A96DCF2`.

2. **ELF Wrapping (`wrap_spu_elf.py`):**
   - SPU Policy Modules are entered at LS base `0x0` / `0xA00`.
   - `python ./ps3recomp/tools/wrap_spu_elf.py ./spu_programs/wws_job_pm.bin --entry 0x0 --base 0x0 --out ./spu_programs/wws_job_pm.elf` wraps the raw instructions into a valid 32-bit big-endian SPU ELF with a `PT_LOAD` executable segment.
   - The `text_fp` of the wrapped ELF precisely matches `0xC8D774963A96DCF2`.

3. **SPU Lifting (`spu_lifter.py`):**
   - `python ./ps3recomp/tools/spu_lifter.py ./spu_programs/wws_job_pm.elf --auto-functions ./spu_programs/wws_job_pm.elf --symbol-prefix spu_wws_job_pm_ --output ./src/gen/spu/wws_job_pm/`
   - Recovers 208 functions, lifts 3,072 instructions into C++, debases relative includes to `#include "spu_context.h"`, and provides entry point `spu_wws_job_pm_spu_func_00000000`.

4. **Registry Wiring (`src/gen/spu/spu_workloads.c`):**
   - Register image 5 (`wws_job_pm`) with fingerprint `0xC8D774963A96DCF2ULL`.
   - Call `spu_wws_job_pm_spu_recomp_register()` to register the 208 indirect branch targets.

---

## 3. Crucial 64-Bit `data` Parameter Handling in SPURS ABI

Inspection of the boot log reveals a critical parameter characteristic:
- `wid=8` (`BrBDblBufWrkld`) passes `data=0x157D3480009FDF80` — a packed 64-bit parameter where:
  - Upper 32 bits (`0x157D3480`): High-word buffer / audit pointer.
  - Lower 32 bits (`0x009FDF80`): Low-word command / joblist EA.
- `wid=9..14` pass 32-bit zero-extended values such as `0x0000000012600580`.

### ABI Verification in `ps3recomp`:
1. In `cellSpurs.c` line 1464: `u64 data = vm_read64(attr_ea + WKATTR_DATA);` correctly reads the full 64-bit value from the guest attribute struct.
2. In `cellSpurs.c` line 1737: `slot->data = vm_read64(ea + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 8);` retrieves the stored 64-bit value.
3. In `spurs_policy.c` lines 284–287:
   ```c
   ctx->gpr[4]._u32[0] = (uint32_t)(wkl_data >> 32);  /* High half (word 0) */
   ctx->gpr[4]._u32[1] = (uint32_t)wkl_data;          /* Low half (word 1) */
   ctx->gpr[4]._u32[2] = 0;
   ctx->gpr[4]._u32[3] = 0;
   ```
   This satisfies the Cell SPU ABI 2.1.2 preferred doubleword layout (r4 bytes 0..7). The upper 32 bits `0x157D3480` and lower 32 bits `0x009FDF80` are both preserved and passed directly into the SPU Policy Module without truncation.

---

## 4. Dynamic Sub-Job Dispatch & SPU Interpreter Handoff

The Sony Santa Monica `Wws_Job` engine operates as an SPU Job Streamer:
1. The resident Policy Module (`pm=0x0077CA80`) polls the SPURS workload queue.
2. When jobs are queued, it issues DMA reads (`mfc_get`) from main memory to load sub-job code overlays into Local Store (typically at LS offset `0x4000` or dynamically allocated buffers) and branches indirectly (`bisl`, `bi`) to execute them.
3. Sub-jobs include:
   - `libedgegeom` mesh decompression & triangle culling (`MeshProcJobs`).
   - RSX command buffer packet builders (`BrBDblBufWrkld`, `RenderJobs`).
   - Material & lighting shaders pre-processing.

### Two-Tier Execution Strategy:
- **Tier 1 (Static Lift):** Extract static sub-job images using `./ps3recomp/tools/extract_jobmods.py` and lift them via `lift_jobmods.py`.
- **Tier 2 (Dynamic Interpreter Handoff):** When an indirect branch target in Local Store does not match a statically lifted function pointer, the runtime invokes `spu_indirect_branch` $\rightarrow$ `spu_interp.c` (`test_spu_interpreter_handoff.py` pattern) to interpret dynamically generated or patched code blocks. This guarantees zero dropped jobs and prevents crashes during dynamic sub-job execution.

---

## 5. Phase 6 Deliverables & Success Gates

1. **Plan 06-01: SPU Policy Module Extraction & Lifter Pipeline**
   - Extract `0x0077CA80` (12,288 bytes) to `./spu_programs/wws_job_pm.bin`.
   - Wrap with `wrap_spu_elf.py` to `./spu_programs/wws_job_pm.elf`.
   - Lift with `spu_lifter.py --auto-functions` into `./src/gen/spu/wws_job_pm/`.
   - Debase include paths to bare `#include "spu_context.h"`.

2. **Plan 06-02: Workload Registry Wiring & Standalone Compilation**
   - Update `./src/gen/spu/spu_workloads.c` with image 5 registration (`fp=0xC8D774963A96DCF2ULL`).
   - Update root `./CMakeLists.txt` to compile `./src/gen/spu/wws_job_pm/spu_recomp.c`.
   - Build `./build/gow_ascension.exe` with zero compile or link errors.

3. **Plan 06-03: Live Workload Execution & Interpreter Fallback Verification**
   - Run `./tools/run_port.ps1 -LiveDraw -TimeoutSeconds 15`.
   - Verify that all `wid=8 PM NOT LIFTED (fp=0xC8D774963A96DCF2)` warnings are eliminated.
   - Verify active execution logs for `wid=8` (`BrBDblBufWrkld`), `wid=9/10` (`RenJobSet`), `wid=13` (`RenderJobs`), and `wid=14` (`MeshProcJobs`).
   - Validate SPU interpreter fallback for any unlifted dynamic sub-jobs.
