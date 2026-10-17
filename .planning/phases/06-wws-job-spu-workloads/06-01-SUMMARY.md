# Summary 06-01: Wws_Job Policy Module Extraction & Lifter Pipeline

**Status:** Completed  
**Artifacts Generated:**
- `./spu_programs/wws_job_pm.bin` (12,288 bytes)
- `./spu_programs/wws_job_pm.elf` (12,524 bytes, 32-bit BE SPU ELF)
- `./src/gen/spu/wws_job_pm/spu_recomp.h` (208 functions lifted)
- `./src/gen/spu/wws_job_pm/spu_recomp.c` (function table & registration)

**Key Verifications:**
- Extracted exact 12,288-byte Policy Module block at guest EA `0x0077CA80` (`PH[0]` file offset `0x0076CA80`).
- Validated `text_fp` equals `0xC8D774963A96DCF2`, matching the runtime fingerprint from `cellSpurs`.
- Lifted 208 functions covering 3,072 SPU instructions with `spu_lifter.py --auto-functions`.
- Successfully debased include paths to `#include "spu_context.h"`.
