---
status: complete
date: 2026-10-17
slug: fix-fios-boot-progression
description: Fix sys_process_is_stack and bypass corrupt HDD FIOS cache to advance boot through all PSARCs into IntroLoaderThread
---

# Quick Task Summary: Fix FIOS Boot Progression & Stack Identification

## Accomplishments
1. **Fixed `sys_process_is_stack` (`0x4F7172C9`):**
   - Replaced hardcoded return value of `0` in `src/compat/ascension_hle.cpp`.
   - Implemented real address boundary check for both the main-thread stack (`0x0E000000u..0x0FFFFFFF`, top `0x0FF00000`) and worker-thread stacks allocated from `VM_STACK_BASE` (`0xD0000000u..0xDFFFFFFF`).
   - Verified that `sys_process_is_stack(0x0FEFEC10)` accurately returns `1` (`true`).

2. **Diagnosed & Resolved FIOS Cache Loop on `/dev_hdd1/cache/`:**
   - Analysis revealed `cache.idx` on disk was left in a corrupt state with all zeroes in the initial 512-byte header (`magic=00000000`).
   - Every iteration, FIOS executed: `cellFsOpen` -> `cellFsFstat` -> `cellFsLseek(off=0)` -> `cellFsRead(512 bytes)` -> failed magic check -> `cellFsClose`, causing 883+ redundant open/close cycles per run.
   - Disabled `PS3_HDD1_ROOT` and purged corrupt cache files. As on real PS3 hardware without an optional HDD cache partition, `/dev_hdd1/` returned `CELL_FS_ENOENT`, causing FIOS to immediately stream all assets directly from disc (`E:\PS3_GAME\USRDIR\*.PSARC`).

3. **Full Boot Progression into `IntroLoaderThread` & RSX:**
   - `main` thread completed FIOS initialization in <2 seconds.
   - Mounted `ARCHIVES`, `GAME.PSARC`, `SHELL.PSARC`, `INTROCIN.PSARC`, `RESOURCES.PSARC`, and `HECA.PSARC`.
   - Successfully spawned threads up to `tid=14`:
     - `tid=9` (`fs::RefreshGameData`)
     - `tid=10` (`SaveDataUtil::LoadUnlocksPrefs`) — completed cleanly (`exit 0`).
     - `tid=11` (`SaveDataUtil::LoadSaveProfiles`) — completed cleanly (`exit 0`).
     - `tid=12` (`IntroLoaderThread`) — actively running the intro frame loop.
     - `tid=13` (`brbDriver`)
     - `tid=14` (`brbClient`)
   - Initialized GCM and Audio (`_cellGcmInitBody`, `cellAudioPortOpen`).
   - `tid=12` actively executing flips (70+ flips across buffers 0 and 1) with 134+ guest surface clears and 5,281 total NV4097 FIFO method calls across 40 distinct methods.
