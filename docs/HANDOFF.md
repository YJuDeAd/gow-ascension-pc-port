# God of War: Ascension Port Handoff

## Current Best Build
- **Target**: `build/gow_ascension.exe` (SHA256: `E3B46131DDFEBBA869B01699F180E5688A524E61654550BB28FD4D158F588140`)
- **Runtime**: `ps3recomp/build/libps3recomp_runtime.a` (SHA256: `48AE715E1FE064656057969D613CD8149834A56121A0875F56CB0339D6959C03`)
- **Upstream Runtime**: `libps3recomp_runtime.a` (SHA256: `7631D612693821736BA9DF947191895F6028A9E0C6D0C13CE9B0A0ECCE8AE7FA`)
- **How to Run**:
  ```powershell
  $env:PS3_PATCHES="Disable MLAA"; & build/gow_ascension.exe vfs/PS3_GAME/USRDIR/EBOOT.ELF
  ```
  Pass rate: 100.0% past `0x0061B9FC` (20/20 leaves at ~15s -> `0x00467B1C`).

## Stall Inventory & Status
1. `brb_StartSession` Stall (`0x0061B9FC`):
   - Status: **FIXED** (20/20 passed, 100% vs baseline 35%).
   - Root Cause: Missing basic-block merging (`--merge-chunks`) and musttail dispatch (`spu_dispatch_mt.c`).
2. Main Loop Wait (`0x00467B1C`, `func_00466E28`):
   - Status: **ISOLATED via Convergence Bisect to Component 4 (Runtime Files)**.
   - Wait Predicate: Unsigned min reduction over 7 16-bit counters at `0x70A9AF72..0x70A9AF7E` for Workload 2 (`HighestPrio6`), waiting for `min(L0..L6) >= 8`.
   - Bisect Matrix:
     - (1) Their PPU lifted code: 0/20 passed ($p=1.0$, unresolved hooks; read-only poll).
     - (2) Their defaults: 0/20 passed ($p=1.0$, stalls 30s-60s).
     - (3) Their `cellSpurs.c`: 0/20 passed ($p=1.0$, identical byte-for-byte).
     - (4) Remaining runtime files: 20/20 passed ($p=1.451 \times 10^{-11}$).
   - Root Cause: SPU channel/coherency/drain runtime subsystem (`spu_channels.c`, `spu_coherency.c`, `spu_drain.c`) required for `HighestPrio6` WWS tasks to dispatch and signal completion.

## How to Regenerate SPU Code
Run `powershell tools/relift_spu.ps1` to extract and re-lift all SPU sources from `vfs/PS3_GAME/USRDIR/EBOOT.ELF` into `src/gen/spu/` with `--merge-chunks` and `--xregs` (~158s). Then `ninja -C build`.

## Next 3 Steps
1. Land Part 3: Integrate Component 4 runtime files (`spu_channels.c`, `spu_coherency.c`, `spu_drain.c`) into `ps3recomp`.
2. Run 20 cold x 90s full validation on unified tree.
3. Run 3 x 5-minute runs to discover the next downstream wait.
