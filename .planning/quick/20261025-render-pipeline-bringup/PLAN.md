---
status: complete
created: 2026-10-25
slug: 20261025-render-pipeline-bringup
description: Intro cutscene and main menu rendering bringup on clean a6d76c8 submodule base
---

# Plan: Render Pipeline Bringup

## Task 1: Verify EBOOT Address Compatibility & Inspect relift.ps1 Pipeline
- [x] Check EBOOT addresses in ppu_switches.json, job_fragments.json, and ascension_defaults.cpp against our EBOOT.ELF.
- [x] Inspect relift.ps1, CMakeLists.txt, and mod.json in gow-ascension-pc.
- [x] Find /INTRO/BOOT_*.TXT filename in our BCAS25016 GAME.PSARC.

## Task 2: Apply MIT Patch Stack & Host Defaults on Working Branch
- [x] Create branch `ascension-render` in ps3recomp from `a6d76c8`.
- [x] Apply gow-ascension-pc patches and master series shims for VFS/pad/telemetry.
- [x] Integrate 21 environment defaults into initialization.
- [x] Implement skip-intro movie bypass in VFS / PSARC reader.

## Task 3: Generate & Compile Lifted SPU Job Fragments & Post-Proc Entry Points
- [x] Lift 47 SPU PIC fragments from our EBOOT.ELF using tools/lift_spu_pic.py.
- [x] Ensure 4 MLAA post-proc entry points (0x7170, 0x7CD0, 0x9340, 0x83A0) in spu_0001 are compiled.
- [x] Compile and link cleanly with main.cpp and dedicated QPC VBlank ticker.

## Task 4: 120s Render Verification Run & Frame Capture
- [x] Run 120s with LD_FRAME_DUMP and GOW_PROFILE=1.
- [x] Verify non-black pixels, FPS, draws, and inspect dumped PNG frames.
