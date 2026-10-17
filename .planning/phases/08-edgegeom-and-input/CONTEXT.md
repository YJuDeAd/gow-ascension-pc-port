# Phase 8: `libedgegeom` 3D Geometry Output & XInput Controller Wiring — Context

**Phase:** 08-edgegeom-and-input  
**Milestone:** 2 — Wws_Job Engine, RSX Ring Buffers & Playable Menus  
**Status:** Locked & Ready for Planning  
**Planner & Executor:** Gemini Flash 3.8 High  

---

## 1. `libedgegeom` SPU Mesh Pipeline Architecture

God of War: Ascension uses Sony's high-performance `libedgegeom` SPU geometry pipeline orchestrated through Santa Monica Studio's `Wws_Job` framework.
- **Workload Registration:** `Wws_Job/MeshProcJobs` is registered as `wid=14` with context data pointer `0x12965B80` (packed SPURS 64-bit attribute).
- **SPU Geometry Stages:**
  1. **Frustum & Occlusion Culling:** Cull non-visible submeshes at SPU local store speeds.
  2. **Blend Shapes & Skinning:** Compute matrix transforms and vertex blending for Kratos, enemies, and interactive environmental elements.
  3. **Index & Vertex Decompression:** Decompress edge-compressed vertex buffers (quantized positions, packed normals, octahedral tangents, half-precision UVs).
- **DMA Writeback (`mfc_put` / `mfc_putl`):**
  - Staged vertex and index buffers are DMA'd back to guest main memory mapped into GCM IO space (`0x11000000 .. 0x16200000`).
  - Upon job completion, `MeshProcJobs` updates RSX synchronization labels via `cellGcmGetLabelAddress` or event flags, unblocking the RSX command processor.

---

## 2. Vertex Attribute Strides, Endianness & D3D12 Input Layouts

D3D12 reads vertex buffers produced by `libedgegeom` directly from mapped host memory:
- **Attribute Streams:**
  - Stream 0: Position (`DXGI_FORMAT_R32G32B32_FLOAT` or 16-bit fixed point)
  - Stream 1: Normals / Tangents (`DXGI_FORMAT_R8G8B8A8_SNORM` or `R16G16B16A16_FLOAT`)
  - Stream 2: Texture Coordinates UV0/UV1 (`DXGI_FORMAT_R16G16_FLOAT` or `R32G32_FLOAT`)
- **Endianness Contract:**
  - RSX native vertex fetchers expect big-endian data unless formatted by `libedgegeom` output shaders.
  - Audit `mfc_put` / `mfc_putl` operations to ensure that vertex buffers written to `0x11000000+` match the expected byte layout of D3D12 Input Layout descriptors in `rsx_live_draw.c` (`rsx_vertex_compact.c`).

---

## 3. Gamepad & Input Subsystem (`cellPad`) Wiring

To navigate past Santa Monica Studio / Sony Computer Entertainment legal screens and the title screen into gameplay:
- **HLE Implementation:** `ps3recomp/libs/input/cellPad.c` provides full XInput support on Windows with `cellPadGetData` and `cellPadGetInfo2`.
- **Button Mapping:**
  - `CELL_PAD_CTRL_START` (`0x0008`): Advances title screen prompt ("Press START").
  - `CELL_PAD_CTRL_CROSS` (`0x4000`): Selects menu items (New Game, Continue).
- **Automated Input Injection Profile:**
  - Enable input simulation via environment variables (`YDKJ_INJECT_PAD=1`, `YDKJ_PAD_BTN=0x0008`, `YDKJ_PAD_DELAY=5`) to allow automated unattended boot into the Prologue scene.
  - Wire XInput polling to allow interactive gamepad input via standard Xbox / DualShock / DualSense controllers on PC.

---

## 4. Phase 8 Deliverables & Success Gates

1. **Plan 08-01: `Wws_Job/MeshProcJobs` (`wid=14`) SPU Pipeline & DMA Validation**
   - Audit `wid=14` dispatch in `spu_workload_dispatch` and verify `libedgegeom` sub-jobs execute cleanly without memory corruption.
   - Verify DMA writebacks to IO-mapped main memory apertures.

2. **Plan 08-02: Vertex Attribute Endianness & D3D12 Geometry Drawing**
   - Verify vertex layout alignment and stride compatibility with D3D12 pipeline state objects.
   - Verify 3D draw calls (`NV4097_DRAW_INDEX_ARRAY` / `NV4097_DRAW_ARRAYS`) emit visible geometry.

3. **Plan 08-03: `cellPad` XInput Controller & Menu Navigation Bring-Up**
   - Verify `cellPadGetData` / `cellPadGetInfo2` pass controller state cleanly to the game thread.
   - Test advancing past the title screen into the main menu and Prologue.
