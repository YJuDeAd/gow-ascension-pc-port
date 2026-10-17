# Phase 1: Host Toolchain Build, Binary Inventory & Clean PPU Lift — Context

**Gathered:** 2026-10-17
**Status:** Ready for planning

<domain>
## Phase Boundary

Establish the verified Windows compilation toolchain, build the upstream `ps3recomp` runtime library, inspect and parse the God of War: Ascension PS3 executable (`EBOOT.ELF`), calculate the precise executable code boundary to avoid disassembler runaway on read-only data, lift all 19,000+ PPU functions into cleanly chunked C++ source files in `src/gen/`, and document complete binary analysis metrics in `docs/binary-analysis.md`.

Multiplayer/PSN stubbing, extra HLE handlers, and linking `gow_ascension.exe` belong to Phase 2. SPU/SPURS job extraction belongs to Phase 3.
</domain>

<decisions>
## Implementation Decisions

### 1. Relative Path Anchors (Project Layout)
- All paths are strictly relative to project root `C:\IfItWorksItWorks\Project\gowassension\`.
- Toolchain scripts: `./ps3recomp/tools/elf_parser.py`, `./ps3recomp/tools/ppu_loader.py`, `./ps3recomp/tools/find_functions.py`, `./ps3recomp/tools/ppu_lifter.py`, and `./ps3recomp/tools/gen_hle_nids.py`.
- Upstream reference files: `./twistedmetal/tools/` and `./twistedmetal/CMakeLists.txt` (read-only per LAW 2).
- Target input binary: `./vfs/PS3_GAME/USRDIR/EBOOT.ELF` (8.2 MB ELF64 PPC64 BE).
- Machine-generated C++ output: `./src/gen/` (strictly generated, never hand-edited per LAW 3).

### 2. Plan 01-01: Host Toolchain Verification & ps3recomp Runtime Build
- Verify the complete Windows toolchain:
  - Clang compiler: `clang-cl --version` or `clang --version` (LLVM-MinGW UCRT)
  - Build system: `cmake --version` (>= 3.20) and `ninja --version`
  - Python runtime: `python --version` (>= 3.10) with `pycryptodome` (`python -c "import Crypto"`)
- Configure and build `./ps3recomp` into `./ps3recomp/build` in Release configuration (`-DCMAKE_BUILD_TYPE=Release -G Ninja`).
- Verify that `ps3recomp_runtime.lib` (or `libps3recomp_runtime.a`) is built and non-empty before touching any game binary.

### 3. Plan 01-02: ELF Parsing, Imports Extraction & Exact Boundary Discovery
- Run `python ./ps3recomp/tools/elf_parser.py ./vfs/PS3_GAME/USRDIR/EBOOT.ELF --all > ./analysis/elf_info.json`.
- Extract imported firmware NIDs to `./imports.json` (`python ./ps3recomp/tools/elf_parser.py ./vfs/PS3_GAME/USRDIR/EBOOT.ELF --imports > ./imports.json`).
- Write `./tools/find_code_end.py` to inspect ELF program headers, section headers (`.text`, `.opd`, `.rodata`), and section attributes (`SHF_EXECINSTR`, `SHF_ALLOC`).
- Determine the exact `--code-end` hex boundary where `.text` ends and `.rodata` begins (`0x0062C4CC`) to prevent false-branch disassembler runaway on read-only data strings and lookup tables.

### 4. Plan 01-03: Automated Relifting Pipeline & Documentation
- Write `./tools/relift.py` as a Windows-native Python automation wrapper modeled on `./twistedmetal`:
  - Runs `ppu_loader.py` to extract `.opd` function descriptors into `analysis/EBOOT.functions.json` and imports into `analysis/EBOOT.imports.json`.
  - Runs `find_functions.py` heuristic scanning seeded with OPD functions to recover unreferenced internal functions into `analysis/functions.json`.
  - Runs `ppu_lifter.py` passing `--code-end 0x0062C4CC`, `--hle-stubs`, `--chunks 8`, and `--jobs` parallel workers into `./src/gen/`.
  - Runs `gen_hle_nids.py --all --out ./src/gen/ppu_hle_nids.cpp` to generate the complete HLE NID dispatch table.
- Verify that total disk footprint of `./src/gen/` is well under the 600 MB threshold (actual ~122 MB across 8 chunks + table + NID dispatch).
- Record full binary metrics in `./docs/binary-analysis.md` (ELF headers, section table, OPD entry points, recovered function count, chunk sizes, and import breakdown).

### The Planner & Executor's Discretion
- Chunk split count (default 8 chunks balances Clang compilation parallelism and link-time memory).
- Internal data structure design for `find_code_end.py` (pure standard-library `struct` and `pathlib`, no external dependencies).
</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing:**

### Upstream ps3recomp Documentation & Tools
- `ps3recomp/docs/GAME_PORTING_GUIDE.md` — Authoritative guide: Phase 3 (Analysis), Phase 4 (Disassembly), Phase 5 (Code Generation), `--hle-stubs` requirement, and NID table generation.
- `ps3recomp/tools/elf_parser.py` — ELF parser CLI syntax and `--imports` / `--all` flags.
- `ps3recomp/tools/ppu_loader.py` — OPD descriptor parser syntax and output JSON format.
- `ps3recomp/tools/find_functions.py` — Heuristic function recovery syntax and `--seed-json` usage.
- `ps3recomp/tools/ppu_lifter.py` — Lifter CLI syntax, `--code-end`, `--hle-stubs`, and `--chunks` usage.

### Upstream Reference Port
- `twistedmetal/CMakeLists.txt` — Reference build structure and flags.
- `twistedmetal/docs/bringup.md` — Section on lifter boundary and code-end necessity.

### Project Rules
- `AGENTS.md` — The 5 Inviolable Project Laws (LAW 1: Graphify first, LAW 2: Read-only sub-repos, LAW 3: Never hand-edit `src/gen/`, LAW 4: 45 GB disk guardrail, LAW 5: Opus 4.6 planner -> Flash 3.8 High executor handshake).
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `tools/find_code_end.py` — Standalone ELF64 PPC64 BE section parser that computes the last executable section end address (`0x0062C4CC`).
- `tools/relift.py` — Full pipeline orchestrator for PPU code generation.

### Established Patterns
- Big-endian PPC64 struct unpacking using Python `struct.unpack('>Q', ...)` for ELF headers and section headers.
- Splitting generated C++ functions across N chunks (8 chunks) so C++ compilers do not exhaust RAM or choke on 20,000 functions in a single compilation unit.

### Integration Points
- Input: `vfs/PS3_GAME/USRDIR/EBOOT.ELF`
- Output: `src/gen/ppu_recomp_000.cpp` ... `ppu_recomp_007.cpp`, `ppu_recomp_table.cpp`, `ppu_recomp.h`, `ppu_hle_nids.cpp`
- Artifacts: `imports.json`, `analysis/*.json`, `docs/binary-analysis.md`
</code_context>

<specifics>
## Specific Ideas
- "Ensure `--code-end 0x0062C4CC` is always enforced so read-only data strings and floating-point constants aren't interpreted as PPC instructions."
- "Keep `src/gen/` under 600 MB to protect the ~38 GB available on C:."
</specifics>

<deferred>
## Deferred Ideas
- NID triage and PSN stubbing (`src/compat/psn_stubs.cpp`) — Phase 2.
- SPU and SPURS job extraction (`tools/spu_lifter.py`) — Phase 3.
- Native executable link (`gow_ascension.exe`) — Phase 2.
</deferred>

---

*Phase: 01-binary-inventory*
*Context gathered: 2026-10-17*
