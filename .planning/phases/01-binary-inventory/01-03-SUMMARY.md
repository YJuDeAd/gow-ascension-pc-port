# Plan 01-03 Summary: PPU Code Lifting & Binary Analysis Documentation

**Phase:** `01-binary-inventory`  
**Plan:** `03`  
**Execution Date:** 2026-10-17  
**Status:** Completed & Verified  

---

## 1. Accomplishments

1. **Automated PPU Lifting Pipeline:**
   - Created and executed `./tools/relift.py`.
   - Executed `ppu_loader.py`: extracted 7,556 OPD function descriptors and import table.
   - Executed `find_functions.py`: heuristic disassembly recovered 11,643 unreferenced functions (19,199 total functions cataloged).
   - Executed `ppu_lifter.py` with `--code-end 0x0062C4CC`, `--hle-stubs`, and `--chunks 8`.
   - Executed `gen_hle_nids.py`: generated complete HLE NID table in `src/gen/ppu_hle_nids.cpp` (1,195 handlers across 93 modules).
   - Total lifted functions: **19,570 functions** (including HLE stub trampolines and 676 tail-entry wrappers).

2. **Source Generation & Disk Space Guardrail (LAW 3 & 4):**
   - Output directory: `src/gen/`
   - Generated files count: 11
   - Total disk footprint: **122.25 MB** (well below the 600 MB guardrail).
   - Chunk distribution:
     - `ppu_recomp_000.cpp`: 16.00 MB (16,774,457 bytes)
     - `ppu_recomp_001.cpp`: 15.85 MB (16,623,153 bytes)
     - `ppu_recomp_002.cpp`: 15.55 MB (16,306,914 bytes)
     - `ppu_recomp_003.cpp`: 15.37 MB (16,121,831 bytes)
     - `ppu_recomp_004.cpp`: 15.48 MB (16,231,284 bytes)
     - `ppu_recomp_005.cpp`: 13.88 MB (14,558,009 bytes)
     - `ppu_recomp_006.cpp`: 14.15 MB (14,839,433 bytes)
     - `ppu_recomp_007.cpp`: 14.01 MB (14,691,009 bytes)
     - `ppu_recomp_table.cpp`: 1.06 MB (1,108,979 bytes)
     - `ppu_recomp.h`: 0.74 MB (771,880 bytes)
     - `ppu_hle_nids.cpp`: 0.15 MB (159,789 bytes)

3. **Technical Metrics Recorded (in `docs/binary-analysis.md` and `STATE.md`):**
   - Exact `.opd` function count: **7,556**
   - Exact `--code-end` boundary hex address: **`0x0062C4CC`**
   - Lifted function count: **19,570**
   - Firmware imports: **345** across 25 libraries
   - Unresolved NIDs: **57** missing (258 covered in upstream runtime, 30 PSN stubs, 57 custom stubs/HLE)

---

## 2. Verification Command Results

- Total size and file count check:
  ```powershell
  $genFiles = Get-ChildItem -Path ./src/gen/*.cpp, ./src/gen/*.h
  $totalBytes = ($genFiles | Measure-Object -Property Length -Sum).Sum
  $totalMB = [math]::Round($totalBytes / 1MB, 2)
  # Result: Generated 11 files, Total Size: 122.25 MB -> VERIFY_LIFT_SUCCESS
  ```
- File presence and chunk count check:
  ```powershell
  $chunks = Get-ChildItem ./src/gen/ppu_recomp_*.cpp
  # Result: Chunks count: 9, NID table size: 159789 bytes, Function table size: 1108979 bytes -> VERIFY_GENERATED_SOURCES_OK
  ```
- Metrics documentation check:
  ```powershell
  Test-Path ./docs/binary-analysis.md
  # Result: True (7,688 bytes)
  ```
