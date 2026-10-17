# Summary 08-01: `Wws_Job/MeshProcJobs` (`wid=14`) SPU Pipeline & DMA Validation

**Status:** Completed  
**Artifacts Generated / Modified:**
- Evaluated `Wws_Job` workloads across SPURS kernel dispatch.
- Verified 95 SPU DMA transfers targeting GCM IO-mapped memory aperture (`0x11029D00`).

**Key Verifications:**
- Confirmed `Wws_Job` workloads properly registered and dispatched to SPU Policy Module (image 2).
- Validated that SPU DMA `mfc_put` transfers write into the 82 MB GCM IO aperture (`0x11000000 .. 0x16200000`) without memory corruption.
- Verified RSX synchronization labels at `cellGcmGetLabelAddress` indices 0 (`0x20000000`) and 1 (`0x20000010`) are mapped and tracked.
