# Quick Task: Fix Corrupted Instruction Switches and Post-Logo Hang

**Session Slug**: `20261102-fix-relift-switches-and-hang`
**Branch**: `parity/reference-exact`
**Status**: in-progress

## Context
In commit `439b7b4`, the PPU instruction switches for Loop 3 (`0x00467010` and `0x00467034` in `func_00466E28`, `src/gen/ppu_recomp_005.cpp`) were misaligned, overwriting actual guest logic (`vm_read8` of `readyCount` and `contention` and the corresponding `cmpwi` instructions). Furthermore, injecting `li r11, 0x3FC; sc` into spin loops clobbered `gpr[3]` (overwritten by `lv2_syscall` return code `0`) and `gpr[11]`, causing register corruption.

## Tasks
1. **TASK 1: Fix Corrupted Instruction Switches & Add NOP-Only Guardrail**
   - Disassemble all 12 switch sites in `vfs/PS3_GAME/USRDIR/EBOOT.ELF`.
   - Output detailed mapping table.
   - Remove corrupted switches from `tools/ppu_switches.json`.
   - Add hard assertion in `relift.py` enforcing that yield patches only overwrite `0x60000000` (`nop`).
2. **TASK 2: Eliminate Register Clobbering in Injected Spin-Yields**
   - Audit register liveness across all loop functions.
   - Update `relift.py` post-processing to wrap all `0x3FC` yields in `{ uint64_t _r3 = ctx->gpr[3], _r11 = ctx->gpr[11]; ctx->gpr[11] = 0x3FC; lv2_syscall(ctx); ctx->gpr[3] = _r3; ctx->gpr[11] = _r11; }`.
   - Inject the non-clobbering yield into `func_00466E28` at `0x0046700C` and `0x00467030` while leaving `vm_read8` and `cmpwi` 100% intact.
   - Show side-by-side C++ diff.
3. **TASK 3: Resolve Watchdog BOOT Thread RVAs & Audit Hang Sites**
   - Resolve RVAs from `build/gow_ascension.map` / `llvm-symbolizer`.
   - Inspect loop bodies for `func_0033A0E0` (`0x0033A394`), `func_00466678` (`0x00466710`), and `func_0061B918` (`0x0061B9FC`).
   - Audit `taskset 0x70027280` wait state.
4. **TASK 4: Rebuild & Run Fast Verification Benchmark (10 Cold x 45s)**
   - Build with `ninja -C build gow_ascension` and record SHA256.
   - Benchmark 10 cold x 45s in `build/logs/parity_10_fixed/`.
   - Output comparison table vs `0198d37` and `439b7b4`.
