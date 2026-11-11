# Reference Recipe Report: gow-ascension-pc vs gowassension

**Author:** Sub-Agent A ("reference-recipe")  
**Target Output:** `docs/reports/reference-recipe.md`  
**Reference Repository:** `C:\IfItWorksItWorks\Project\gow-ascension-pc` (commit `bb96291` on branch `fix/softlock-remediation`, origin `184c318` on `origin/main`)  
**Workspace Repository:** `C:\IfItWorksItWorks\Project\gowassension` (commit `c6b3d14` on branch `main`)  
**Date:** November 11, 2026  

---

## 1. Pinned ps3recomp Commit in gow-ascension-pc

In `gow-ascension-pc`, the static recompilation runtime is referred to as **ps3kit** (a customized fork of `sp00nznet/ps3recomp`). The reference repository does not check in `ps3kit` as a submodule, but links to it as an external directory specified via environment configuration or adjacent checkout:

- **Adjacent Checkout Fallback:** [`gow-ascension-pc/CMakeLists.txt:35-44`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/CMakeLists.txt#L35-L44)
  ```cmake
  if(NOT DEFINED PS3RECOMP_DIR)
      if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ps3kit/CMakeLists.txt")
          get_filename_component(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ps3kit" ABSOLUTE)
      else()
          get_filename_component(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ps3recomp" ABSOLUTE)
      endif()
  endif()
  ```
- **Local Environment Variable:** [`gow-ascension-pc/.env:2`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/.env#L2)
  ```ini
  PS3KIT_DIR=C:\IfItWorksItWorks\Project\gowassension\ps3recomp
  ```
- **Documentation Citations:**
  - [`gow-ascension-pc/README.md:20`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/README.md#L20), [`L146`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/README.md#L146), [`L159`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/README.md#L159)
  - [`gow-ascension-pc/docs/README.en.md:20`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/docs/README.en.md#L20), [`L146`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/docs/README.en.md#L146), [`L159`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/docs/README.en.md#L159)
  - Historical commit `78e80f1c` note in `docs/STATUS.md:87`: *"On the branch `ascension-v0.15` of a private fork of ps3recomp, commits on ps3recomp v0.15.0, also kept here as `patches/` (0001-0037)..."*

### Upstream Base Commit
- **Upstream Recomp Base:** [`gowassension/docs/PATCH_ORDER.md:3`](file:///C:/IfItWorksItWorks/Project/gowassension/docs/PATCH_ORDER.md#L3)
  > *"This document defines the deterministic order, source, classification, and purpose of all patches applied on top of upstream ps3recomp base commit `a6d76c8` (commit `a6d76c895f77eb30b90a806c830544e61821b77f`)."*
- **Git Commit Inspection:**  
  `git -C ps3recomp show -s a6d76c895f77eb30b90a806c830544e61821b77f`
  ```
  commit a6d76c895f77eb30b90a806c830544e61821b77f
  Author: Ned Heller <sp00nz@gmail.com>
  Date:   Tue Oct 20 19:24:23 2026 -0700
  Subject: Merge pull request #413: unify the open PRs for v0.17.0
  Tag: v0.17.0
  ```
- **Current `gowassension/ps3recomp` Submodule State:**  
  Submodule pointer is commit `9562c751c4b1a49813917f8a3b2d29a8a0910e56` (`v0.17.0-53-g9562c751`), which incorporates the base `a6d76c8` plus the entire 44-patch series and reference patches 0045–0048 in commit `418f335f`.

---

## 2. Patch Order in `docs/PATCH_ORDER.md`

Citation: [`gowassension/docs/PATCH_ORDER.md:13-63`](file:///C:/IfItWorksItWorks/Project/gowassension/docs/PATCH_ORDER.md#L13-L63)  
Patches are stored in `patches/series/` (or `gow-ascension-pc/patches/`) and applied in strict deterministic sequence on top of `a6d76c8`:

| # | Series Filename | Class | Upstream / Component Subject | Purpose & Summary |
|---|---|---|---|---|
| 0001 | `0001-cellSpurs-the-second-generation-entry-points-God-of-.patch` | Upstream (MIT) | cellSpurs | Second-generation SPURS entry points (`cellSpurs2` APIs) called by modern titles. |
| 0002 | `0002-cellGamePatchCheck-and-LWCOND_TRACE.patch` | Upstream (MIT) | cellGame / sync | `cellGamePatchCheck` returning NOTPATCH (disc boot) and conditional variable trace logging. |
| 0003 | `0003-cellSpurs-_cellSpursLFQueuePushBody-and-spu_signal_p.patch` | Upstream (MIT) | cellSpurs | Lock-free queue push body and SPU parked line signal propagation. |
| 0004 | `0004-Apply-ps3recomp-0004-edge-zlib-task2.patch.patch` | Upstream (MIT) | SPU / Edge Zlib | Host decompression for Edge Zlib streams, SDK LFQueue line, Task2, and MapEaIoAddressWithFlags. |
| 0005 | `0005-Apply-ps3recomp-0005-gcm-offset-table.patch.patch` | Upstream (MIT) | cellGcmSys | Write IO offset table changes through to guest memory copy. |
| 0006 | `0006-Apply-ps3recomp-0006-edge-zlib-skip.patch.patch` | Upstream (MIT) | SPU / Edge Zlib | Edge Zlib HLE truncated output handling and element skip offset logic. |
| 0007 | `0007-Apply-ps3recomp-0007-gcm-park-wait.patch.patch` | Upstream (MIT) | GCM / RSX | `GCM_PARK_WAIT_MS` override for FIFO park grace and `HOTREAD16` reader diagnosis. |
| 0008 | `0008-Apply-ps3recomp-0008-spurs2-32-workloads.patch.patch` | Upstream (MIT) | cellSpurs | Expand SPURS2 per-SPU scheduler capacity to 32 workloads and STEPCAP loop dump. |
| 0009 | `0009-Apply-ps3recomp-0009-spurs-spu-diagnostics.patch.patch` | Upstream (MIT) | Diagnostics | Diagnostic instrumentation: `SPURS_KICKLOG`, `SPURS_WLDUMP(_EA)`, and `SPU_PCWATCH`. |
| 0010 | `0010-Apply-ps3recomp-0010-macos-window-frame-dump.patch.patch` | Upstream (MIT) | Platform / macOS | Offload guest off main thread for AppKit window serving; numbered Metal frame dumps. |
| 0011 | `0011-Apply-ps3recomp-0011-spurs-temparea-ready-level.patch.patch` | Upstream (MIT) | cellSpurs | SPURS kernel `tempArea` allocation, `SPURS_READY_LEVEL`, and WWS diagnostics. |
| 0012 | `0012-Apply-ps3recomp-0012-spu-evlog.patch.patch` | Upstream (MIT) | SPU | SPU event logging (`SPU_EVLOG`) for policy module interrupt paths in order. |
| 0013 | `0013-Apply-ps3recomp-0013-gcm-park-grace-jobapi-log.patch.patch` | Upstream (MIT) | GCM / SPU | GCM FIFO park grace preservation against stuck-get rule; job-API and report logs. |
| 0014 | `0014-Apply-ps3recomp-0014-spu-commit-log.patch.patch` | Upstream (MIT) | SPU | Committed `PUTLLC` reservation logging on single line under lock (`SPU_COMMIT_EA`). |
| 0015 | `0015-Apply-ps3recomp-0015-taskset-bitset-lockline.patch.patch` | Upstream (MIT) | cellSpurs | SPURS taskset bitsets read-modify-write synchronization under lock-line lock. |
| 0016 | `0016-Apply-ps3recomp-0016-gcm-acquire-wait-flip-pace-diag.patch` | Upstream (MIT) | GCM / Video | Non-stuck handling for blocked `ACQUIRE`, flip pacing, and post-process diagnostics. |
| 0017 | `0017-Apply-ps3recomp-0017-gcm-park-spu-hold.patch.patch` | Upstream (MIT) | GCM / SPURS | Hold patched park while SPURS tasks execute; throttle verbose spurs-pm polling. |
| 0018 | `0018-Apply-ps3recomp-0018-null-read-per-function.patch.patch` | Upstream (MIT) | Memory | Rate-limit null-read diagnostic dumps per guest function with register dump. |
| 0019 | `0019-Apply-ps3recomp-0019-spurs-taskset2-task-starts.patc.patch` | Upstream (MIT) | cellSpurs | One start per SPURS task, Taskset2 size and event flag conformance, SPU task startup. |
| 0020 | `0020-Apply-ps3recomp-0020-watchdog-regs-dump-unwind.patch.patch` | Upstream (MIT) | Watchdog | Diagnostic dumping: `WATCHDOG_REGS`, `WATCHDOG_DUMP`, `WATCHDOG_UNWIND`, `SPU_MFCLOG_BYTES`. |
| 0021 | `0021-Apply-ps3recomp-0021-live-draw-x16-srgb-ddx.patch.patch` | Upstream (MIT) | RSX / LiveDraw | X16 texture format support, framebuffer sRGB conversion, texture gamma, FP DDX/DDY. |
| 0022 | `0022-Apply-ps3recomp-0022-vdec-ex-movie-perf-overlay-jobm.patch` | Upstream (MIT) | Video / VDec | `cellVdec` Ex extensions, movie playback timing, perf overlay, job-module lifting support. |
| 0023 | `0023-Apply-ps3recomp-0023-spu-pic-fragments-interp-rejoin.patch` | Ascension (Port) | SPU / Lift | Position-independent SPU job code fragments, interpreter rejoin probe, PIC deltas. |
| 0024 | `0024-Apply-ps3recomp-0024-spu-x86-sse-float-frame-chain-t.patch` | Upstream (MIT) | SPU / x86 | SSE4.1 floating-point instructions (`fa`, `fs`, `fm`, `fma`, `fms`, `fnms`) and frame chain timestamps. |
| 0025 | `0025-Apply-ps3recomp-0025-vblank-qpc-callbacks-movie-diag.patch` | Upstream (MIT) | Video / VBlank | 59.94 Hz QPC VBlank ticker, one callback per VBlank interval, movie diagnostic tracing. |
| 0026 | `0026-Apply-ps3recomp-0026-spurs-wake-on-gpu-report.patch.patch` | Upstream (MIT) | SPURS | GPU report wake triggers SPURS kernel wakeup and resets scheduler idle backoff. |
| 0027 | `0027-Apply-ps3recomp-0027-sse-ops-interp-ed-job-hist.patc.patch` | Upstream (MIT) | SPU / x86 | SSE4.1 `selb`, `rotqbyi`, `shlqbyi`, float comparisons, `csflt`, MFC engine cache. |
| 0028 | `0028-Apply-ps3recomp-0028-spu-shadow-nolift-default.patch.patch` | Upstream (MIT) | SPU / Interp | SPU shadow verification (lifted vs interpreted execution) and title NOLIFT defaults. |
| 0029 | `0029-Apply-ps3recomp-0029-report-hold-missed-wake-vdec-he.patch` | Upstream (MIT) | GCM / VDec | Full-speed movie playback: bounded GPU report wait for SPURS tasks, no missed wakes. |
| 0030 | `0030-Apply-ps3recomp-0030-host-window-language-mods.patch.patch` | Upstream (MIT) | Platform / GUI | Host window management (embedding, sizing, fullscreen, icon), console language, mod support. |
| 0031 | `0031-Apply-ps3recomp-0031-spu-fragments-by-content.patch.patch` | Ascension (Port) | SPU | SPU fragments identified inside composite GETs and content hashing; 32 code slots. |
| 0032 | `0032-Apply-ps3recomp-0032-spu-fast-fp-mode.patch.patch` | Upstream (MIT) | SPU / Math | SPU float fast path in MXCSR round-toward-zero + FTZ + DAZ mode; `SPURS_MAXCONT`. |
| 0033 | `0033-Apply-ps3recomp-0033-sse-byte-ops-fma-exptex-rsx-cop.patch` | Upstream (MIT) | SPU / RSX | SSE byte operations, FMA, `EXP_TEX` texture decompression, and RSX fast copy paths. |
| 0034 | `0034-Apply-ps3recomp-0034-usleep-hr-timer.patch.patch` | Upstream (MIT) | OS / Timer | High-resolution waitable timer for microsecond sleeps (>= 200 µs); `LD_DRAW_DUMP_SKIP`. |
| 0035 | `0035-Apply-ps3recomp-0035-tex8-tex9-fp-probe.patch.patch` | Upstream (MIT) | RSX / Shaders | Routing of vertex outputs `TEX8`/`TEX9` to pixel program, FP probe, vertex array dumping. |
| 0036 | `0036-Apply-ps3recomp-0036-persp-corr-rrc-replay-noirq-pro.patch` | Upstream (MIT) | RSX / Shaders | RSX perspective-correct texture coordinate interpolation, `rrc_replay`, `SPU_NOIRQ`. |
| 0037 | `0037-Apply-ps3recomp-0037-taskset-bitset-lock-rejoin-call.patch` | Upstream (MIT) | SPURS / SPU | Taskset bitset clears under lock-line lock; `SPU_REJOIN_CALL=1` nested drain; `SPU_PCWATCH_R90`. |
| 0038 | `0038-Apply-ps3recomp-0038-inline-guest-stores.patch.patch` | Upstream (MIT) | PPU / Lifter | Inline guest-store fast paths in lifted PPU code, eliminating out-of-line helper calls. |
| 0039 | `0039-Apply-ps3recomp-0039-half2-tex-shadow-compare.patch.patch` | Upstream (MIT) | RSX / LiveDraw | `Y16_X16_FLOAT` texture decoding and depth-compare (shadow map) sampler emulation. |
| 0040 | `0040-Apply-ps3recomp-0040-upload-reserve-capture-draws-me.patch` | Upstream (MIT) | RSX / LiveDraw | Texture upload ring buffer reservation, `LD_CAPTURE_DRAWS`, `--merge-chunks` option. |
| 0041 | `0041-Apply-ps3recomp-0041-dispatch-hooks-kernel-spin.patch.patch` | Upstream (MIT) | SPU / SPURS | Per-thread SPU depth-guard ring, gated dispatch callbacks, `SPURS_KERNEL_SPIN` spinlock. |
| 0042 | `0042-Perf-panel-SPU-RSX-and-report-hold-figures-hold-only.patch` | Upstream (MIT) | Diagnostics | Live draw perf overlay lines for SPU/RSX busy percentages and report-hold states. |
| 0043 | `0043-Live-draw-cache-resident-host-allocations-for-guest-.patch` | Upstream (MIT) | RSX / LiveDraw | Cache resident host allocations for guest spans across consecutive draw commands. |
| 0044 | `0044-Promote-IRQ-poll-for-SPU-task-completion-patch-0044.patch` | Upstream (MIT) | SPU / Lifter | `spu_lifter` register promotion preserving local variables across merged loop interrupt polls. |
| 0045 | `0045-Fix-syntax-errors-resolve-conflict-markers-and-resto.patch` | Upstream (MIT) | Core / Build | Resolve build syntax errors, merge conflict remnants, and missing struct members. |
| 0046 | `0046-Restore-rsx_fp_texture_expand_units-for-rsx_draw_eng.patch` | Upstream (MIT) | RSX | Restore `rsx_fp_texture_expand_units` symbol for `rsx_draw_engine` texture fetches. |
| 0047 | `0047-lift_jobmods.py-fix-newline-syntax-error-before-pars.patch` | Ascension (Port) | Tools | Fix Python syntax error in `lift_jobmods.py` argument parsing logic. |
| 0048 | `0048-diagnostics.patch` | Ascension (Port) | SPURS / PPU | SPURS 32-workload dispatch telemetry and thread HLE call chain diagnostics. |
| 0049 | `0049-spu-stack-unwind.patch` | Ascension (Port) | SPU / Core | SPU recursion depth guard unwind to driver stack (`spu_restart_dispatch`) preventing halts. |

### Reference Patches 0045–0048 in `gow-ascension-pc`
In `gow-ascension-pc/patches/`, after 0044, four additional patches resolve softlocks:
- `ps3recomp-0045-workload-info-binkspu-0x9d4.patch` (SPURS workload shutdown / BinkSPU 0x9D4)
- `ps3recomp-0046-gcm-unmap-io-address-free.patch` (GCM IO space deallocation on unmap)
- `ps3recomp-0047-spurs-kernel-idle-sleep-and-core-pinning.patch` (10 ms idle sleep & `SPURS_PIN` affinity)
- `ps3recomp-0048-boot-main-msc-imp-getenv.patch` (`#ifdef _MSC_VER` for `__imp_getenv` compatibility)

These 4 reference patches were merged into `gowassension/ps3recomp` under commit `418f335f` (`feat(parity): apply reference patches 0045-0048 and parity KEEP items`).

---

## 3. Lifter Commands and Flags

Citation: [`gow-ascension-pc/scripts/windows/relift.ps1:22-84`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/scripts/windows/relift.ps1#L22-L84)

### A. PPU Recompilation (`ppu_lifter.py`)
Lines 22–30:
```powershell
python (Join-Path $Tools "ppu_lifter.py") (Join-Path $Work "disc\PS3_GAME\USRDIR\EBOOT.ELF") `
    --functions (Join-Path $Root "meta\functions.json") `
    --hle-stubs (Join-Path $Root "meta\EBOOT.imports.json") `
    --toc 0x81EF60 `
    --code-end 0x62C4CC `
    --chunk-lines 150000 `
    --reg-locals `
    --insn-switches (Join-Path $Root "tools\ppu_switches.json") `
    -o (Join-Path $Root "src\recomp")
```
- **Flags & Parameters:**
  - `--toc 0x81EF60`: God of War: Ascension Table of Contents register anchor.
  - `--code-end 0x62C4CC`: End boundary of executable guest code segment.
  - `--chunk-lines 150000`: Generates translation units split evenly by line count instead of arbitrary chunk counts.
  - `--reg-locals`: Translates guest PPC registers to local C variables (`_localize_regs`), keeping hot registers in host CPU registers.
  - `--insn-switches`: Passes JSON array of instruction replacement patches (`tools/ppu_switches.json`) to the lifter directly.
  - Output directory: `src/recomp`.

### B. SPU Job Modules Lifter (`lift_jobmods.py`)
Lines 31–55:
```powershell
$env:LIFT_JOBMODS_MERGE = "1"
$env:LIFT_NATIVE = Join-Path $Cfg.PS3KIT_DIR "libs\spu_hle\gowa_drawcmd\native_functions.txt"
python (Join-Path $Tools "lift_jobmods.py") `
    --jobmods (Join-Path $Work "jobmods") `
    --out (Join-Path $Root "src\spu") `
    --register (Join-Path $Root "src\spu\jobmods_register.c") `
    --symbol-prefix ascension `
    --extra-funcs-json (Join-Path $Root "tools\jobmods_extra_funcs.json")
```
- **Environment & Flags:**
  - `LIFT_JOBMODS_MERGE=1`: Lifts contiguous basic blocks into unified C functions.
  - `LIFT_NATIVE`: Binds native host replacements for Module 218 (`gowa_drawcmd`).
  - `--extra-funcs-json`: Injects function entrypoints for job modules that lack symbol exports.

### C. SPU Post-Process Lifter (`spu_lifter.py`)
Lines 56–80:
```powershell
$env:SPU_LIFT_PROMOTE_FUNCS = "9E60,B730,87D0,8E20"
python (Join-Path $Tools "spu_lifter.py") $elf `
    --auto-functions $elf `
    --symbol-prefix "spu_0001_at_0067F580_" `
    -o (Join-Path $Spu $img) `
    --merge-chunks @nat
```
- **Environment & Flags:**
  - `SPU_LIFT_PROMOTE_FUNCS`: Promotes hot SPU registers to local variables in routines `0x9E60`, `0xB730`, `0x87D0`, `0x8E20`.
  - `--merge-chunks`: Merges loop blocks into `goto` chains within single functions to avoid register spill/reload on block transitions.
  - `@nat`: Array of `--native <addr>=<symbol>` mappings from `libs/spu_hle/gowa_post/native_functions.txt`.

### D. SPU PIC Fragments Lifter (`lift_spu_pic.py`)
Lines 81–84:
```powershell
python (Join-Path $Tools "lift_spu_pic.py") `
    --elf (Join-Path $Work "disc\PS3_GAME\USRDIR\EBOOT.ELF") `
    --fragments (Join-Path $Root "tools\job_fragments.json") `
    --out (Join-Path $Root "src\spu") `
    --register (Join-Path $Root "src\spu\pic_register.c") `
    --symbol-prefix ascension
```
- Lifts position-independent SPU job code fragments dynamically injected during execution.

---

## 4. Toolchain Used (Compiler, Flags, CMake Generator)

Citations:
- [`gow-ascension-pc/scripts/windows/build.ps1:26-30`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/scripts/windows/build.ps1#L26-L30)
- [`gow-ascension-pc/scripts/windows/build-gpu.ps1:28`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/scripts/windows/build-gpu.ps1#L28)
- [`gow-ascension-pc/CMakeLists.txt:13-16`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/CMakeLists.txt#L13-L16), [`L199-L286`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/CMakeLists.txt#L199-L286), [`L348-L363`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/CMakeLists.txt#L348-L363), [`L402-L418`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/CMakeLists.txt#L402-L418)

### Build Orchestration & Compilers
- **Build System Generator:** `Ninja` (`-G Ninja`).
- **Main Game & Runtime Compiler:** LLVM `clang-cl` (`-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl`).
  - *Note:* Required for SPU indirect branches (`spu_dispatch_mt.c`) via `__attribute__((musttail))` and tail-call optimization (`CMakeLists.txt:155-159`).
- **GPU Backend Compiler (RPCS3 RSX):** Microsoft Visual C++ `cl.exe` (`-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl`).
  - *Note:* RPCS3 RSX codebase (`ps3recomp/gpu/`) requires MSVC and does not compile cleanly under `clang-cl` (`scripts/windows/build-gpu.ps1:1-2`).
- **Language Standards:** C17 (`CMAKE_C_STANDARD 17`) and C++20 (`CMAKE_CXX_STANDARD 20`).
- **Configuration Type:** `Release` (`-DCMAKE_BUILD_TYPE=Release`).

### Compilation Flags
1. **Target-Wide Clang Flags (`CMakeLists.txt:349-363`):**
   - `-Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing`
   - `-O2` under Release / RelWithDebInfo (`$<$<CONFIG:Release,RelWithDebInfo>:-O2>`)
   - `NDEBUG`
   - `/MAP` (lld-link emits `AscensionRecomp.map` for the runtime sampling profiler)
2. **Lifted PPU Sources (`CMakeLists.txt:402-418`):**
   - `/w` (all compiler warnings suppressed)
   - `/bigobj` (enables >65,536 COFF section headers)
   - `/clang:-ffp-contract=off` (disables fused multiply-add on scalar operations that PPC rounds twice)
   - `/clang:-fno-math-errno` (inlines `sqrtf` and vector math intrinsics without setting `errno`)
   - Optional: `-mavx2 -mbmi2 -mfma` (when `ASCENSION_PPU_AVX2=ON`)
   - Optional: `/clang:-march=native` (when `ASCENSION_NATIVE=ON`)
3. **Lifted SPU Sources (`CMakeLists.txt:199-286`):**
   - `-O2 -msse4.1` (probed via `check_c_compiler_flag`)
   - `/clang:-frounding-math` (preserves SPU round-toward-zero mode)
   - `/w` (suppresses verbose warnings from register promotion)
   - Defines: `SPU_USE_DISPATCH_MT`, `SPU_ASSUME_SPU_FP`
   - `SPU_XFLOAT_APPROX` (`ASCENSION_SPU_XFLOAT_APPROX=ON` default: single-instruction SSE float operations matching RPCS3 Approximate mode)
   - `SPU_LS_NO_HOOKS` (`ASCENSION_SPU_LS_HOOKS=OFF` default: strips local-store watchpoints for speed)
   - Optional: `-mfma -ffp-contract=off` (when `ASCENSION_SPU_FMA=ON`)
   - Optional: `-mavx2` (when `ASCENSION_SPU_AVX2=ON`)
4. **Link Libraries (`CMakeLists.txt:321-329`):**
   - In-tree runtime: `ps3recomp_runtime`
   - Windows subsystem imports: `dbghelp`, `user32`, `gdi32`, `winmm`, `dxguid`
   - GPU backend (if `ASCENSION_RPCS3_RSX=ON`): `ps3rsx.lib`

---

## 5. Files Needed That Are Not in the Public Repo

| Category | File / Resource | Location / Path | Purpose |
|---|---|---|---|
| **Toolkit Repository** | `ps3kit` / `ps3recomp` | Adjacent or via `PS3KIT_DIR` | Runtime static library, HLE libraries, PPU/SPU lifters. |
| **Game Disc Dump** | Decrypted `EBOOT.ELF` | `work/disc/PS3_GAME/USRDIR/EBOOT.ELF` | Source PPC64 executable for PPU lifting. |
| **Game Disc Dump** | Game Archives & Movies | `work/disc/PS3_GAME/USRDIR/*.PSARC`, `*.M2V` | Game assets (`GAME.PSARC`, `RESOURCES.PSARC`, `OPENINGINT6.M2V`). |
| **Crypto Keys** | PS3 RAP / Disc Keys | `data/keys` | Required if decrypting `EBOOT.BIN` via `decrypt_self.py`. |
| **PPU Lifted Output** | `ppu_recomp.h`, `recomp_*.cpp` | `src/recomp/` | Generated C++ source files (hundreds of thousands of lines). |
| **SPU Lifted Output** | 33 SPU Job Modules | `src/spu/jobmod_*/spu_recomp.c` | Lifted WWS SPU job modules. |
| **SPU Dispatch Registry**| `jobmods_register.c`, `pic_register.c`, `spu_workloads.c` | `src/spu/` | Runtime self-registration tables for lifted SPU code. |
| **Analysis Metadata** | `functions.json`, `EBOOT.imports.json` | `meta/` | PPU function boundaries and PRX import symbols. |
| **Extracted SPU Modules**| 33 raw binary modules | `work/jobmods/jobmod_*.bin` | Dumped SPU binaries extracted from game memory/disc. |
| **Font File** | `Cinzel-Black.ttf` | `launcher/Cinzel-Black.ttf` | Font for on-screen performance overlay (`LD_PERF_OVERLAY`). |
| **External Headers** | FFmpeg C API Headers | `FFMPEG_INCLUDE_DIR` (`libavcodec/avcodec.h`) | Required at compile-time when `PS3RECOMP_FFMPEG_RUNTIME=ON`. |
| **Graphics SDK** | Vulkan SDK headers/libs | `VULKAN_SDK` | Required when building RPCS3 RSX backend (`build-gpu.ps1`). |
| **Local Config** | `.env` | Repository root | Machine-specific paths and build switches (never committed). |
| **Sony SDK Headers** | Proprietary headers | **None** | `ps3recomp` uses clean-room open-source HLE stubs. |

---

## 6. Building with `ASCENSION_RPCS3_RSX=OFF` (No RPCS3 Code)

Citation: [`gow-ascension-pc/CMakeLists.txt:336-343`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/CMakeLists.txt#L336-L343)
```cmake
option(ASCENSION_RPCS3_RSX "Link RPCS3's RSX renderer (ps3rsx)" OFF)
set(ASCENSION_RPCS3_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../work/gpu-build" CACHE PATH
    "Build tree of ps3recomp/gpu")
if(ASCENSION_RPCS3_RSX)
    include("${PS3RECOMP_DIR}/gpu/ps3rsx.cmake")
    ps3rsx_link(${PROJECT_NAME} "${ASCENSION_RPCS3_BUILD}"
        SOURCES "${PS3RECOMP_DIR}/runtime/ppu/tests/boot_main.cpp")
endif()
```

### Build Behavior
- **Does it build with `ASCENSION_RPCS3_RSX=OFF`?**  
  **YES.** It builds cleanly and completely without any RPCS3 code. `ASCENSION_RPCS3_RSX` defaults to `OFF`.
- **Renderer Used:**  
  The native **Live Draw** Direct3D 12 backend (`libs/video/rsx_d3d12_backend.c`, activated by `RSX_LIVE_DRAW=1`), or ps3kit's native Vulkan backend (`libs/video/rsx_vulkan_backend.c` if `RSX_VULKAN=1`).
- **RPCS3 Code Exclusion:**  
  `ps3rsx.cmake` is not loaded, `ps3rsx_link` is not invoked, `ps3recomp/gpu/` is never compiled, and third-party submodules `glslang` and `asmjit` are not required.

### What is Needed from the `ps3kit` Folder in That Case
When `ASCENSION_RPCS3_RSX=OFF`, the build requires only:
1. `include/`: Recompilation headers (`ps3recomp.h`, `spu_context.h`, `ppc_context.h`, etc.).
2. `runtime/ppu/`: PPU boot harness (`ppu_loader.cpp`, `ppu_hle.cpp`, `ppu_fs.cpp`, `ppu_sysprx.cpp`, `tests/boot_main.cpp`).
3. `runtime/spu/`: SPU execution engine (`spu_dispatch_mt.c`, `spu_helpers.h`, etc.).
4. `runtime/memory/`, `runtime/syscalls/`, `runtime/platform/`: Core runtime emulation layers.
5. `libs/`: HLE firmware implementation (SPURS, sync, filesystem, audio, input, system, LiveDraw D3D12 backend, `libs/codec/edge_inflate.cpp`).
6. `tools/`: `gen_hle_nids.py` and `nid_database.py` (if generating `ppu_hle_nids.cpp` on the fly).
7. `FFmpeg Headers` (via `FFMPEG_INCLUDE_DIR`) if `PS3RECOMP_FFMPEG_RUNTIME=ON` is specified for video cutscenes (`cellVdec`).

---

## 7. Complete Differences vs `gowassension` Main

| Category | `gow-ascension-pc` | `gowassension` (main) | Classification | Reason / Details |
|---|---|---|---|---|
| **Disc Version** | `BCES-01741` (European release 1.00) | `BCAS25016` (Asia release 1.00) mounted on drive `E:` | `needed-for-our-disc` | Our physical/mounted media is Asia (`BCAS25016`); paths and title checks reflect this. |
| **VFS Layout** | Extracted directory in `work/disc/` passed to exe as argument | Mounted ISO on drive `E:` (`E:\PS3_GAME\USRDIR\GAME.PSARC`) + `vfs/` overlay | `needed-for-our-disc` | [`src/compat/ascension_defaults.cpp:324-342`](file:///C:/IfItWorksItWorks/Project/gowassension/src/compat/ascension_defaults.cpp#L324-L342) enforces drive `E:` mount check. |
| **HDD Roots** | Defaults to `gamedata/dev_hdd0` (git-ignored) | Explicitly set to `<root>/vfs` (`PS3_HDD0_ROOT`); `PS3_HDD1_ROOT` cleared | `ours-only` | Isolates all persistent writes to the local `vfs/` structure. |
| **FIOS Cache Clean Paths** | Checks `work\disc\cache\BCES01741\fios\cache.idx` | Checks `vfs/cache/{BCAS25016,BCES01741,NPUA80918,UCES01741}/fios/cache.idx` | `needed-for-our-disc` | Supports Asia disc title ID `BCAS25016`. |
| **Runtime Patches** | 48 patches (0001–0044 baseline + 0045–0048 softlock fixes) | 49 patches in `PATCH_ORDER.md` + 0050–0062 in `patches/series/`; 0045–0048 merged into ps3recomp commit `418f335f` | `needed-for-our-disc` (0001-0048) / `ours-only` (0049-0062) | Baseline patches are identical; 0049–0062 contain gowassension-specific diagnostic and scheduler tweaks. |
| **Host Defaults: SPURS_MAXCONT** | `"3=5,13=3,15=5,16=5,17=5"` ([`ascension_defaults.cpp:162`](file:///C:/IfItWorksItWorks/Project/gow-ascension-pc/host/ascension_defaults.cpp#L162)) | `"3=1,9=5,10=5,11=5,12=5,13=3,15=5,16=5,17=5,18=5"` ([`ascension_defaults.cpp:233`](file:///C:/IfItWorksItWorks/Project/gowassension/src/compat/ascension_defaults.cpp#L233)) | `ours-only` | Restricts WID 3 (particle manager) to 1 SPU to prevent thread starvation, adds WIDs 9–12 and 18. |
| **Host Defaults: Extra Flags** | None (only base 21 environment defaults) | Adds `GOW_FIOS_CACHE=off`, `GOW_7B1C_BYPASS=0`, `SPU_CH_BLOCK=1`, `SPU_INTERP_UNLIFTED=1`, `SPU_DMA_LAX=1`, `SPU_HOST_DEPTH_MAX=2000` | `ours-only` | Diagnostic heuristics and unwind bounds in `gowassension`. |
| **Host Defaults: PS3_MODS** | Set dynamically by launcher script `run.ps1` to AppData | Hardcoded default to `<root>/mods/skip-intro` | `ours-only` | Automatically applies the static skip-intro mod in CLI runs without a launcher. |
| **Mods Implementation** | Launcher recipe-based mod applying to disc files (`mod.json`) | Static 0-byte dummy file replacements in `mods/skip-intro/INTRO/*.TXT` | `ours-only` | Immediate file-level bypass of intro video sequences. |
| **Lifter Flags & Pipeline** | `--chunk-lines 150000 --reg-locals --toc 0x81EF60 --insn-switches` to `ppu_lifter.py` | Pre-patches ELF binary via `relift.py`, uses `--chunks 8`, regex post-processing for `gpr[3]`/`gpr[11]` | `ours-only` | `gowassension` uses chunk-count splitting and explicit regex yield wrappers. |
| **Generated Code Location** | `src/recomp/` (PPU) and `src/spu/` (SPU) | `src/gen/` (PPU) and `src/gen/spu/` (SPU) | `ours-only` | Repository directory naming convention. |
| **Generated Code Hashes** | Machine-generated, excluded from git, lifted per machine | Checked into workspace in `src/gen/` (hashes detailed below) | `ours-only` | Pinned lifted artifacts in `gowassension`. |
| **Dual Renderer Switch** | `ASCENSION_RPCS3_RSX` in `CMakeLists.txt` | `GOW_ENABLE_RPCS3_RSX` in `CMakeLists.txt` | `ours-only` | Build option naming divergence. |
| **Linking Method** | `add_subdirectory(ps3recomp)` compiles runtime as subproject | Links prebuilt `ps3recomp_runtime.lib` or `libps3recomp_runtime.a` | `ours-only` | Decouples runtime compilation from game binary rebuilds. |
| **Stack Allocation** | Relies on OS default (Windows 1 MB + dynamic) | Linker flag `/STACK:268435456` (256 MB) for deep PPC64 call nesting | `ours-only` | Guardrail against deep interpreter/recompiled recursion overflow. |

---

## 8. Exact SHA256 Hashes of `src/gen/` in `gowassension`

Citation: Computed directly via `Get-FileHash -Algorithm SHA256` on `src/gen/*`:

| File | SHA256 Hash |
|---|---|
| `src/gen/ppu_hle_nids.cpp` | `75F3BC191C2E5A4B75AD2A84765F885F45AC153974C2B3D521F506F91107B27E` |
| `src/gen/ppu_recomp.h` | `9E13DFD7C0112AAA2AD09A33AC4A189B3B33541AEABE1A4CA1FEDFDEBF6EB3E7` |
| `src/gen/ppu_recomp_000.cpp` | `DE3790FBCAA1CCFA156B6A1B8E7AF41157A4B79529702F4EB1F6D5C215304366` |
| `src/gen/ppu_recomp_001.cpp` | `FBCB8D3034E843948A024EA08E14C04D4C08209513280843B771BBA043C961C1` |
| `src/gen/ppu_recomp_002.cpp` | `75F69AD698F9FD6774CCE6BCC8B034929E64F246A7E6CAEA46100B7A1FB64235` |
| `src/gen/ppu_recomp_003.cpp` | `CD0F367AFA72795658EC5AE5259F5E278D331F6962687BB6335D85C9B75D2C45` |
| `src/gen/ppu_recomp_004.cpp` | `7280063B96C8089A27D39C0D4389A4FDE4C8F054852291A51C6B39002B6B4DFE` |
| `src/gen/ppu_recomp_005.cpp` | `FA3A76AB3BB39CD34EB67271F04DEB0E19CF087BF12602375CDD0DF088330E4C` |
| `src/gen/ppu_recomp_006.cpp` | `94844D9B63DBC10C0C27D4B18965632C74DE9A649FEE4E77A27240EF2DE7877B` |
| `src/gen/ppu_recomp_007.cpp` | `C4CDF85458F69E4F8DBE10351BFBBEFF4CF776B89D314FE1F6F5DE63F3DB4303` |
| `src/gen/ppu_recomp_table.cpp`| `B4517F0AA1D527381BA861D5B4A5FFD2A9867ED9F653267BBB4A4DD591CA5990` |
| `src/gen/spu/jobmods_manifest.json` | `E543A431526484FF20F3E229EE34515E871FA6DE58362B8CF791657A779EB5D6` |
| `src/gen/spu/jobmods_register.c` | `90FA46F7A9D5CDEDAC63A398CF4EFEE321FF534B223408BDC2673FE15FD824E5` |
| `src/gen/spu/pic_register.c` | `2938FD663F1EC8E1A0E2A1237A8316168FBE6DFDCFEBE539B420C735B808460A` |
| `src/gen/spu/spu_workloads.c` | `EB9E9DF1D7B8B1673BC610336AF3C415C2932ED6A8201C661555B11C15F79D20` |
