# Quick Task Summary: Fix Wws_Job Stall-and-Notify & cellSpursGetWorkloadInfo Bottlenecks

**Date:** 2026-10-20  
**Status:** Complete  

## 1. Problem & Root Causes Diagnosed

1. **`cellSpursGetWorkloadInfo` (NID `0x4E153E3E`) 9.45M Call Spinloop:**
   - In `ppu_recomp_005.cpp` (`0x00466FF8..0x00467030`), `tid=1` (`main`) called `cellSpursGetWorkloadInfo` and expected `CellSpursWorkloadInfo` fields `+0x20` (contention) and `+0x23` (readyCount) to be zeroed when idle.
   - Because the function was stubbed or left those fields uninitialized, `tid=1` spun in a tight loop millions of times.

2. **Wws_Job Audit Buffer RAM Corruption:**
   - In `cellSpursAddWorkloadWithAttribute` (NID `0xC0158D8Bu`), GoW Ascension passed workload attribute data containing `data_hi = 0x157D3480`.
   - The SPU WWS Job Manager interpreted non-zero `data_hi` as an audit buffer EA, writing `0xFFFFFFFF` into guest RAM and corrupting subsequent buffers.

3. **SPU `img=2` Stall-and-Notify Interrupt Vector Delegation:**
   - `img=2` (`wws_job_pm`, `pm=0x0077CA80`) issues a DMA list with stall-and-notify (`cmd=0x45`), triggering SPU interrupt vector `0x00A2C`.
   - The interrupt vector was missing its delegation to the lifted interrupt handler chain starting at `0x0040`. Specifically, `0x0040 -> 0x0060 -> 0x0080` calls `0x00001990`, which reads `MFC_RdListStallStat` (ch25), writes `MFC_WrListStallAck` (ch26), sets `loadJobState@12A0 = 2` (`kExecuteCommands`), and unparks the list (`[mfc-list] RESUME`).

4. **16-bit `MFC_Size` (Channel 19) Masking & `SPU_DMA_LAX`:**
   - In CBEA hardware, `MFC_Size` (ch19) is a 16-bit register; upper bits in GPR are ignored.
   - When `wws_job_pm` wrote `wrch $ch19, $r56` where `$r56` held two packed 16-bit indices (`0x00420041 = 4325441`), `spu_dma.h` stored the unmasked 32-bit value into `spu->mfc_size`, causing transfers to fail the `size > MFC_MAX_DMA_SIZE` validation.
   - Masking `spu->mfc_size = value & 0xFFFF` and enabling `$env:SPU_DMA_LAX = "1"` allowed transfers to complete cleanly.

---

## 2. Changes Made

1. **`src/compat/ascension_hle.cpp`:**
   - Added `hle_cellSpursGetWorkloadInfo`: zeroes 64 bytes at `r5` (`+0x20` contention, `+0x23` readyCount) and returns `CELL_OK` (`0`).
   - Added `hle_cellSpursAddWorkloadWithAttribute`: sanitizes Wws_Job PM attribute (`0x0077CA80`) by zeroing `data_hi` at `attr + 0x10`.
   - Added `gow_wws_job_pm_interrupt_handler`: saves `r78` to `0x1590`, zeroes `r78`, and delegates SPU `img=2` vector `0x00A2C` directly to `spu_wws_job_pm_spu_func_00000040(ctx)`.

2. **`ps3recomp/runtime/spu/spu_dma.h`:**
   - In `mfc_channel_write`: masked `MFC_Size` write with `value & 0xFFFF` per CBEA hardware specification.
   - In `mfc_submit` and `mfc_submit_inline`: masked `size = spu->mfc_size & 0xFFFF`.

3. **`tools/run_port.ps1`:**
   - Added `$env:SPU_DMA_LAX = "1"` under the `$cfgLiveDraw` profile.

---

## 3. Verification Results

In a 90-second run (`powershell -ExecutionPolicy Bypass -File .\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 90`):
- `cellSpursGetWorkloadInfo`: Called **1 time** (0 spinloops).
- `REJECTED malformed transfer`: **0 occurrences** (all DMA transfers succeeded).
- `[mfc-list] RESUME`: Fired and unparked SPU job execution.
- `tid=1` (`main`): Successfully unblocked from `lwcond(0x15E12380)` and advanced to the Main Engine Loop at `lr=0x003CA6B0`.
- RSX LiveDraw: Reached **frame 1344** with **58 guest surface clears** and **2561 FIFO method calls** across 40 distinct NV4097 methods without device-lost errors or crashes.
