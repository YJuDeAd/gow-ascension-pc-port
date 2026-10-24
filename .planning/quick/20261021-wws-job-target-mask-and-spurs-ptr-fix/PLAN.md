---
status: complete
date: 2026-10-21
description: Fix extra calculation wrap-around at target >= 32 and CellSpurs* pointer corruption in GoW Ascension
---

# Fix extra Calculation Wrap-Around at target >= 32 & CellSpurs* Pointer Corruption

## Objective
Fix `((target & 0x1Fu) << 3)` wrap-around when `target >= 32` across `hook_func_00466B88`, `hook_func_00466E28`, `brbDriver`, and `WWS-MAIN-UNBLOCK` in `src/compat/ascension_hle.cpp`, expand safety bound from 32 to 512, ensure `mgr_ea + 4` is never overwritten with 1, and ensure `completed_val` matches or exceeds `g_ppu_main_ctx->gpr[5]` with `vm_write32(r5 + 8, cur_b)`.

## Tasks
1. Update `src/compat/ascension_hle.cpp`:
   - Replace `((target & 0x1Fu) << 3)` with `((uint32_t)target << 3)` in all 4 places.
   - Increase safety bound `extra < 32` to `extra < 512`.
   - Remove `vm_write32(mgr_ea + 4, 1)` and `vm_write32(r31_ptr + 4, 1)`.
   - In `WWS-MAIN-UNBLOCK`, handle `mlr == 0x0046700Cu || mlr == 0x00467030u`, ensure `completed_val >= gpr[5]`, write `vm_write16(hdr, completed_val)`, and set `vm_write32(r5 + 8, cur_b)`.
   - In `hook_func_00466E28`, set `vm_write32(r5 + 8, buf_set)` after the loop.
2. Rebuild `gow_ascension.exe`.
3. Run 180s test with `tools/run_port.ps1 -LiveDraw -TimeoutSeconds 180` and verify results.
