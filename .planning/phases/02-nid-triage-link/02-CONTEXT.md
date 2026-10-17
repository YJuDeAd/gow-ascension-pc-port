# Phase 2: NID Triage, Offline PSN Stubbing & Native `.exe` Link — Context

**Gathered:** 2026-10-17  
**Status:** Ready for planning  

<domain>
## Phase Boundary

Resolve all 345 imported firmware NIDs from `imports.json`, generate offline stubs for multiplayer and PSN services (`sceNp*`, `sceNpCommerce2*`, `cellVoice*`, etc.), implement single-player HLE extensions in `src/compat/ascension_hle.cpp` (SPURS v2 forwarding, lightweight condition variables, `sys_ppu_thread_once`, VFS and display queries), construct the native entrypoint harness in `src/main.cpp`, configure root `CMakeLists.txt` targeting Clang + Ninja with disk guardrails (PDB suppression on generated code), and achieve a clean link of `build/gow_ascension.exe` with 0 unresolved symbols.

SPU jobqueue extraction and lifting belongs to Phase 3. CRT initialization and FIOS bring-up execution testing belongs to Phase 4.
</domain>

<decisions>
## Implementation Decisions

### 1. Plan 02-01: NID Triage & Offline PSN Stubs
- Cross-reference all 345 imported NIDs in `imports.json` against `ps3recomp/libs/` using Graphify and `tools/nid_triage.py`.
- Auto-generate or maintain `src/compat/psn_stubs.cpp` stubbing all multiplayer/PSN modules (`sceNp`, `sceNp2`, `sceNpCommerce2`, `sceNpTus`, `cellSysutilAvc2`, `cellVoice`, `cellSysutilNpEula`) to return `CELL_OK` (0) and set mock parameters (e.g. region `'US'`, valid context IDs).
- Write `docs/nid-triage.md` cataloging all 345 NIDs:
  - 258 covered in runtime (74.8%)
  - 30 PSN stubs (8.7%)
  - 57 single-player missing/unresolved NIDs (16.5%) requiring HLE handlers or bridges (`cellSpurs`, `cellGcmSys`, `sys_net`, `cellFs`, `sysPrxForUser`).

### 2. Plan 02-02: Single-Player HLE Stubs & Entrypoint Harness
- Create `src/compat/ascension_hle.cpp` implementing:
  - SPURS v2 forwarding (`cellSpursInitializeWithAttribute2`, `cellSpursCreateTaskset2`, `cellSpursCreateTask2`, `cellSpursJoinTask2`, `cellSpursTryJoinTask2`, etc.) forwarding to SPURS v1 implementations in runtime.
  - FIOS compatibility: `sys_lwcond_create` 32-bit EA layout fix (populating both low and high words of the 64-bit struct).
  - Initialization: `sys_ppu_thread_once` executing the initialization OPD once under a thread mutex.
  - Network status: `cellNetCtlGetState` returning `CELL_NET_CTL_STATE_IP_OBTAINED = 3`.
  - File system: `cellFsReadWithOffset` and `cellFsWriteWithOffset` bridges.
- Create `src/main.cpp` modeled on `twistedmetal/src/boot_main.cpp`:
  - Registers SEH crash handler with last HLE NID reporting.
  - Sets up virtual file system (`ppu_vfs_root`).
  - Calls `ppu_load_elf()`, `ppu_recomp_register()`, `ppu_hle_init()`, `ppu_sysprx_register()`, `ppu_fs_register()`, `gow_psn_stubs_register()`, and `ascension_hle_register()`.
  - Dispatches guest entry point via `ppu_run()`.

### 3. Plan 02-03: Root CMakeLists.txt & First Native `.exe` Link
- Create `CMakeLists.txt` in workspace root:
  - Compiles with C++20 and C17.
  - Compiles `src/gen/*.cpp`, `src/compat/*.cpp`, `src/main.cpp`, and runtime PPU bridge files (`ppu_loader.cpp`, `ppu_hle.cpp`, `ppu_sysprx.cpp`, `ppu_fs.cpp`).
  - Implements **LAW 4** (disk guardrails): compiles `src/gen/` with `/O2` / `-O2 -g0` (no full debug PDBs).
  - Links against `ps3recomp/build/ps3recomp_runtime.lib` (or `libps3recomp_runtime.a`) and Win32 system libraries (`ws2_32`, `bcrypt`, `xinput`, `ole32`, `d3d12`, `dxgi`, `dxguid`, `d3dcompiler`, `mfplat`, `mfuuid`, `winmm`).
  - Sets 256 MB main thread stack reservation (`/STACK:268435456` or `-Wl,--stack,268435456`).
- Verification gate:
  - `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` succeeds.
  - `cmake --build build` links `build/gow_ascension.exe` with 0 unresolved symbols.
</decisions>

<canonical_refs>
## Canonical References

### Upstream Reference Files
- `twistedmetal/CMakeLists.txt` — Root build system and library dependencies.
- `twistedmetal/src/boot_main.cpp` — Native PPU boot harness, crash filter, and VFS initialization.
- `twistedmetal/src/hle_extra.cpp` — SPURS v2 forwarding and `sys_lwcond_t` layout fixes.
- `twistedmetal/docs/bringup.md` — Detailed analysis of FIOS deadlocks and SPURS v2 bridging.

### Project Rules & Analysis
- `AGENTS.md` — The 5 Inviolable Project Laws (LAW 1: Graphify first, LAW 2: Read-only sub-repos, LAW 3: Never hand-edit `src/gen/`, LAW 4: 45 GB disk guardrail, LAW 5: Opus 4.6 planner -> Flash 3.8 High executor handshake).
- `docs/binary-analysis.md` — Section 4.1 NID triage and import catalog.
- `imports.json` — 345 imported NIDs from God of War: Ascension EBOOT.ELF.
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/compat/psn_stubs.cpp` — 30 offline PSN/multiplayer stubs.
- `src/hle_extra.cpp` — Proven SPURS v2 and FIOS HLE bridges.
- `src/boot_main.cpp` — Working entrypoint harness.
- `tools/nid_triage.py` — Automated import triaging against `ps3recomp` runtime.

### Established Patterns
- Linking with `-static` and `-fms-extensions` with Clang on Windows.
- Suppressing PDB generation on generated sources to protect disk space.
- Forwarding SPURS v2 taskset/task APIs directly to SPURS v1 implementations.
</code_context>

<deferred>
## Deferred Ideas
- Embedded SPU program extraction and lifting (`src/spu_gen/`) — Phase 3.
- Edge Zlib host decompression fallback — Phase 3.
- Live RSX rendering to D3D12 — Phase 5.
</deferred>

---

*Phase: 02-nid-triage-link*
*Context gathered: 2026-10-17*
