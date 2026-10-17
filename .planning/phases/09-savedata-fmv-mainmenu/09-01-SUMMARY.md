# 09-01 SUMMARY: SaveData Callback Completion & 8-Argument Guest Caller Verification

## Status: COMPLETE

## Accomplishments
1. **8-Argument Guest Caller Bridge**:
   - Replaced 4-argument prototype with full 8-argument signature in `src/boot_main.cpp` and `src/main.cpp`:
     ```cpp
     typedef void (*ps3_guest_caller_fn)(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                                         uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
     static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                                      uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
     {
         ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
     }
     ```
   - Eliminates parameter truncation for all PS3 HLE guest callbacks (`cellSail`, `cellSaveData`, `cellSysutil`).

2. **`SaveDataUtil::LoadSaveProfiles` (`tid=11`) Verification**:
   - Dispatches `cellSaveDataAutoLoad2` for `BLES00000-SYSDATA` and `BLES00000-PROFDATA-0` through `BLES00000-PROFDATA-3`.
   - `funcStat` OPD `0x00811AC0` and `0x00811AD0` receive valid `isNew=1` parameters and return `CELL_SAVEDATA_CBRESULT_ERR_NO_DATA` (-4).
   - Handled cleanly as new save data profiles; thread exits with status 0 (`sys_ppu_thread_exit(tid=11 status=0)`).
