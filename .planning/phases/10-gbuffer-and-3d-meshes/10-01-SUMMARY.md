# 10-01 SUMMARY: D3D12 Deferred Shading G-Buffer Render-Target-to-Texture Aliasing & Transition Verification

## Status: COMPLETE

## Accomplishments
1. **D3D12 Sampler Key & Sampler Cache Verification**:
   - Upstream regression test `test_live_draw_sampler_key.py` verified: `PASS: sampler_key separates every field decode_sampler reads and nothing else`.
   - Confirms sampler state keys accurately track min/mag filter, address wrap modes (U, V, W), and min/max LOD ranges for textures and surface sampling.

2. **Render-Target-to-Texture Aliasing Architecture**:
   - `ps3recomp/libs/video/rsx_live_draw.c` automatically maps texture lookups (`NV4097_SET_TEXTURE_OFFSET`) matching active surface guest addresses (`g.surfaces[i].offset`) to `SRV_SURFACE_BASE + i`, binding them as shader resource views and incrementing `g_ld_bind_surf`.
   - Feedback sampling on self-bound render targets utilizes `surface_feedback_srv(target)` to sample a snapshot copy of the surface.
   - Resource state transitions (`D3D12_RESOURCE_STATE_RENDER_TARGET` $\rightarrow$ `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`) are tracked per render pass before draw call execution.
