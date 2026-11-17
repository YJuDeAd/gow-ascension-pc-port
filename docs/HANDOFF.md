# God of War: Ascension Port Handoff

## Current Best Build
- **Target**: `build/gow_ascension.exe` (SHA256: `690036C13201CD3048BFC6683A7DF17BACACA3EFB7C0091760A244D307ACA27C`)
- **Runtime**: `D:/gow-bilal-asia/.../libps3recomp_runtime.a` (SHA256: `7631D612693821736BA9DF947191895F6028A9E0C6D0C13CE9B0A0ECCE8AE7FA`)
- **How to Run**:
  ```powershell
  $env:PS3_PATCHES="Disable MLAA"; & build/gow_ascension.exe vfs/PS3_GAME/USRDIR/EBOOT.ELF
  ```
  Pass rate: 20/20 leave `0x0061B9FC` before 15s; 15/20 advance past `0x00467B1C` to `0x0046700C`.

## Stall Inventory & Status
1. `brb_StartSession` Stall (`0x0061B9FC`):
   - Status: **FIXED** (20/20 passed, 100% vs baseline 35%).
   - Root Cause: Missing basic-block merging (`--merge-chunks`) and musttail dispatch (`spu_dispatch_mt.c`).
   - Depth Guard: Conclusively audited; 0 depth=2000 trips across all runs.
2. Main Loop Wait (`0x00467B1C`, `func_00466E28`):
   - Status: **FIXED via Component 4 Runtime Subsystem** (20/20 passed, $p=1.451 \times 10^{-11}$).
   - Wait Predicate: Unsigned min reduction over 7 16-bit counters for Workload 2 (`HighestPrio6`), waiting for `min(L0..L6) >= 8`.
   - Root Cause: SPU channel/coherency/drain runtime subsystem (`spu_channels.c`, `spu_coherency.c`, `spu_drain.c`) required for `HighestPrio6` tasks to dispatch and signal completion.
3. Next Downstream Wait (`0x0046700C`, `func_00466E28`):
   - Status: **ACTIVE (Identified in 3x 5-min runs)**.
   - Wait Predicate: Workload 8 (`BrBDblBufWrkld`) completion poll (`cellSpursGetWorkloadInfo`), checking `readyCount == 0` (offset `0x23`) and `contention == 0` (offset `0x20`).
   - SPU Mechanism: Under `SPURS_READY_LEVEL=1`, kernel does not clear ready byte on slot fill; SPU policy module `wws_job_pm` clears it via PUTLLC at `0x2F40/0x2F7C` once job list drains.
   - Blocked Threads: `brbDriver` (`tid 13`, `lr = 0x006245B8`) and `brbClient` (`tid 14`, `lr = 0x0062308C`) awaiting buffer descriptor transitions.

## How to Regenerate SPU Code
Run `powershell tools/relift_spu.ps1` to extract and re-lift all SPU sources from `vfs/PS3_GAME/USRDIR/EBOOT.ELF` into `src/gen/spu/` with `--merge-chunks` and `--xregs` (~158s). Then `ninja -C build`.

## Next 3 Steps
1. Land runtime SPU subsystem improvements (`spu_channels.c`, `spu_coherency.c`, `spu_drain.c`) directly in tree.
2. Resolve Workload 8 (`BrBDblBufWrkld`) completion predicate and `brbDriver`/`brbClient` synchronization.
3. Advance main thread past `0x0046700C` into the title menu / gameplay engine loop (`0x00344758`).
