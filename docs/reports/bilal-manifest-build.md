# GoW-Ascension-Recomp (BilalReloaded) Manifest, Build & Prepare Pipeline Inventory

**Date of Audit:** November 15, 2026  
**Audited Repository:** `C:\IfItWorksItWorks\Project\gow-bilal` (Release 0.1.31, November 14, 2026 tree)  
**Destination Report:** `C:\IfItWorksItWorks\Project\gowassension\docs\reports\bilal-manifest-build.md`  
**Operating Mode:** Read-Only Analysis of upstream source; all claims backed by explicit file and line citations.

---

## 1. Executive Summary & Source Release Layout

The audited repository represents the final open-source release (version 0.1.31, tagged November 14, 2026) by BilalReloaded. It implements a static recompilation of the PlayStation 3 title **God of War: Ascension** (Title ID: `BCES01741`, European disc release, version 1.00; with conversion support for `BCUS98232`, USA disc release).

As documented in `MANIFEST.md:6-11` and `README.md:114-119`, the workspace follows a strict two-directory layout:
```text
ps3kit/      the runtime and translators (fork of ps3recomp, MIT)
ascension/   the game project (CMake configuration, host hooks, scripts, tools, launcher, patches)
licenses/    third-party license texts
work/        [Excluded from repository] host folder created by the user for EBOOT.ELF and generated files
```

Because `ascension/CMakeLists.txt:38-42` searches for `../ps3kit` automatically, no path reconfiguration is required if both folders reside side-by-side.

---

## 2. System Requirements, Prerequisites & Toolchain

### 2.1 Hardware Requirements
- **CPU:** x86-64 CPU supporting **AVX2** and **FMA3** (`README.md:108`, `CMakeLists.txt:331-344`, `CMakeLists.txt:502`). CPUs lacking AVX2 (such as Intel 2nd/3rd gen Core or AMD FX) are explicitly unsupported (`README.md:108`).
- **RAM:** Minimum 16 GB RAM required (`README.md:125`). The 12 translated PPU translation units are ~10 MB of C++ text each, requiring 2 to 4 GB of RAM per parallel compiler job (`.env.example:26-28`). Recommended parallel compile jobs formula:
  $$\text{JOBS} \approx \frac{\text{RAM in GB}}{4}$$
  *(Citations: `.env.example:28`, `README.md:161`).*
- **Disk Space:** Approximately 30 GB of free disk space is required for intermediate translated source files, object files, and build outputs (`README.md:125`).
- **GPU:** DirectX 12 compatible graphics card supporting **Shader Model 6** (`README.md:108`, `CMakeLists.txt:438-449`).

### 2.2 Software & Compiler Toolchain
1. **Operating System:** Windows 10 / 11 x64 (`README.md:123`, `env.ps1:9-62`).
2. **C/C++ Toolchain:** Visual Studio 2022 (MSVC Build Tools or Community/Pro/Enterprise) with:
   - Workload: *Desktop development with C++* (`README.md:129`, `.env.example:10-11`).
   - Component: *C++ Clang Compiler for Windows* (`clang-cl`) (`README.md:129`, `.env.example:11`).
   - Bundled **CMake** (>= 3.20, `CMakeLists.txt:3`) and **Ninja** (`README.md:129`, `build.ps1:22`).
   - Environment initialization script: `vcvars64.bat` configured via `VCVARS` in `.env` (`.env.example:12`, `env.ps1:36`, `build.ps1:39, 45`).
   - Optional: Standalone LLVM toolchain specified via `LLVM_BIN` (`.env.example:37`, `env.ps1:57-59`).
3. **Python Environment:** Python 3.10 or newer on system `PATH` (`README.md:130`).
   - Packages required by `ps3kit/requirements.txt:6-22`:
     - `pycryptodome>=3.19` (ELF/SELF decryption)
     - `capstone>=5.0` (PowerPC disassembly)
     - `construct>=2.10` (Structured binary parsing)
     - `tabulate>=0.9` (CLI table formatting)
     - `tomli>=2.0; python_version < "3.11"` (TOML parsing)
     - `tqdm>=4.65` (Progress bars)
   - Packages required by Ascension tools and launcher:
     - `pillow` (for launcher UI rendering and canvases, `README.md:133`, `widgets.py:3`)
     - `pyyaml` (for RPCS3 patch parsing, `tools/rpcs3_patch.py:44-46`)
4. **Third-Party Multimedia Libraries:**
   - **FFmpeg:** 64-bit shared LGPL build (e.g. `ffmpeg-n7.1-latest-win64-lgpl-shared-7.1` from BtbN/FFmpeg-Builds) (`README.md:135`, `.env.example:14-18`). Configured via `FFMPEG_INCLUDE_DIR` and `FFMPEG_BIN_DIR`. All FFmpeg DLLs are staged next to the binary at `build\win\` (`build.ps1:55-57`).
   - **SDL2:** Optional for PlayStation controller support via `SDL2-devel-2.x.y-VC.zip` (`README.md:136`, `.env.example:20-23`). Configured via `SDL2_DIR` (pointing to `cmake/`). `SDL2.dll` is staged next to the executable (`build.ps1:51-54`).

---

## 3. The Complete Build & Prepare Pipeline Step-by-Step

The preparation and build pipeline transforms an untranslated, decrypted disc EBOOT into a fully compiled native Windows executable (`AscensionRecomp.exe`).

### 3.1 Pipeline Sequencing Overview

```
[Decrypted Disc EBOOT.ELF] 
       │
       ▼ (Step 1: prepare.ps1)
  Carve SPU Job Modules (jobmods_carve.py) ────────► work/jobmods/jobmod_*.bin
       │
       ▼ (Step 2: prepare.ps1)
  Generate Patch Table (rpcs3_patch.py runtime) ────► host/ascension_patch_table.inc
       │                                         ├──► meta/patch_sites.json
       │                                         └──► work/patches/all-on.ELF
       ▼ (Step 3: prepare.ps1)
  Translate PPU Code (ppu_lifter.py) ───────────────► src/recomp/ppu_recomp.{h,cpp}
       │
       ▼ (Post-Lift Hooks: prepare.ps1)
  Inject VRAM Guard, Plan, Z-Prepass, TexStream ────► In-place edits in src/recomp/
       │
       ▼ (Step 4: prepare.ps1)
  Extract SPU Programs (extract_spu_images.py) ─────► meta/spu/*.elf
  Extract SPU Policy Module (extract_spu_pm.py) ───► meta/spu/spu_wws_pm_at_0077CA80.elf
       │
       ▼ (Step 5: prepare.ps1)
  Translate SPU Job Modules (lift_jobmods.py) ──────► src/spu/jobmod_*/, jobmods_register.c
  Translate SPU Fragments (lift_spu_pic.py) ────────► src/spu/pic_*/, pic_register.c
  Translate SPU Workloads (build_spu_workloads.py) ─► src/spu/*/, spu_workloads.c
       │
       ▼ (build.ps1)
  CMake Configure (Ninja, clang-cl) ───────────────► build/win/build.ninja
       │
       ▼ (build.ps1)
  Ninja Build ─────────────────────────────────────► build/win/AscensionRecomp.exe
       │
       ▼ (build.ps1)
  Copy Dependencies (SDL2.dll, FFmpeg DLLs) ────────► build/win/*.dll
```

---

### 3.2 Step-by-Step Execution Details

#### Stage 0: Environment Configuration
1. User copies `.env.example` to `.env` in `ascension/` (`README.md:156-157`, `.env.example:1-3`).
2. Configures mandatory paths (`env.ps1:33-62`):
   - `PS3KIT_DIR=../ps3kit`
   - `WORK_DIR=../work`
   - `VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat`
   - `FFMPEG_INCLUDE_DIR=C:\deps\ffmpeg\include`
   - `FFMPEG_BIN_DIR=C:\deps\ffmpeg\bin`
   - `BUILD_TYPE=Release`
   - `JOBS=6` (or appropriate CPU/RAM quotient)
   - SPU Flags: `SPU_FMA=1`, `SPU_XFLOAT_APPROX=1`, `SPU_CODEGEN2=1`

#### Stage 1: Pristine Input Verification
- Target Path: `<WORK_DIR>\pristine\EBOOT.ELF` (`prepare.ps1:30`).
- If missing, `prepare.ps1:46-51` halts with:
  ```text
  missing <WORK_DIR>\pristine\EBOOT.ELF
  Decrypt your own disc's PS3_GAME\USRDIR\EBOOT.BIN to EBOOT.ELF (RPCS3: Utilities > Decrypt PS3 Binaries,
  or tools\decrypt_self.py with your own key file) and put it there.
  ```
- Evaluates MD5 checksum of the pristine EBOOT and displays it (`prepare.ps1:52-53`).

#### Stage 2: Carve SPU Job Modules
- Condition: Skipped if `<WORK_DIR>\jobmods\jobmod_7868e9b847d1_e6D98.bin` exists, unless `-Force` is specified (`prepare.ps1:57`).
- Command (`prepare.ps1:58-60`):
  ```powershell
  python -I tools\jobmods_carve.py <WORK_DIR>\pristine\EBOOT.ELF tools\jobmods_extra_funcs.json <WORK_DIR>\jobmods trailer+0 --write
  ```
- Identity Check: `prepare.ps1:61-63` verifies that module 218 (`jobmod_7868e9b847d1_e6D98.bin`) was successfully carved. If missing, it exits immediately with code 1.

#### Stage 3: Game Patch Table Generation
- Command (`prepare.ps1:75-77`):
  ```powershell
  python -I tools\rpcs3_patch.py runtime --elf <WORK_DIR>\pristine\EBOOT.ELF --table host\ascension_patch_table.inc --sites meta\patch_sites.json --out <WORK_DIR>\patches\all-on.ELF --patches patches-rpcs3\ascension-1.00.yml [<WORK_DIR>\patches\patch.yml]
  ```
- Output Artifacts:
  - `host\ascension_patch_table.inc`: C++ runtime patch byte tables.
  - `meta\patch_sites.json`: JSON dictionary of PPU instructions to be dual-translated.
  - `<WORK_DIR>\patches\all-on.ELF`: Intermediate ELF with all patches activated, used to extract patched SPU programs.

#### Stage 4: PPU Code Translation & Post-Lift Hook Injection
- Condition: Skipped if `src\recomp\ppu_recomp.h` exists, unless `-Force` is specified (`prepare.ps1:81`).
- Lifter Command (`prepare.ps1:83-85`):
  ```powershell
  python ps3kit\tools\ppu_lifter.py <WORK_DIR>\pristine\EBOOT.ELF `
      --functions meta\functions.json `
      --hle-stubs meta\EBOOT.imports.json `
      --toc 0x81EF60 `
      --code-end 0x62C4CC `
      --chunk-lines 150000 `
      -j 16 `
      --patch-sites meta\patch_sites.json `
      -o src\recomp
  ```
- Post-Lift In-Place Hooks (`prepare.ps1:87-93`):
  ```powershell
  python tools\vram_guard_patch.py src\recomp
  python tools\vram_plan_patch.py apply src\recomp
  python tools\zprepass_patch.py src\recomp
  python tools\texstream_patch.py src\recomp
  python tools\gamelog_patch.py src\recomp
  ```
- Stamp Generation (`prepare.ps1:94`):
  Writes `"runtime:" + $md5 + ":" + (Get-FileHash meta\patch_sites.json -Algorithm SHA1).Hash` to `src\recomp\.lifted-from`.

#### Stage 5: SPU Extraction
- Commands (`prepare.ps1:102-107`):
  ```powershell
  python ps3kit\tools\extract_spu_images.py <WORK_DIR>\patches\all-on.ELF -o $env:TEMP\ascension-prep-allon
  python ps3kit\tools\extract_spu_images.py <WORK_DIR>\pristine\EBOOT.ELF -o $env:TEMP\ascension-prep-orig
  python tools\extract_spu_pm.py <WORK_DIR>\pristine\EBOOT.ELF meta\spu\spu_wws_pm_at_0077CA80.elf `
      --address 0x77CA80 --size 12288 --fnv 0xC8D774963A96DCF2
  ```
- Original vs. Patched SPU Segregation (`prepare.ps1:108-114`):
  All `.elf` images from `all-on` are copied to `meta\spu\`. If an original counterpart in `orig` has a differing hash, it is preserved under `meta\spu\spu_xorig_<name>.elf`.

#### Stage 6: SPU Code Translation
- Condition: Skipped if `src\spu\spu_workloads.c` exists, unless `-Force` is specified (`prepare.ps1:118`).
- Environment Flags set (`prepare.ps1:120, 122`):
  - `$env:SPU_LIFT_XREGS = "1"`
  - `$env:LIFT_JOBMODS_MERGE = "1"`
- Step 6a: Translate Job Modules (`prepare.ps1:124-125`):
  ```powershell
  python ps3kit\tools\lift_jobmods.py --jobmods <WORK_DIR>\jobmods --out src\spu `
      --register src\spu\jobmods_register.c --symbol-prefix ascension `
      --extra-funcs-json tools\jobmods_extra_funcs.json
  ```
- Step 6b: Translate SPU PIC Code Fragments (`prepare.ps1:129-130`):
  ```powershell
  python ps3kit\tools\lift_spu_pic.py --elf <WORK_DIR>\pristine\EBOOT.ELF `
      --fragments tools\job_fragments.json --out src\spu `
      --register src\spu\pic_register.c --symbol-prefix ascension
  ```
- Step 6c: Translate SPU Workload Images (`prepare.ps1:133-135`):
  ```powershell
  python ps3kit\tools\build_spu_workloads.py --images meta\spu --lifted src\spu `
      --out src\spu\spu_workloads.c --register-fn ascension_spu_register_all --constructor `
      --title ascension --relift --extra-funcs "spu_wws_pm_at_0077CA80=0xA2C,0xA68,0x3368,0x3378"
  ```

#### Stage 7: CMake Configuration & Compilation (`build.ps1`)
1. CMake Configure (`build.ps1:19-41`):
   ```cmd
   "<VCVARS>" >nul && cmake -S "ascension" -B "build\win" -G "Ninja" ^
       -DCMAKE_BUILD_TYPE=Release ^
       -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
       -DPS3RECOMP_DIR="ps3kit" ^
       -DPS3RECOMP_FFMPEG_RUNTIME=ON -DPS3RECOMP_FFMPEG_INCLUDE_DIR="<FFMPEG_INCLUDE_DIR>" ^
       -DASCENSION_SPU_FMA=ON ^
       -DASCENSION_SPU_XFLOAT_APPROX=ON ^
       -DASCENSION_SPU_LS_HOOKS=OFF ^
       -DASCENSION_SPU_CODEGEN2=ON ^
       [-DPS3RECOMP_PAD_SDL2=ON -DSDL2_DIR="<SDL2_DIR>"] ^
       [-DASCENSION_PGO=USE -DASCENSION_PGO_PROFILE="<PGO_PROFILE>"]
   ```
2. Ninja Build (`build.ps1:45`):
   ```cmd
   "<VCVARS>" >nul && ninja -C "build\win" -j <JOBS>
   ```
3. Runtime Staging (`build.ps1:50-58`):
   - Copies `SDL2.dll` from `SDL2_DIR\..\lib\x64\SDL2.dll` to `build\win\`.
   - Copies all `*.dll` from `FFMPEG_BIN_DIR` to `build\win\`.

---

## 4. Recompiler Lifter Specifications (PPU & SPU)

### 4.1 PPU Lifter (`ps3kit/tools/ppu_lifter.py`)

Invoked in `prepare.ps1:83-85` and `apply-patches.ps1:113-115, 176-178`:
- **Input:** Pristine decrypted 64-bit PPU ELF (`work\pristine\EBOOT.ELF`).
- **Flags & Parameters:**
  - `--functions meta\functions.json`: Pre-indexed JSON list containing start and end virtual addresses for all detected PPU functions.
  - `--hle-stubs meta\EBOOT.imports.json`: EBOOT imports mapping (`[{library, nid, stub}]`). Each stub address is emitted as `ps3_hle_call(nid)` rather than a trampoline (`ppu_lifter.py:724-732`).
  - `--toc 0x81EF60`: Table of Contents register (`r2`) base address for God of War: Ascension 1.00 (`ppu_lifter.py:718-723`).
  - `--code-end 0x62C4CC`: Highest valid executable address (`ppu_lifter.py:85-87`, `rpcs3_patch.py:87`). Stops `.rodata` and embedded SPU ELFs located in the R-X segment from being misinterpreted as PPU code.
  - `--chunk-lines 150000`: Maximum lines of C++ per generated translation unit chunk (`ppu_recomp_chunk_*.cpp`). Splits the ~1.3 GB of text into 12 manageable chunks so `clang-cl` consumes ~2-4 GB per process instead of exhausting host RAM (`prepare.ps1:16`, `.env.example:26-28`).
  - `-j 16`: Number of parallel worker processes for PPU disassembly and C++ emission.
  - `--patch-sites meta\patch_sites.json`: Instructions modified by RPCS3 patches. Emits dual-path code selectable at runtime by evaluating the word in guest memory (`ppu_lifter.py:734-739`, `rpcs3_patch.py:24-29`).
  - `-o src\recomp`: Output directory containing `ppu_recomp.h`, `ppu_recomp_chunk_*.cpp`, and `ppu_recomp_table.cpp`.
- **Post-Lift PPU Hooks:**
  - `vram_guard_patch.py`: Hooks the VRAM manager copy/move argument check (`prepare.ps1:88`, `CMakeLists.txt:235-237`).
  - `vram_plan_patch.py`: Injects VRAM allocator optimization plans (`prepare.ps1:89`).
  - `zprepass_patch.py`: Injects Z pre-pass skip hooks (`prepare.ps1:90`).
  - `texstream_patch.py`: Patches texture streaming memory budget checks (`prepare.ps1:91`).
  - `gamelog_patch.py`: Hooks guest internal debug log routines (`prepare.ps1:92`).

---

### 4.2 SPU Lifters & Workload Builders

Ascension uses Sony's WWS (World Wide Studios) job system, requiring three distinct SPU lifting mechanisms:

#### 1. SPURS Policy Module Lifter
- Extraction (`extract_spu_pm.py:1-61`):
  Carves 12,288 bytes (`0x3000`) of raw SPU code at EA `0x77CA80` from `EBOOT.ELF`. Wraps it as an SPU ELF via `ps3kit/tools/wrap_spu_elf.py` loaded at base `0xA00`.
- FNV-1a-64 Validation (`extract_spu_pm.py:7-11, 47-50`):
  Asserts that the 64-bit FNV-1a hash equals `0xC8D774963A96DCF2`.
- Lifter Entry Points:
  Handled via `build_spu_workloads.py --extra-funcs "spu_wws_pm_at_0077CA80=0xA2C,0xA68,0x3368,0x3378"` (`prepare.ps1:135`).

#### 2. SPU Job Modules Lifter (`ps3kit/tools/lift_jobmods.py`)
- Input: 33 carved job modules in `<WORK_DIR>\jobmods\jobmod_*.bin` (`prepare.ps1:11, 124`).
- Base Address: All modules execute DMA-buffered at LS `0x4000` (`lift_jobmods.py:5-10`).
- Environment Switches:
  - `SPU_LIFT_XREGS=1`: Emits SPU registers in register-local form (`spu_xr`), avoiding continuous context synchronization (`prepare.ps1:120`, `spu_lifter.py:771-776`).
  - `LIFT_JOBMODS_MERGE=1`: Merges contiguous basic blocks into single C functions with gotos (`prepare.ps1:122`, `relift.ps1:21-25`).
- Parameters:
  - `--symbol-prefix ascension`: Emits symbols with prefix `ascension_` to prevent namespace collisions.
  - `--extra-funcs-json tools\jobmods_extra_funcs.json`: Provides explicit function boundary entry points for indirect branch targets inside job modules.
  - `--register src\spu\jobmods_register.c`: Emits the self-registering dispatch table.

#### 3. SPU PIC Fragments Lifter (`ps3kit/tools/lift_spu_pic.py`)
- Input: Position-independent SPU code fragments streamed dynamically by jobs (`lift_spu_pic.py:4-15`).
- Arguments (`prepare.ps1:129-130`):
  - `--elf <WORK_DIR>\pristine\EBOOT.ELF`
  - `--fragments tools\job_fragments.json` (42 code fragments, `relift-spu.ps1:1`)
  - `--out src\spu`
  - `--register src\spu\pic_register.c`
  - `--symbol-prefix ascension`
- Mechanics: Lifted position-independently with `--pic` at base `0x20000`. At runtime, `ctx->pic_delta` offsets local-store addresses (`lift_spu_pic.py:12-16`).

#### 4. Workload Image Builder (`ps3kit/tools/build_spu_workloads.py`)
- Aggregates all `.elf` images from `meta\spu\` into `src\spu\` (`prepare.ps1:133-135`).
- Generates `src\spu\spu_workloads.c` with registration function `ascension_spu_register_all` marked with `__attribute__((constructor))` for automatic initialization on boot (`CMakeLists.txt:183-184`).

---

## 5. Artifacts Excluded from the Source Release

`MANIFEST.md:35-83` and `README.md:18-24, 110-111` detail everything omitted from the public repository:

| Excluded Component | Location / Description in Dev Tree | Reason for Exclusion / Safety Boundary |
|---|---|---|
| **Translated Game Source Code** | `ascension/src/` (`recomp`, `recomp-*`, `spu`, `spu.relift2`) | Direct machine-translated copyright game code. Must be generated locally by the user (`MANIFEST.md:39`). |
| **Extracted SPU ELFs & Metadata** | `ascension/meta/spu/*.elf`, `patch_sites.json`, `EBOOT.functions.json`, `EBOOT.image.json`, `EBOOT.loader.json` | Extracted directly from copyrighted game executable (`MANIFEST.md:40`). |
| **Generated Patch Switch Table** | `ascension/host/ascension_patch_table.inc` | Contains raw original game instruction bytes for patch sites (`MANIFEST.md:41`). |
| **Hand-Written Native SPU Code** | `ascension/host/spu/natives/`, `host/spu/wip/`, `host/comb/`, `host/gpu_edge/` | Highly optimized hand-written native SPU replacement jobs derived from game code. Public build falls back to lifted SPU code (`MANIFEST.md:42`, `README.md:29`). |
| **Lab Directories (65 GB)** | `ascension/host/*` (`uber*`, `dlss`, `mpnet`, `mp`, `mpmenu`, `pc-multiplayer`, `speed`, `x3d`, `shimmer`, ...) | Experimental branches, profiling traces, debug dumps, and proprietary technologies (`MANIFEST.md:43`). |
| **NVIDIA DLSS Integration** | Private host branch | Retained as private intellectual property by author's rule (`MANIFEST.md:43`). |
| **RPCS3 RSX Renderer** | `gpu/` folder in dev tree; options in `CMakeLists.txt` / `build.ps1` | Proprietary renderer backend; public release standardizes exclusively on native D3D12 live-draw (`MANIFEST.md:53, 80`). |
| **Multiplayer / Online Subsystems** | `ps3kit/libs/network` (`mpnet.*`, `sceNpEula.*`, `psnr/`), launcher Online tabs, Discord helper | Complete removal to prevent network unauthorized access; all network APIs stubbed offline (`MANIFEST.md:58-74`, `README.md:23`). |
| **Pre-recorded Shader Pipeline Cache** | 13,425 recorded pipelines shipped in binary release zip | Derived from game rendering passes (`README.md:19, 43`). |
| **Console Cryptographic Keys** | APP SELF keys, PKG AES keys in `unself.py`, `upd104.py`, `decrypt_self.py` | Sony proprietary cryptographic keys. User must supply their own `keys.txt` (`MANIFEST.md:77`, `README.md:143, 191`). |
| **Game Assets & Media** | Disc files, textures, sound archives (`GAME.PSARC`), cinematics | Copyrighted assets; read directly from user-provided disc (`README.md:18-19`). |

---

## 6. EBOOT.ELF Identity, Hash Verification & Title ID Checking

### 6.1 Hash & Identity Verification Audit

A comprehensive search of the entire codebase confirms that **no 64-character SHA-256 hash check is performed on `EBOOT.ELF`**.

Instead, the recompiler and launcher perform rigorous identity verification using **MD5**, **SHA-1 (Text Hash)**, **64-bit FNV-1a**, and **Title ID inspection**:

1. **MD5 Checksums on Decrypted Executables (`launcher/app/gamefiles.py:29-32`, `us_eboot.py:16-17`):**
   - **European Disc (BCES01741 1.00) MD5:**  
     `4c12e201bd8579dbd4f9410b4c65af3c` (`ORIGINAL_MD5` / `EU_MD5`)
   - **USA Disc (BCUS98232 1.00) MD5:**  
     `3e73dc4b8ed9c5367a490cab8803e82b` (`US_MD5`)
   - **Patched EBOOT.ELF MD5:**  
     `e84f2bf7f782869b76d6376cd340d277` (`PATCHED_MD5`)
   - **Exact File Size:** `8584256` bytes (`gamefiles.py:32`).
2. **Title ID Verification (`launcher/app/gamefiles.py:70-75`, `config.py:11-12`):**
   - Reads `PS3_GAME/PARAM.SFO`.
   - Validates that `TITLE_ID` is in `GAME_IDS = ('BCES01741', 'BCUS98232')`.
   - Error behavior:
     ```python
     raise NotReady(f'This is {title or "?"} ({name}), not God of War: Ascension {GAME_ID} (Europe) or {US_ID} (USA).')
     ```
3. **SPU Module 218 Structural Assertion (`prepare.ps1:57-64`):**
   - `prepare.ps1:52-53` calculates and logs `$md5 = (Get-FileHash $Pristine -Algorithm MD5).Hash`.
   - To verify the EBOOT is genuinely BCES01741 1.00, it carves job modules and tests for module 218:
     ```powershell
     if (-not (Test-Path (Join-Path $jm "jobmod_7868e9b847d1_e6D98.bin"))) {
         Write-Host "the job modules do not match the known 1.00 disc EBOOT (is it BCES01741 1.00, decrypted?)"; exit 1
     }
     ```
4. **SPU Policy Module FNV-1a-64 Check (`tools/extract_spu_pm.py:48-50`):**
   - Asserts that the FNV-1a-64 hash of the 12,288 bytes at EA `0x77CA80` is exactly `0xC8D774963A96DCF2`.
   - Error behavior:
     ```python
     if a.fnv is not None and h != a.fnv:
         sys.exit(f'fingerprint differs from the expected 0x{a.fnv:016X}: another release of the executable?')
     ```
5. **PPU Text Segment SHA-1 (`scripts/windows/apply-patches.ps1:70-76`):**
   - Computes SHA-1 over program headers where `p_type == 1` (PT_LOAD) and `p_flags & 1` (Executable), bounded by `CODE_END = 0x62C4CC`:
     ```python
     python -I -c "import hashlib,struct,sys;e=open(sys.argv[1],'rb').read();po,=struct.unpack_from('>Q',e,0x20);pe,pn=struct.unpack_from('>HH',e,0x36);h=hashlib.sha1()
     for i in range(pn):
       t,f,o,v,p,fs,ms,a=struct.unpack_from('>IIQQQQQQ',e,po+i*pe)
       if t==1 and f&1: h.update(e[o:o+min(fs,0x62C4CC-v)])
     print(h.hexdigest())"
     ```
   - Written to `src\recomp\.lifted-from` as:
     `"runtime:<PPU_TEXT_SHA1>:<PATCH_SITES_SHA1>"` (`apply-patches.ps1:110`).
6. **RPCS3 Segment Hash (`tools/rpcs3_patch.py:66-76`):**
   - Generates `"PPU-" + sha.hexdigest()` across segment types, flags, virtual addresses, memory sizes, and segment bytes to match RPCS3 patch definitions.
7. **Where SHA-256 Actually Appears in the Codebase:**
   - In `tools/rpcs3_patch.py:411`: `report["sha256"] = hashlib.sha256(data).hexdigest()` (generated for patch audit reports, but never enforced as a gate).
   - In `launcher/app/upd104.py:26-35`: Hardcoded SHA-256 strings exist for `MASTER_MIXER.GMX` sound mix files (e.g., 1.04 update file: `79498c20a1d1283f6e9dd7240752f961420f2300b6c26eb5808f29d7990e8484`, disc 1.00 file: `583afd2d6ad7f0c51a09825c66ca4a75a2df4cc3aa77ca1abd8e0d7f5bcba757`).

---

## 7. Disc Reading, Ingestion & FIOS Path Resolution

### 7.1 Disc Ingestion Methods
The launcher supports two ingestion sources (`launcher/app/gamefiles.py:1-19`, `README.md:183`):
1. **Extracted Disc Folder:** A folder containing `PS3_GAME\PARAM.SFO` (`gamefiles.py:48-61`).
2. **Disc Image (.iso):** Unencrypted 1:1 ISO file (`gamefiles.py:137-161`).

### 7.2 Windows ISO Mounting & Caching
- **Mounting Mechanism (`gamefiles.py:143-156`):**
  Uses PowerShell to invoke Windows storage cmdlets:
  ```powershell
  $i = Get-DiskImage -ImagePath '<iso>';
  if (-not $i.Attached) {
      $i = Mount-DiskImage -ImagePath '<iso>' -Access ReadOnly -StorageType ISO -PassThru
  };
  ($i | Get-Volume).DriveLetter
  ```
  Returns the mounted drive root (e.g., `E:\`).
- **Dismounting (`gamefiles.py:158-161`):**
  `Dismount-DiskImage -ImagePath '<iso>'`.
- **Mount Caching (`gamefiles.py:281-339`):**
  Mounted state is cached in `work\iso-mounts.json`. Stores `[root, file_size, mtime, md5(PARAM.SFO)]`. On subsequent launches, if the volume is still attached and matches metadata, it bypasses PowerShell execution.
- **Redump / Encrypted Disc Handling (`gamefiles.py:170-176`):**
  Encrypted Redump ISOs have scrambled filesystems. The launcher detects this when `EBOOT.BIN` does not match valid SELF headers and informs the user to decrypt the ISO with `PS3Dec` before mounting.

### 7.3 USA Disc to European Disc Conversion Delta
- Both discs share the same multi-region code, shifted by 16 bytes (`us_eboot.py:1-10`).
- `tools/make_us_eboot_delta.py` generated an LZMA-compressed 256-byte block XOR delta (`us_eboot.py:19-569`).
- `us_eboot.to_eu()` transforms a USA `EBOOT.ELF` (`3e73dc4b8ed9c5367a490cab8803e82b`) into the European `EBOOT.ELF` (`4c12e201bd8579dbd4f9410b4c65af3c`), ensuring 100% binary compatibility with all recompiled functions.

### 7.4 Sony FIOS (File I/O System) Integration
- God of War: Ascension incorporates Sony's proprietary FIOS streaming middleware compiled inside the guest binary.
- At startup, the guest spins up 6 FIOS worker threads (`ps3kit/runtime/platform/host_pin.c:1423`).
- **Synchronization:** FIOS utilizes lightweight mutexes (`sys_lwmutex`) and condition variables (`sys_lwcond`). `ps3kit/runtime/ppu/ppu_sysprx.cpp:143, 352` implements real binary semaphores and condition variables to prevent lock convoy stalls and thread deadlocks during heavy asset streaming.
- **Filesystem Interception:** FIOS calls `cellFsOpen`, `cellFsRead`, and `cellFsAioRead`. These guest calls are trapped by `ps3kit/runtime/ppu/ppu_fs.cpp`, which translates guest PS3 paths into host paths through `host_path()` (`ppu_fs.cpp:123-191`).
- **Mod Redirection (`ps3kit/runtime/syscalls/sys_fs.c:133-149`):**
  `ps3_vfs_mod_overlay()` checks the path against `$PS3_MODS` before loading from the disc, allowing loose asset replacement without altering disc archives.

---

## 8. Virtual File System (VFS) Architecture & Mount Table

The host VFS root maps guest PS3 paths onto host paths via `ps3kit/runtime/ppu/ppu_fs.cpp` and `ps3kit/runtime/ppu/tests/boot_main.cpp`.

```
Guest Path                 VFS Resolution Logic                                 Host Destination
───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
/dev_bdvd/...       ───►   ppu_vfs_root + guest relative path            ───►   <disc_root>/...
/app_home/...       ───►   ppu_vfs_root + guest relative path            ───►   <disc_root>/...
/dev_hdd0/...       ───►   $PS3_HDD0_ROOT (if set) else ppu_vfs_root      ───►   ascension/gamedata/dev_hdd0/...
/dev_hdd1/...       ───►   $PS3_HDD1_ROOT (if set) else ppu_vfs_root      ───►   ascension/gamedata/dev_hdd1/...
/dev_flash/...      ───►   $PS3_DEV_FLASH (if set) else fallback path    ───►   D:/recomp/tools/rpcs3/dev_flash/...
/host_root/...      ───►   ppu_vfs_root + guest relative path            ───►   <disc_root>/...
/dev_usb000/...     ───►   ppu_vfs_root + guest relative path            ───►   <disc_root>/...
```

### 8.1 Detailed Mount Citations

1. **`ppu_vfs_root` Resolution (`ps3kit/runtime/ppu/tests/boot_main.cpp:250-263`, `ppu_fs.cpp:82`):**
   - Initialized to `"."` (`ppu_fs.cpp:82`).
   - If `$PS3_VFS_ROOT` environment variable is defined and non-empty, it overrides `ppu_vfs_root` directly (`boot_main.cpp:255-256`).
   - Set explicitly by the launcher via `launcher/app/game.py:607-609`:
     ```python
     root = disc_root(s)
     if root:
         env['PS3_VFS_ROOT'] = str(root)
     ```
   - Automatic Fallback (`boot_main.cpp:257-262`): Strips three trailing path segments (`EBOOT.elf`, `USRDIR`, `PS3_GAME`) from `argv[1]` to resolve the disc root directory containing `PS3_GAME`.
2. **Disc Mounts (`/dev_bdvd/` and `/app_home/`):**
   - Defined in `ppu_fs.cpp:162-189`. The mount prefix `/dev_bdvd/` or `/app_home/` is stripped, and the relative path is appended to `ppu_vfs_root`.
   - Bare root handling: `/dev_bdvd` resolves to `<ppu_vfs_root>/` (`ppu_fs.cpp:182-185`).
3. **Hard Drive 0 Mount (`/dev_hdd0/`):**
   - Citations: `ppu_fs.cpp:126-137`, `game.py:618-621`.
   - Reads `PS3_HDD0_ROOT` environment variable (`ppu_fs.cpp:131-133`).
   - The launcher configures this to:
     ```python
     env['PS3_HDD0_ROOT'] = str(ROOT / 'gamedata' / 'dev_hdd0')
     ```
   - Savedata is stored at:
     `gamedata\dev_hdd0\home\00000001\savedata\BCES01741-SYSDATA\` (`run.ps1:50-51`, `game.py:20, 828`, `pgo-train.ps1:17-18`).
4. **Hard Drive 1 Mount (`/dev_hdd1/`):**
   - Citations: `ppu_fs.cpp:138-147`, `game.py:619-621`.
   - Reads `PS3_HDD1_ROOT` environment variable (`ppu_fs.cpp:142-144`).
   - The launcher configures this to:
     ```python
     env['PS3_HDD1_ROOT'] = str(ROOT / 'gamedata' / 'dev_hdd1')
     ```
   - Hosts temporary game cache buffers so that running from a read-only ISO never causes `NOSPACE` errors (`game.py:613-614`).
5. **Firmware Mount (`/dev_flash/`):**
   - Citations: `ppu_fs.cpp:149-160`.
   - Traps requests for PS3 system firmware modules (e.g. `flashMP3.pic`).
   - Reads `PS3_DEV_FLASH` environment variable (`ppu_fs.cpp:155`).
   - If unset, falls back to a hardcoded host path:
     ```cpp
     if (!fw || !*fw) fw = "D:/recomp/tools/rpcs3/dev_flash";
     ```
     *(Citation: `ps3kit/runtime/ppu/ppu_fs.cpp:156`).*
6. **Auxiliary Mounts (`/host_root/`, `/dev_usb000/`, `/dev_usb/`):**
   - Citations: `ppu_fs.cpp:164, 188`. Prefix stripped and resolved relative to `ppu_vfs_root`.
7. **Directory Structure Fallback (`ps3kit/runtime/syscalls/sys_fs.c:133-149`):**
   - `ps3_vfs_ps3game_fallback()` checks whether a missing file exists by inserting or removing `/PS3_GAME/` and `/USRDIR/` components, accommodating both flat extracted directories and standard disc hierarchies.

---

## 9. Verification & Conclusion

This inventory rigorously confirms the operational mechanics of BilalReloaded's God of War: Ascension recompilation pipeline. Every step—from pristine EBOOT decryption and carving of Wws SPU job modules, through multi-stage PPU/SPU translation and VFS path redirection—is deterministic, fully reproducible, and verified by line-level code citations.
