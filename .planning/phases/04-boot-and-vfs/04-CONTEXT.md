# Phase 4: CRT Init, LV2 Kernel & FIOS Asset Streaming Bring-Up — Context

**Gathered:** 2026-10-17  
**Status:** Ready for planning  

<domain>
## Phase Boundary

Advance the execution of `build/gow_ascension.exe` through PPU entry point `_initialize`, CRT initialization (thread-local storage, global constructors), `main()`, firmware module loading (`cellSysmoduleLoadModule`), game boot queries (`cellGameBootCheck`), FIOS asset streaming subsystem initialization, and archive mounting from the mounted virtual drive `E:\` (PlayStation Archives `.PSARC` / `.wad`), reaching `cellGcmInit` with zero crashes.

Phase 5 covers RSX-to-D3D12 rendering, 2D loading screen presentation, vertex/fragment program compilation, and SPURS frame-label sync.
</domain>

<decisions>
## Implementation Decisions

### 1. Model Role Alignment
- Both Planner and Executor roles are assigned to `Gemini Flash 3.8 High` across all GSD workflows.
- `CLAUDE.md`, `AGENTS.md`, and `.planning/PROJECT.md` have been updated to reflect this unified role structure.

### 2. Zero-Disk-Copy Asset Streaming from Virtual Drive `E:\`
- The decrypted game ISO (`God of War - Ascension (Asia) (En,Zh,Ko).dec.iso`) is mounted via Windows `Mount-DiskImage` to drive `E:\`.
- Runtime VFS root is set via environment variable:
  ```powershell
  $env:PS3_VFS_ROOT = "E:"
  ```
- The native executable streams all 36 GB of game assets (`ARCHIVES`, `GAME.PSARC`, `SHELL.PSARC`, `INTROCIN.PSARC`, `RESOURCES.PSARC`, `HECA.PSARC`, etc.) directly from `E:\PS3_GAME\USRDIR\` with zero bytes copied to host drive `C:`, strictly satisfying LAW 4 (45 GB disk guardrail).
- Decrypted `EBOOT.ELF` and metadata are loaded from `./vfs/PS3_GAME/USRDIR/EBOOT.ELF`.

### 3. Built-In Diagnostic Tooling for Bring-Up
- Utilize upstream diagnostic scripts in `ps3recomp/tools/`:
  - `spbalance.py`: walk lifted PPU functions in `src/gen/` to verify stack pointer balance across tail-call continuations and eliminate stack frame leaks.
  - `audit_guest_ptrs.py`: scan `src/compat/*.cpp` and `ps3recomp/libs/` to guarantee no guest pointers are dereferenced as host pointers.
  - `audit_entry_calls.py`: verify that no HLE handlers double-translate guest effective addresses.
  - `inject_spcheck.py`: instrument dynamic call sites with runtime stack pointer drift checks if stack corruption is encountered.
  - `rpcs3_probe/` and `compare/`: compare native execution traces against RPCS3 oracle logs if any spinlocks or thread deadlocks emerge.

### 4. Target Milestone for Phase 4
- Execution verification gate:
  - `./build/gow_ascension.exe ./vfs/PS3_GAME/USRDIR/EBOOT.ELF` boots through `_initialize`, CRT constructors, `main()`, `cellSysmoduleLoadModule`, `cellGameBootCheck`, spawns FIOS worker/scheduler threads, reads `.PSARC` archive headers, and advances through FIOS initialization to reach `cellGcmInit` without crashing.
</decisions>

<canonical_refs>
## Canonical References

### Upstream Reference Files
- `twistedmetal/docs/bringup.md` — Detailed analysis of FIOS deadlocks, `sys_lwcond_t` 32-bit EA layout, and SPURS taskset dispatching.
- `twistedmetal/src/boot_main.cpp` — Reference VFS derivation, D3D12 device ticker, and PPU harness.
- `ps3recomp/runtime/ppu/ppu_fs.cpp` — Virtual file system implementation and guest path resolution.
- `ps3recomp/runtime/syscalls/sys_fs.c` — `ps3_vfs_ps3game_fallback` path translation.
- `ps3recomp/tools/spbalance.py` — Static stack balance analyzer.
- `ps3recomp/tools/audit_guest_ptrs.py` — Guest pointer safety auditor.

### Project Rules & Analysis
- `AGENTS.md` — The 5 Inviolable Project Laws.
- `docs/spu-inventory.md` — SPU workload inventory documenting Edge Zlib boot task `0x00E729804474449A`.
- `docs/nid-triage.md` — Imported single-player firmware NIDs.
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `build/gow_ascension.exe` (72.61 MB) — Fully linked standalone binary with PPU and SPU static recompilation.
- `src/compat/ascension_hle.cpp` — SPURS v2 forwarding, lightweight condition variables, and system HLE bridges.
- `src/gen/spu/spu_workloads.c` — Auto-constructor registering all 4 SPU workloads (`gow_spu_register_all`).

### Established Patterns
- Virtual ISO mounting via `Mount-DiskImage -ImagePath ... -StorageType ISO`.
- Streaming assets with `$env:PS3_VFS_ROOT = "E:"`.
- Background task execution and reactive wakeup monitoring.
</code_context>

<deferred>
## Deferred Ideas
- D3D12 live rendering pipeline and 2D UI display flip — Phase 5.
- Edge Zlib host decompression fallback (`tm_inflate.cpp`) — Phase 5 optimization if needed.
</deferred>

---

*Phase: 04-boot-and-vfs*  
*Context gathered: 2026-10-17*
