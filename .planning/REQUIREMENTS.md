# REQUIREMENTS.md — God of War: Ascension Port

## Functional Requirements

### FR-1: Host Toolchain & Binary Inventory
- **FR-1.1:** `clang-cl`, Ninja, CMake ≥ 3.20, and Python 3.9+ with `pycryptodome` must be installed and on PATH.
- **FR-1.2:** `ps3recomp_runtime.lib` must be built from `./ps3recomp/` source using `clang-cl` + Ninja.
- **FR-1.3:** `EBOOT.ELF` (8.2 MB) must be parsed by `elf_parser.py` to produce `imports.json` and section/segment inventory.
- **FR-1.4:** A `find_code_end.py` tool must determine the exact hex boundary where `.text` ends and `.rodata`/data begins, to pass as `--code-end` to the lifter, preventing false-branch function blowup in data regions.
- **FR-1.5:** `ppu_loader.py` must produce `EBOOT.functions.json`, `EBOOT.imports.json`, `EBOOT.image.json`, `EBOOT.loader.json`.
- **FR-1.6:** `find_functions.py --seed-json` must augment OPD-detected functions with heuristic scanner results.
- **FR-1.7:** `ppu_lifter.py --hle-stubs` must generate lifted C++ chunks into `./src/gen/` without translating import trampolines literally.
- **FR-1.8:** A `relift.py` Windows-native wrapper must automate the full lift pipeline (loader → find_functions → lifter → gen_hle_nids) in one command.

### FR-2: NID Triage & Native Link
- **FR-2.1:** `imports.json` must be diffed against `./ps3recomp/runtime` HLE coverage to identify missing NIDs.
- **FR-2.2:** All PSN/multiplayer modules (`sceNp`, `sceNp2`, `sceNpCommerce2`, `cellSysutilAvc2`, `cellVoice`) must be stubbed returning `CELL_OK` in `./src/compat/psn_stubs.cpp`.
- **FR-2.3:** `./CMakeLists.txt` must compile and link `build/gow_ascension.exe` with 0 unresolved symbols.
- **FR-2.4:** `src/gen/` must compile with `/O2` and without `/Zi` to stay within disk budget.
- **FR-2.5:** `gen_hle_nids.py --all` must generate `src/gen/ppu_hle_nids.cpp` mapping all resolved NIDs to HLE handlers.

### FR-3: SPU Extraction & Lifting
- **FR-3.1:** `extract_spu_images.py` must extract all embedded 128-bit Cell SPU ELF images from `EBOOT.ELF`.
- **FR-3.2:** Santa Monica Studio's SPURS `JobQueue` / `libedgegeom` / Edge Zlib binaries must be identified and catalogued.
- **FR-3.3:** `spu_lifter.py` must lift all SPU modules to C and place output in `./src/spu_gen/`.
- **FR-3.4:** Each SPU image must be registered in a runtime dispatch table (via `gen_spu_workloads.py` or manual registration).
- **FR-3.5:** Standalone C++ unit tests must validate each SPU job produces correct output for known inputs.

### FR-4: CRT Init & FIOS Bring-Up
- **FR-4.1:** `gow_ascension.exe` must boot with `PS3_VFS_ROOT=./vfs` (or external asset path) through CRT init (`sys_initialize_tls`, mutex creation).
- **FR-4.2:** The executable must enter `main()` and the game's own `initHardware` equivalent.
- **FR-4.3:** Sony FIOS `.wad` archive mounting must succeed (applying the `sys_lwcond_t` struct layout fix from Twisted Metal).
- **FR-4.4:** `cellGcmInit` must complete with IO memory mapping and display buffer registration.
- **FR-4.5:** A `trace_diff.py` tool must compare native execution logs against RPCS3 oracle logs to diagnose stalls.

### FR-5: RSX-to-D3D12 & SPURS Frame Sync
- **FR-5.1:** `RSX_LIVE_DRAW=1` must enable the D3D12 backend for live RSX command translation.
- **FR-5.2:** RSX buffer flips must connect to D3D12 presentation (swap chain present).
- **FR-5.3:** RSX labels (NV406E OFFSET/ACQUIRE/RELEASE) must be implemented for SPURS-to-RSX synchronization.
- **FR-5.4:** 2D UI (loading screens, menus) must be visible through the D3D12 backend.
- **FR-5.5:** 3D rendering must produce visible geometry (even if shaders/textures are incomplete).

## Non-Functional Requirements

### NFR-1: Disk Space
- Total build artifacts (gen code + object files + executable) must not exceed 35 GB on C:.
- `src/gen/` compilation must use `/O2` without `/Zi` PDB generation.
- Build intermediates must be prunable via `cmake --build build --target clean`.

### NFR-2: Build Performance
- Full rebuild should complete in < 60 minutes on the host machine.
- Incremental rebuilds (editing `hle_extra.cpp` only) should complete in < 30 seconds.

### NFR-3: Portability
- All automation scripts must be cross-platform Python (`.py`) or PowerShell (`.ps1`).
- No Bash-only scripts in `./tools/`.

### NFR-4: Deterministic Plans
- Every PLAN.md task must include exact Windows CLI commands, exact relative paths, and a `<verify>` command.
- Plans are written by Claude Opus 4.6 and executed by Gemini Flash 3.8 High without ambiguity.

### NFR-5: Knowledge Graph
- All agents must query `graphify query` before writing new build scripts, NID macros, or HLE handlers.
- `graphify update .` must be run after significant code changes.

## Acceptance Criteria by Phase

| Phase | Gate Criteria |
|---|---|
| **Phase 1** | `ps3recomp_runtime.lib` exists; `imports.json` generated; `src/gen/` populated with lifted C++; `relift.py` runs end-to-end |
| **Phase 2** | `build/gow_ascension.exe` links with 0 unresolved symbols; `psn_stubs.cpp` covers all PSN NIDs |
| **Phase 3** | All SPU images extracted and lifted; dispatch table registered; at least 1 SPU unit test passes |
| **Phase 4** | Executable boots through CRT init and enters `main()`; FIOS mounts VFS; `cellGcmInit` completes |
| **Phase 5** | D3D12 window opens; RSX commands flow; at least a 2D UI element is visible on screen |
