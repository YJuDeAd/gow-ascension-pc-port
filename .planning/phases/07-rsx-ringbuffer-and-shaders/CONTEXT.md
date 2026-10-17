# Phase 7: RSX Big Ring Buffer (BrB) FIFO Sync & Shader Decompilation — Context

**Phase:** 07-rsx-ringbuffer-and-shaders  
**Milestone:** 2 — Wws_Job Engine, RSX Ring Buffers & Playable Menus  
**Status:** Locked & Ready for Planning  
**Planner & Executor:** Gemini Flash 3.8 High  

---

## 1. RSX Big Ring Buffer (`BrB`) & FIFO Dynamics Analysis

In God of War: Ascension, the rendering architecture does not rely on simple linear PPU command buffers. Instead, it utilizes Santa Monica Studio's Big Ring Buffer (`BrB`) subsystem driven by `brbDriver` (`tid=13`), `brbClient` (`tid=14`), and the SPU workload `Wws_Job/BrBDblBufWrkld` (`wid=8`).

### FIFO Execution Flow:
1. **Initial Command Stream:**
   - `_cellGcmInitBody` maps an 82 MB IO aperture at guest address `0x11000000` (`cmdSize=0x10000`, `ioSize=0x5200000`).
   - At IO offset `0x00000000`, a JUMP word (`0x20010000`) directs the GPU to IO offset `0x00010000`.
   - Initial startup commands advance through `0x00010038` where fence `0x1` is drained.
2. **Big Ring Buffer Handover:**
   - At IO offset `0x00014000`, the command stream transitions into the Big Ring Buffer chunks (`0x00132040+`, `0x00134040+`, etc.).
   - Prior to Phase 6, because `wid=8` (`BrBDblBufWrkld`) was unlifted, the RSX encountered unpopulated command buffer words (`0x3E8101C0`), resulting in:
     `[cellGcmSys] FIFO resync (unmapped-JUMP) 0x00014000 -> put 0xFFFFFFFF`
     followed by empty fence drains (`fence 0x0`).
3. **Phase 7 FIFO Objectives:**
   - With `wid=8` (`BrBDblBufWrkld`) and `wid=13` (`RenderJobs`) now actively executing on SPU worker threads, verify that the push buffer chunks at IO offsets `0x00014000` through `0x00238040` are properly populated with valid `NV4097` command packets.
   - Configure `$env:SPU_HOST_DEPTH_MAX = "100000"` in `tools/run_port.ps1` so the deep SPU job call chains in `Wws_Job` do not trip the default 2,000-depth recursion guard.
   - Ensure the RSX FIFO walker smoothly consumes Big Ring Buffer packets without falling back to unmapped-JUMP resync.

---

## 2. GCM IO Mapping & Offset Translation Verification

God of War: Ascension maps several large memory regions into GCM IO space:
- Aperture 1: EA `0x11000000` $\rightarrow$ IO `0x00000000` (size `0x100000`, 1 MB context/ring header).
- Aperture 2: EA `0x11100000` $\rightarrow$ IO `0x00100000` (size `0x5100000`, 81 MB command buffers and dynamic vertex/index ring).
- Aperture 3: EA `0x16200000` $\rightarrow$ Main memory map (size `0x2000000`, 32 MB asset staging).
- Local Memory: RSX local VRAM at EA `0x20000000+`.

### Offset Verification Protocol:
- Verify that `gcm_io2ea(io)` and `gcm_ea2io(ea)` in `cellGcmSys.c` cleanly translate every RSX `JUMP` (type 1, `0x20000000 | target`) and `CALL` (type 2, `0x40000000 | target`) instruction across the full 82 MB address range.
- Validate using `./ps3recomp/tools/test_gcm_io_mappings.py` and `test_gcm_map_flags.py` that flagged IO mappings (`cellGcmMapEaIoAddressWithFlags`, flag `0x2`) preserve memory coherency between SPU DMA writes and RSX FIFO reads.

---

## 3. Vertex & Fragment Shader Pipeline (`RSX_LIVE_DRAW=1`)

Once valid command packets flow through the FIFO, the RSX command processor translates `NV4097` methods into Direct3D 12 draw calls:

1. **Vertex Program Configuration:**
   - `NV4097_SET_TRANSFORM_PROGRAM_LOAD` / `START`: loads 32-bit microcode words into the vertex program cache.
   - `rsx_vp_decompiler.c`: translates raw RSX vertex microcode into valid HLSL (`vs_5_1`) vertex shaders.
   - `ps3recomp/tools/vp_eval.py`: CPU-side evaluator to inspect and debug decompiled vertex outputs, constants, and vertex attributes.

2. **Fragment Program Configuration:**
   - `NV4097_SET_SHADER_PROGRAM`: configures fragment shader instruction words and texture samplers.
   - `rsx_fp_decompiler.c`: decompiles fragment programs into HLSL (`ps_5_1`).
   - `rsx_d3d12_backend.c`: compiles vertex and pixel shaders via `D3DCompile` at runtime and caches Pipeline State Objects (PSOs).

3. **Boot Logos & 2D Menu Bring-Up:**
   - `IntroLoaderThread` (`tid=12`, entry `0x008159B0`) streams boot assets from `INTROCIN.PSARC` (Sony Computer Entertainment, Santa Monica Studio logo screens) and `SHELL.PSARC` (Scaleform UI).
   - Verify that `NV4097_SET_BEGIN_END` primitives (quads and triangles) rasterize into display buffer 0 (`loc0:0x00000000`), advancing beyond cleared frame buffers to visible pixels.

---

## 4. Phase 7 Deliverables & Success Gates

1. **Plan 07-01: Big Ring Buffer (`BrB`) FIFO Flow & Recursion Depth Tuning**
   - Configure `$env:SPU_HOST_DEPTH_MAX = "100000"` in `tools/run_port.ps1`.
   - Audit push buffer generation at IO offsets `0x00014000` through `0x00132040+` to ensure `Wws_Job/BrBDblBufWrkld` populates valid RSX packets.
   - Eliminate `FIFO resync (unmapped-JUMP)` at `0x00014000`.

2. **Plan 07-02: GCM IO Memory Translation & Command Packet Validation**
   - Validate IO-to-EA translation for all JUMP/CALL packets across `0x11000000 .. 0x16200000`.
   - Ensure SPU DMA writes to command buffer chunks flush coherently before the RSX FIFO walker executes them.

3. **Plan 07-03: D3D12 Shader Compilation & Boot Logo Presentation**
   - Trace and verify D3D12 PSO compilation (`LD_TRACE_PSO=1`) for `NV4097` vertex/pixel programs.
   - Validate rasterization of 2D quad/triangle draw calls from `IntroLoaderThread`.
   - Document frame presentation and shader statistics in `docs/bringup-status.md`.
