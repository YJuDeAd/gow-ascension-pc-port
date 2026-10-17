# Phase 10 Context: RSX Deferred G-Buffer Render-Target-to-Texture Resolve & libedgegeom Skinning Audit

## Phase Objective
Ensure D3D12 LiveDraw correctly manages deferred shading G-buffer render targets (`NV4097_SET_SURFACE_COLOR_*OFFSET` and `ZETA_OFFSET`) with resource state transitions (`D3D12_RESOURCE_STATE_RENDER_TARGET` $\rightarrow$ `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`) when sampled as textures (`NV4097_SET_TEXTURE_OFFSET`), and audit `Wws_Job/MeshProcJobs` (`wid=14`) `libedgegeom` vertex format layouts (`F16`, `CMP`, `UB`) for 3D character and world rendering.

---

## 1. Upstream Toolchain & Graphify Invariants
- **Graphify Query**:
  `graphify query "rsx_live_draw NV4097_SET_SURFACE_COLOR NV4097_SET_TEXTURE_OFFSET test_live_draw_sampler_key.py"`
  Resolved upstream locations:
  - `rsx_live_draw.c` (`ps3recomp/libs/video/rsx_live_draw.c`):
    - `surface_get(location, offset, want_w, want_h, want_fmt)` manages D3D12 committed resources for render targets.
    - Surface-as-texture aliasing detected at lines 7136-7165 (`slots[u] = SRV_SURFACE_BASE + sampled; g_ld_bind_surf++;`).
    - Environment flag `LD_ALIAS_DBG=1` prints `[alias-hit]` diagnostics when a texture samples an active render target surface.
  - `rsx_vertex_formats.h` (`ps3recomp/libs/video/rsx_vertex_formats.h`):
    - Maps `RSX_VERTEX_TYPE_F` (`R32_FLOAT`), `RSX_VERTEX_TYPE_SF` (`R16_FLOAT`), `RSX_VERTEX_TYPE_UB` (`R8_UNORM`), `RSX_VERTEX_TYPE_UB256` (`R8_UINT`), and `RSX_VERTEX_TYPE_S1` (`R16_SNORM`).
  - `ps3recomp/tools/test_live_draw_sampler_key.py`:
    - Regression test verifying sampler descriptor key decoding (`PASS`).

---

## 2. Locked Technical Specifications

### A. Deferred Shading Render-Target Aliasing & G-Buffer Resolve
- **Render Target to Texture Sampling**:
  - God of War: Ascension renders scene geometry into offscreen RSX color/depth targets (`NV4097_SET_SURFACE_COLOR_AOFFSET` / `BOFFSET` / `COFFSET` / `DOFFSET` and `ZETA_OFFSET`) before binding those exact VRAM/IO addresses as textures (`NV4097_SET_TEXTURE_OFFSET`) for deferred lighting, ambient occlusion, and MLAA post-processing passes.
  - Offscreen results are blitted or resolved to the 3 triple-buffered display surfaces (`0x0`, `0x384000`, `0x708000`).
  - The D3D12 live-draw engine must track render targets by guest address and transition them to `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` before subsequent draws sample them.
  - Verify `g_ld_bind_surf` increments and verify with `$env:LD_ALIAS_DBG = "1"`.

### B. Vertex Attribute & `libedgegeom` SPU Geometry Audit
- **`libedgegeom` Output Inspection**:
  - `Wws_Job/MeshProcJobs` (`wid=14`) decompresses 3D geometry and DMAs skinned vertex attributes (positions, normals, tangents, UVs) into mapped guest memory (`0x11029D00+`).
  - Audit `NV4097_SET_VERTEX_DATA_ARRAY_FORMAT` and `NV4097_SET_VERTEX_DATA_ARRAY_OFFSET` in the live command stream.
  - Ensure half-float (`F16`, `RSX_VERTEX_TYPE_SF`), compressed normals (`CMP`, `RSX_VERTEX_TYPE_CMP`), and unsigned byte skin weights (`UB`, `RSX_VERTEX_TYPE_UB`) map to the exact matching `DXGI_FORMAT` in the D3D12 Input Layout.

### C. Live Pass Verification & Metrics Recording
- Run `./tools/run_port.ps1` with `$env:LD_ALIAS_DBG = "1"` and `-LiveDraw`.
- Verify surface allocation, alias hits, draw call counts (`DRAW_ARRAYS` / `DRAW_INDEX_ARRAY`), and frame swapchain presentations.
- Log metrics in `./docs/bringup-status.md` and `.planning/STATE.md`.

---

## 3. Inviolable Laws Check
- **Law 1**: Checked `rsx_live_draw.c`, `rsx_vertex_formats.h`, and `test_live_draw_sampler_key.py`.
- **Law 2**: Sub-repos `./ps3recomp/` and `./twistedmetal/` remain strictly read-only.
- **Law 3**: `src/gen/` remains completely untouched.
- **Law 4**: All scripts in PowerShell (`.ps1`) and Python (`.py`). Release mode build without full `/Zi` PDBs.
- **Law 5**: Deterministic CLI instructions with deterministic `<verify>` commands.
