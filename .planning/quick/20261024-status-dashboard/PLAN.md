---
status: in-progress
task: Add auto-generated status dashboard (gen_status.py, SVGs, status.md, status_data.json) and embed in README
date: 2026-10-24
---

# Plan: Auto-Generated Port Status Dashboard

## Objective
Build a status dashboard generator (`tools/gen_status.py`) using Python standard library only.
Generate dark/light SVG treemaps, accessible markdown tables, and machine-readable JSON.
Maintain weights and offline-correct exceptions in `docs/status.json`.
Embed the dashboard into `README.md` via `<picture>` element.

## Plan Steps
1. Create `docs/status.json` containing:
   - Hardware component weights (PPU, SPU, SPURS, RSX, I/O, Speed).
   - Exceptions for offline PSN/network stubs with explicit "reason" fields.
   - Evidence citations for hardware metrics.
2. Build `tools/gen_status.py` (stdlib only):
   - Parse `imports.json` (345 imports across 25 libraries in 5 families: System, GPU/SPU, Network, PSN, Media/Input).
   - Parse HLE registrations from `src/gen/ppu_hle_nids.cpp`, `src/compat/*.cpp`, and `ps3recomp/runtime/`.
   - Classify each function as working, partial, or pending based on real implementation vs stub and `docs/status.json` exceptions.
   - Parse execution log (`--from-log`) for measured hardware metrics (frames, clears, threads, SPURS workloads, SPU native hits/misses, VBlank rate, fps).
   - Render `docs/status-dark.svg` and `docs/status-light.svg` as valid self-contained XML (<200 KB, system font stack, ~900px width).
   - Render `docs/status.md` as accessible markdown tables with documented classification rules.
   - Render `docs/status_data.json` with commit hash and timestamp.
3. Update `README.md`:
   - Add Status section near top with `<picture>` element (dark/light SVGs) linking to `docs/status.md`.
   - Add concise explanation of numbers and reproduction command (`python tools/gen_status.py`).
   - Fix outdated status claims while preserving legal notices.
4. Verify:
   - `python tools/gen_status.py --from-log build/logs/run_20261024_035814_stderr.log` exits 0.
   - XML validity of both SVGs via `xml.dom.minidom`.
   - Exact sum checks: group totals sum to headline N/M.
   - Git status check: no forbidden files staged.
