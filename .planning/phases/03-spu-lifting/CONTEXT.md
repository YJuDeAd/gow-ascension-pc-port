# Phase 3: Embedded SPU & SPURS JobQueue Extraction and Lifting — Context

**Gathered:** 2026-10-17  
**Status:** Ready for planning  

<domain>
## Phase Boundary

Extract all embedded SPU ELF images and SPURS JobModules from `vfs/PS3_GAME/USRDIR/EBOOT.ELF` into `spu_programs/`, analyze and identify their function boundaries using upstream tools (`find_spu_functions.py`, `spu_ident.py`), lift SPU assembly and MFC DMA instructions into C translation units in `src/gen/spu/` using `build_spu_workloads.py` (which wraps `spu_lifter.py` with sanitized symbol prefixes and location-independent include rewriting), generate the automated workload registration table in `src/gen/spu/spu_workloads.c` with constructor startup (`gow_spu_register_all`), wire `src/gen/spu/**/*.c` into root `CMakeLists.txt`, and verify that `build/gow_ascension.exe` links cleanly with full SPU workload registration.

CRT initialization, FIOS thread streaming, and live execution tracing belong to Phase 4. RSX rendering and D3D12 display flips belong to Phase 5.
</domain>

<decisions>
## Implementation Decisions

### 1. Plan 03-01: SPU Image & JobModule Extraction and Identification
- Use upstream `ps3recomp/tools/extract_spu_images.py` to scan `vfs/PS3_GAME/USRDIR/EBOOT.ELF` and extract embedded 128-bit SPU ELFs to `spu_programs/`.
- Discover and catalog all embedded SPU binaries:
  - `spu_0000_at_00671880.elf`: offset `0x00671880`, 56,484 bytes, 506 functions.
  - `spu_0001_at_0067F580.elf`: offset `0x0067F580`, 39,508 bytes, 874 functions.
  - `spu_0002_at_00689000.elf`: offset `0x00689000`, 41,796 bytes, 403 functions.
  - `spu_0003_at_00693380.elf`: offset `0x00693380`, 38,100 bytes, 661 functions, FNV-1a-64 fingerprint `0x00E729804474449A` (matches the exact boot SPU task dispatched by God of War: Ascension).
- Test `ps3recomp/tools/extract_jobmods.py` on USRDIR: confirm God of War: Ascension bundles all SPU code within `EBOOT.ELF` and `.wad` packages rather than loose SDAT archives.
- Run `find_spu_functions.py` across all extracted ELFs into `analysis/spu_*_functions.json` to map entry points and branch targets.
- Create `docs/spu-inventory.md` documenting file sizes, entry points, detected functions, and SPURS task roles.

### 2. Plan 03-02: SPU Code Lifting & Workload Registry Generation
- Use `ps3recomp/tools/build_spu_workloads.py` to lift all SPU ELFs from `spu_programs/` into `src/gen/spu/`:
  - Enforces unique symbol prefixes per image (`spu_0000_at_00671880_`, etc.) to eliminate cross-image C symbol collisions.
  - Rewrites deep relative `#include` directives to location-independent bare headers (`spu_context.h`, `spu_helpers.h`).
  - Emits `src/gen/spu/spu_workloads.c` with `gow_spu_register_all()` and an `__attribute__((constructor))` startup hook.
- Verify include debasing and validate syntax across all 4 lifted modules (`spu_recomp.c` and `spu_recomp.h`).
- Run standalone compile verification with Clang on each `spu_recomp.c` to guarantee zero syntax or header issues.
- Monitor disk usage: confirm `src/gen/spu/` remains compact (~3.2 MB total), well below the 45 GB host limit (LAW 4).

### 3. Plan 03-03: Root CMakeLists.txt SPU Integration & Executable Link Verification
- Update root `CMakeLists.txt`:
  - Glob `src/gen/spu/**/*.c` as `SPU_SOURCES`.
  - Include `${PS3RECOMP_DIR}/runtime/spu` in target include directories.
  - Add `${SPU_SOURCES}` to `add_executable(gow_ascension ...)`.
  - Maintain `/O2` / `-O2 -g0` optimization without full PDB generation on generated files.
- Rebuild `build/gow_ascension.exe` using CMake and Ninja.
- Verification gates:
  - Zero unresolved linker symbols.
  - Executable links cleanly and registers all 4 SPU workloads automatically at process startup.
</decisions>

<canonical_refs>
## Canonical References

### Upstream Reference Files
- `ps3recomp/tools/extract_spu_images.py` — SPU ELF extractor for embedded PPU images.
- `ps3recomp/tools/extract_jobmods.py` — WWS JobQueue module extractor.
- `ps3recomp/tools/find_spu_functions.py` — SPU function boundary detector.
- `ps3recomp/tools/spu_lifter.py` — Upstream SPU assembly and MFC DMA lifter.
- `ps3recomp/tools/build_spu_workloads.py` — Title-agnostic multi-SPU lifter and workload registry builder.
- `ps3recomp/runtime/spu/spu_workload.c` — Runtime SPU dispatch table and fingerprint resolver.
- `twistedmetal/CMakeLists.txt` — Reference CMake globbing `src/spu_gen/*.c` and SPU runtime headers.

### Project Rules & Analysis
- `AGENTS.md` — The 5 Inviolable Project Laws (LAW 1: Graphify first, LAW 2: Read-only sub-repos, LAW 3: Never hand-edit `src/gen/`, LAW 4: 45 GB disk guardrail, LAW 5: Deterministic plans & CLI verification).
- `docs/binary-analysis.md` — Section 4 binary structure and ELF section layout.
- `docs/nid-triage.md` — SPURS v2 and SPU-related NID inventory.
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ps3recomp/runtime/spu/` — Upstream SPU runtime execution engine, local store emulation, and MFC DMA queue.
- `src/compat/ascension_hle.cpp` — SPURS v2 forwarding handlers forwarding guest SPURS calls to runtime.
- `CMakeLists.txt` — Established Clang + Ninja build configuration linking static runtime.

### Established Patterns
- Auto-registration of SPU workloads via `__attribute__((constructor))` in `spu_workloads.c`.
- Debased includes referencing `<spu_context.h>` via target include path `-Ips3recomp/runtime/spu`.
- Compiling generated SPU C files with `-O2 -g0` to prevent PDB disk bloating.
</code_context>

<deferred>
## Deferred Ideas
- Edge Zlib host decompression fallback (`tm_inflate.cpp` style) — to be benchmarked against lifted SPU decomp in Phase 4.
- Live RSX D3D12 rendering pipeline — Phase 5.
</deferred>

---

*Phase: 03-spu-lifting*  
*Context gathered: 2026-10-17*
