# Phase 9 Context: SaveData Callback Completion, Intro FMV (`cellSail`/Bink) & Main Menu Bring-Up

## Phase Objective
Ensure `SaveDataUtil::LoadSaveProfiles` (`tid=11`) and `cellSysutil` callbacks fire cleanly into guest memory, unblock `IntroLoaderThread` (`tid=12`) through `INTROCIN.PSARC` / `SHELL.PSARC` decompression and FMV playback (`cellSail`/Bink), and advance the engine into the interactive 3D Main Menu with pad input handling.

---

## 1. Upstream Toolchain & Graphify Invariants
- **Graphify Query**:
  `graphify query "savedata sail sysutil dialog"`
  Resolved upstream locations:
  - `cellSaveData.c` (`ps3recomp/libs/system/cellSaveData.c`)
  - `cellSail.c` (`ps3recomp/libs/codec/cellSail.c`)
  - `cellSysutil.c` (`ps3recomp/libs/system/cellSysutil.c`)
  - `cellOskDialog.c` / `cellMsgDialog.c` (`ps3recomp/libs/misc/cellOskDialog.c`, `ps3recomp/libs/system/cellMsgDialog.c`)
- **Upstream Regression Tests Verified**:
  - `ps3recomp/tools/test_savedata_user_autosave.py`: Validates `cellSaveData_register_ctx_handlers()` and ensures 9th argument (`userdata` on stack) passes to stat callback.
  - `ps3recomp/tools/test_sail_boot_callback.py`: Validates 64-bit `userParam` token preservation as callback `arg1` for `PLAYER_CALL_COMPLETED / BOOT` (major 2, minor 1).
  - `ps3recomp/tools/test_dialog_abort.py`: Validates dialog abort suppression and normal close delivery through the sysutil pump.

---

## 2. Locked Technical Specifications

### A. `SaveDataUtil::LoadSaveProfiles` (`tid=11`, entry `0x00811AA0`)
- **Autoload & Callback Verification**:
  - `cellSaveDataUserAutoLoad` / `cellSaveDataAutoLoad2` invoke both `funcStat` (OPD `0x00811AC0`, `0x00811AD0`) and `funcFile` guest callbacks via `ppu_guest_call`.
  - Marshal big-endian `CellSaveDataCBResult` (`result = CELL_SAVEDATA_CBRESULT_OK_NEXT` / `OK_LAST` / `ERR_NO_DATA`).
  - On first boot without an existing save, `funcStat` returns `-4` (`CELL_SAVEDATA_CBRESULT_ERR_NO_DATA`), which `savedata_execute` treats as `CELL_OK` so the game's title state machine advances.
  - Persistent save files write into `./vfs/dev_hdd0/home/00000001/savedata/` (and `./gamedata/dev_hdd0/home/00000001/savedata/`).
- **Sysutil Autosave Notice Dialog**:
  - `cellSysutilCheckCallback()` drains dialog events and dispatches `CELL_SYSUTIL_DRAWING_BEGIN` (`0x0121`) / `CELL_SYSUTIL_DRAWING_END` (`0x0122`) notifications to the title's registered sysutil callback slot (slot 0) when an initial autosave notice dialog is posted.

### B. 8-Argument `harness_guest_caller` Alignment
- In `ps3recomp/include/ps3emu/guest_call.h`:
  ```c
  typedef void (*ps3_guest_caller_fn)(uint32_t opd_addr,
                                      uint64_t arg0, uint64_t arg1,
                                      uint64_t arg2, uint64_t arg3,
                                      uint64_t arg4, uint64_t arg5,
                                      uint64_t arg6, uint64_t arg7);
  ```
- In `src/boot_main.cpp` and `src/main.cpp`, `harness_guest_caller` forwards all 8 arguments (`a0..a7`) to `ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7)`.
- Eliminates argument and `userParam` truncation across all PS3 guest callbacks.

### C. `IntroLoaderThread` (`tid=12`, entry `0x008159B0` -> code `0x0041F74C`)
- **Streaming & Decompression**:
  - Mounts `SHELL.PSARC` (fd 5) and `INTROCIN.PSARC` (fd 6).
  - Background decompression worker threads (`tid=5..8`) consume Edge Zlib 64 KB compressed chunks from `LFQueue` (`0x15E66A80`) via host `gow_inflate` and post completion bits to `EventFlag` (`0x15E66A00`).
  - Configured `$env:GCM_GET_EQ_PUT = "1"` so `CellGcmControl->get` (+0x04) tracks `put` (+0x00) during command buffer flushes, unblocking wait loop `loc_003E4ABC` (`timer_usleep(30 us)` at `0x003E4890`).
- **Intro FMV Playback (`cellSail` / Bink)**:
  - If `cellSail` is invoked (`cellSailPlayerBoot`, `cellSailPlayerOpenStream`, `cellSailPlayerStart`) for boot logos (`BOOT0_EN.GTF`, Santa Monica cinematic intro), ensure stream state transitions cleanly to `CELL_SAIL_PLAYER_STATE_STOP` / EOF (or renders video frames via RSX texture upload) so the engine advances straight into the 3D Main Menu.
  - If software Bink or SPU video jobs execute, ensure completion flags are posted to unblock menu initialization.

### D. Scaleform 3D Main Menu Presentation & Pad Input
- Decompressed `SHELL.PSARC` assets instantiate the Scaleform GFx UI shell.
- Direct3D 12 LiveDraw engine presents 1280x720 frames across 3 display buffers (`0x00000000`, `0x00384000`, `0x00708000`).
- Reference queue drains sequential frame fences past `0x0172FFB0`.
- `cellPad` XInput backend polls controller input and forwards `CELL_PAD_CTRL_CROSS` / `START` to navigate menu options (New Game, Options).

---

## 3. Inviolable Laws Check
- **Law 1**: Verified `cellSaveData.c`, `cellSail.c`, `cellSysutil.c`, and upstream tests (`test_savedata_user_autosave.py`, `test_sail_boot_callback.py`, `test_dialog_abort.py`).
- **Law 2**: Sub-repos `./ps3recomp/` and `./twistedmetal/` remain strictly read-only.
- **Law 3**: `src/gen/` remains completely untouched.
- **Law 4**: Windows host cross-platform PowerShell and Python automation scripts. Release mode build without full `/Zi` PDBs.
- **Law 5**: Deterministic CLI instructions with deterministic `<verify>` commands.
