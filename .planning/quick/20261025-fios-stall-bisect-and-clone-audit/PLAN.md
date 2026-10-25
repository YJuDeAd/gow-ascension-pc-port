# Quick Plan: FIOS Stall Diagnosis, Hook Bisection, Upstream Audit & Clone-ability Verification

## Evidence
- Runs `run_20261025_062151` and `run_20261025_071828` stall at `state=3 rem=5549104 tot=12318592` for request `0x7003A250` from wall ~10 s to the end.
- Ring buffer `0x0082B708` is consumed down to 0 ready bytes because FIOS delivers no further chunks.
- Workers `tid 2,3,4,5,8` wait on condition variables; `tid 6,7` are in `sys_timer_usleep` loops inside `func_004733F8` (`lr=0x00473558`).
- On `main` (with all 19 hooks and unblocks active), the load finished.
- With `GOW_NO_WWS_HACK=1`, all hooks are disabled and the load stalls.

## Tasks

### Task 1: What tid=1, 6 and 7 wait for
1. Instrument `tid=1`: log last 5 HLE calls, blocked object, lr, and guest function call chain via `ppu_dump_guest_stack` every 5 s during stall.
2. Disassemble `func_0049FDC4` and `func_004733F8`: report loop condition of `tid 6/7` (predicate address, compare value, writer function).
3. Every 1 s for 20 s during stall: dump `predicate_ea` + 16 words, stalled request `0x7003A250` (64 words), and scheduler queue head/tail words.

### Task 2: Bisect the hooks
1. Implement `GOW_HOOKS=<list>` in `src/compat/ascension_hle.cpp` with support for `gow_unblock_wws_464_mgr`, `gow_unblock_416_subqueues`, `gow_unblock_spurs_tasks_3a5`, `FRAME-SYNC-UNBLOCK`, and all 19 hooked PPU functions (`5a8d6c`, `3636f8`, `36298c`, `3633a4`, `3c8f54`, `466b88`, `466e28`, `466398`, `3e798c`, `375a50`, `416328`, `416600`, `416a00`, `464afc`, `464a9c`, `46630c`, `464f0c`, `3a5a94`, `3a5d00`).
2. Run 90 s quiet for:
   - All hooks on (`GOW_HOOKS=all`)
   - Each unblock / hook class alone (`wws_464`, `subq_416`, `spurs_3a5`, `frame_sync`)
   - Minimal subset that lets `state` leave 3.
3. Report table with: `rem` reaches 0, wall time, state transitions, flips/s, draw groups, nonblack surfaces, audio AddData.

### Task 3: Replace the hook with the real event
1. Analyze winning hook: memory written, function bypassed.
2. Identify real runtime event (SPURS taskset, SPU DMA, event queue, lwcond signal, Edge zlib).
3. Trace real execution path and locate where the chain stops. Implement fix if cleanly in our code or single patch hunk.

### Task 4: Their repo audit (`gow-ascension-pc`)
1. Compare `gow-ascension-pc` requirements against our ISO (`E:` `BCAS25016` Asia vs `BCES-01741` Europe).
2. Verify build setup (`.env`, `ps3kit`, `RPCS3_RSX`) and report facts.

### Task 5: Clone-ability
1. Create `docs/PATCH_ORDER.md` detailing the 44 patches in order with source and purpose.
2. Create `tools/apply_patches.ps1` to apply patches cleanly to `ps3recomp`.
3. Verify on fresh clone in temp directory: `git clone --recursive`, apply patches, build `ps3recomp` and `gow_ascension.exe`.
