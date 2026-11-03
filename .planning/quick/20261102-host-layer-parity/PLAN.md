# Plan: Host Layer Parity with gow-ascension-pc

## Objective
Make our host layer identical to `gow-ascension-pc`'s, except what is specific to our Asia disc (BCAS25016).

## Task 1: Host-Layer Diff (Read-Only)
- Inventory every file in `gow-ascension-pc` that is neither patch nor generated:
  - `host/`
  - `scripts/windows/`
  - `CMakeLists.txt` and compiler flags
  - `mods-builtin/`
  - `tools/` used at runtime / lifter
- Compare against our repo file:line.
- Generate full effective environment variable table (theirs vs ours, value by value).
- Verify which variables are actually read by `ps3recomp` via grep.

## Task 2: Apply Differences
- Copy reference files into our repo with MIT credits preserved.
- Verify guest addresses in `ppu_switches.json` against our EBOOT via `show_func.py`.
- Update `src/compat/ascension_defaults.cpp` with reference defaults + Asia disc specifics (`spu_title_nolift = nullptr`).
- Remove broken vector 0 override in `src/compat/upstream_compat.cpp`.
- Report on `ppu_switches.json` and re-lift requirements (time/disk estimates; do not re-lift yet).

## Task 3: Build & Measure
- Build `gow_ascension.exe`.
- Output `READY FOR USER VISUAL CHECK`.
- Execute 10 cold runs x 180 s smoke benchmark and record metrics.
