# Quick Task Plan: Fix FIOS Boot Progression & Stack Identification

## Problem Statement
1. `sys_process_is_stack` (`0x4F7172C9`) currently returns `0` (`false`) hardcoded in `src/compat/ascension_hle.cpp`. `main` thread (`tid=1`) called it 6,058 times passing `0x0FEFEC10` (which is in `main`'s stack range `0x0E000000..0x0FFFFFFF`), causing FIOS to treat stack-allocated wait descriptors incorrectly.
2. `fios mediathread 2` (`tid=2`) is opening `/dev_hdd1/cache/BLES00000/fios/cache.idx` and `cache.dat` 883+ times doing 512-byte reads, keeping `main` trapped in a synchronous FIOS wait loop.
3. Compare HDD cache integrity vs disabling `/dev_hdd1` caching to allow direct `.PSARC` streaming from `E:`.
4. Progress boot into `IntroLoaderThread` (`tid=12`), `SaveDataUtil::LoadSaveProfiles` (`tid=11`), and RSX draws.

## Task Breakdown

### Step 1: Implement Correct `sys_process_is_stack`
- In `src/compat/ascension_hle.cpp`, update `sys_process_is_stack_nid` to check if `r3` falls within valid PPU stack ranges:
  - `main` thread stack: `0x0E000000u .. 0x0FFFFFFF` (stack top `0x0FF00000`)
  - worker thread stacks: `0xD0000000u .. 0xDFFFFFFF` (allocated from `VM_STACK_BASE`)
- Return `1` if within either range, otherwise `0`.

### Step 2: Diagnose & Handle `/dev_hdd1/cache/` (HDD FIOS Cache)
- Enable `FS_CALLER=1` in `tools/run_port.ps1` to log the exact `cellFs*` callers.
- Check behavior with `sys_process_is_stack` fixed.
- If FIOS is still trapped in the 883+ open loop of `cache.idx`:
  - Test moving/clearing `vfs/cache/BLES00000/fios/` or returning `CELL_FS_ENOENT` on `cache.idx` so FIOS bypasses HDD cache and streams directly from `E:\PS3_GAME\USRDIR\*.PSARC`.

### Step 3: Build & Run 40s Verification
- Compile with `cmake --build build`.
- Run `powershell -ExecutionPolicy Bypass -File .\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 40`.
- Verify boot progress:
  - `main` completes FIOS init.
  - Spawns threads up to `tid=11` and `tid=12` (`IntroLoaderThread`).
  - PSARCs mounted (`GAME.PSARC`, `SHELL.PSARC`, `INTROCIN.PSARC`).
  - Report status of `tid=1`, `tid=11`, `tid=12`, `cellSail`/`cellPamf`/`cellSaveData`, and NV4097 FIFO methods.

### Step 4: Finalize Patch & Documentation
- Save patch as `patches/fios-boot-progression.patch`.
- Document findings in `.planning/quick/20261017-fix-fios-boot-progression/SUMMARY.md` and update `.planning/STATE.md`.
