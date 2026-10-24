---
status: complete
task: Add auto-generated status dashboard and update README
date: 2026-10-24
---

# Quick Task Summary: Generated Status Dashboard

## Accomplishments
1. Created `docs/status.json` with hardware component weights, evidence citations, and 72 offline-safe PSN/network call exceptions with explicit reasons.
2. Implemented `tools/gen_status.py` using Python standard library only (no pip dependencies):
   - Parsed 345 imported SDK functions across 25 libraries in 5 families.
   - Cross-referenced against HLE dispatch registrations in `src/gen/ppu_hle_nids.cpp`, `src/compat/*.cpp`, and `ps3recomp/runtime/`.
   - Extracted live hardware execution telemetry from runtime logs (17 guest PPU threads, 5 lifted SPU images with 0 interpreter fallbacks, 15+ SPURS workloads, 672 D3D12 live-draw frames, 41 guest clears, 13.3 Hz VBlank).
   - Produced `docs/status-dark.svg` (69 KB) and `docs/status-light.svg` (69 KB) treemap dashboards.
   - Produced `docs/status.md` (accessible markdown tables) and `docs/status_data.json` (machine-readable coverage data).
3. Updated `README.md` with a responsive `<picture>` element linking to `docs/status.md`, instructions for regeneration, and updated status claims.
4. Verified XML validity on both SVGs and confirmed exact equality between per-group counts and headline totals (330 working, 15 partial, 0 pending out of 345 imports; 95.7% coverage).
