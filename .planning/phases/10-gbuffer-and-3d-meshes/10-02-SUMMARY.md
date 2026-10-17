# 10-02 SUMMARY: `libedgegeom` SPU Geometry Vertex Format & Input Layout Mapping Audit

## Status: COMPLETE

## Accomplishments
1. **Vertex Format Mapping Audit**:
   - Audited `ps3recomp/libs/video/rsx_vertex_formats.h` and verified:
     - `RSX_VERTEX_TYPE_F` (float32) $\rightarrow$ `DXGI_FORMAT_R32_FLOAT` (1), `DXGI_FORMAT_R32G32_FLOAT` (2), `DXGI_FORMAT_R32G32B32_FLOAT` (3), `DXGI_FORMAT_R32G32B32A32_FLOAT` (4)
     - `RSX_VERTEX_TYPE_SF` (half-float16) $\rightarrow$ `DXGI_FORMAT_R16_FLOAT` (1), `DXGI_FORMAT_R16G16_FLOAT` (2), `DXGI_FORMAT_R16G16B16A16_FLOAT` (4)
     - `RSX_VERTEX_TYPE_UB` (unsigned byte normalized) $\rightarrow$ `DXGI_FORMAT_R8_UNORM` (1), `DXGI_FORMAT_R8G8_UNORM` (2), `DXGI_FORMAT_R8G8B8A8_UNORM` (4)
     - `RSX_VERTEX_TYPE_UB256` (unsigned byte non-normalized) $\rightarrow$ `DXGI_FORMAT_R8_UINT` (1), `DXGI_FORMAT_R8G8_UINT` (2), `DXGI_FORMAT_R8G8B8A8_UINT` (4)
     - `RSX_VERTEX_TYPE_S1` (signed normalized 16-bit) $\rightarrow$ `DXGI_FORMAT_R16_SNORM` (1), `DXGI_FORMAT_R16G16_SNORM` (2), `DXGI_FORMAT_R16G16B16A16_SNORM` (4)
     - `RSX_VERTEX_TYPE_CMP` (compressed 11-11-10 packed) $\rightarrow$ 4 bytes packed attribute layout.

2. **SPU Geometry (`libedgegeom`) Output Pipeline**:
   - `Wws_Job/MeshProcJobs` (`wid=14`) successfully processes SPU geometry decompression, skinning matrix application, and DMA output to GCM IO-mapped memory at `0x11029D00+`.
   - Verified that vertex stride and layout alignment match D3D12 Input Layout specifications.
