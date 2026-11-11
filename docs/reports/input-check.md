# Input Verification & Reference Comparison Report

**Author**: Sub-Agent B ("input-check")  
**Date**: 2026-11-11  
**Target File**: `vfs/PS3_GAME/USRDIR/EBOOT.elf` (and analysis / metadata tooling)  
**Reference Sources**: 
- `C:\IfItWorksItWorks\Project\gow-ascension-pc`
- Upstream PS3Recomp runtime and tooling: `C:\IfItWorksItWorks\Project\gowassension\ps3recomp`

---

## Executive Summary

| Parameter | Reference Expectation (`gow-ascension-pc`) | Our ELF / Repo (`gowassension`) | Status | Citation / Reference |
| :--- | :--- | :--- | :--- | :--- |
| **ELF SHA-256** | `93d94d54ba5dd76ed2d6c86b6558f4f8d2e02355604abca26e0bb71267943be5` | `93d94d54ba5dd76ed2d6c86b6558f4f8d2e02355604abca26e0bb71267943be5` | **IDENTICAL** (Exact Match) | `gow-ascension-pc/docs/STATUS.md:12`, `vfs/PS3_GAME/USRDIR/EBOOT.elf` |
| **File Size** | 8,584,256 bytes | 8,584,256 bytes | **IDENTICAL** | `gow-ascension-pc/docs/STATUS.md:12`, `vfs/PS3_GAME/USRDIR/EBOOT.elf` |
| **Title ID / Disc** | Disc tagged `BCES-01741` (multi-region string table) | Disc tagged `BCAS-25016` (multi-region string table) | **UNIVERSAL ELF** | String table at `EBOOT.elf` file off `0x61D7F0`-`0x6322C0` |
| **Executable Range** | `0x00010200` - `0x0062C4CC` | `0x00010200` - `0x0062C4CC` | **IDENTICAL** | Sections 1–4 (`flags=0x6` AX), Section 5 (`flags=0x2` A) |
| **Entry Point (OPD)**| `0x00808360` (Code: `0x00010410`) | `0x00808360` (Code: `0x00010410`) | **IDENTICAL** | `analysis/EBOOT.loader.json:2-3`, ELF Header `e_entry` |
| **Primary TOC Base** | `0x0081EF60` | `0x0081EF60` | **IDENTICAL** | `analysis/EBOOT.loader.json:4`, OPD at `0x808360+4` |
| **Code-End Bound**   | `0x0062C4CC` | `0x0062C4CC` | **IDENTICAL** | `gow-ascension-pc/scripts/windows/relift.ps1:24`, Sec 4 boundary |
| **Function Count**   | 19,199 functions (`meta/functions.json`) | 19,199 functions (`analysis/functions.json`) | **IDENTICAL** | `gow-ascension-pc/docs/STATUS.md:14`, `analysis/functions.json` |
| **OPD Func Seeds**   | 7,556 OPD descriptors | 7,556 OPD descriptors | **IDENTICAL** | `gow-ascension-pc/docs/STATUS.md:14`, `analysis/EBOOT.functions.json` |
| **Imports / NIDs**   | 345 imports from 25 libraries | 345 imports from 25 libraries | **IDENTICAL** | `gow-ascension-pc/docs/STATUS.md:13`, `analysis/EBOOT.imports.json` |
| **PPU Switches**     | 12 switch table entries | 8 switch table entries (4 bad pruned) | **IMPROVED** | See Section 6 for full forensic breakdown |

---

## 1. .text Section Range, Entry Point, Segment Boundaries

### 1.1 ELF Program Headers (PT_LOAD Segments)
Our `vfs/PS3_GAME/USRDIR/EBOOT.elf` (64-bit ELF, Big-Endian PowerPC, Machine `0x15` / PPC64, Type `0x2` ET_EXEC) contains 8 program headers:

```
[0] type=0x00000001 (PT_LOAD) flags=0x00400005 (RX) offset=0x00000000 vaddr=0x00010000 filesz=0x007E33E8 memsz=0x007E33E8 align=0x10000
[1] type=0x00000001 (PT_LOAD) flags=0x00600006 (RW) offset=0x007F0000 vaddr=0x00800000 filesz=0x0003BF70 memsz=0x002021B8 align=0x10000
[2] type=0x00000001 (PT_LOAD) flags=0x00000004 (R ) offset=0x0082BF70 vaddr=0x00000000 filesz=0x00000000 memsz=0x00000000 align=0x10000
[3] type=0x00000001 (PT_LOAD) flags=0x00000006 (RW) offset=0x0082BF70 vaddr=0x00000000 filesz=0x00000000 memsz=0x00000000 align=0x10000
[4] type=0x00000001 (PT_LOAD) flags=0x06600006 (RW) offset=0x0082BF70 vaddr=0x00000000 filesz=0x00000000 memsz=0x00000000 align=0x10000
[5] type=0x00000007 (PT_TLS)  flags=0x00000004 (R ) offset=0x008097D8 vaddr=0x008197D8 filesz=0x0000000C memsz=0x00000180 align=0x8
[6] type=0x60000001 (PRN_INF) flags=0x00000000 (  ) offset=0x007E3380 vaddr=0x007F3380 filesz=0x00000028 memsz=0x00000028 align=0x8
[7] type=0x60000002 (PRX_PRM) flags=0x00000000 (  ) offset=0x007E33A8 vaddr=0x007F33A8 filesz=0x00000040 memsz=0x00000040 align=0x4
```

- **Segment 0 (Text / Read-Only)**: `0x00010000` – `0x007F33E8` (File offset `0x00000000` to `0x007E33E8`). Houses executable code, trampolines, import stubs, strings, and read-only data.
- **Segment 1 (Data / BSS)**: `0x00800000` – `0x00A021B8` (File offset `0x007F0000` to `0x0082BF70`). Initialized data is `0x3BF70` bytes (up to `0x0083BF70`), followed by zero-initialized BSS of `0x1C6248` bytes extending to `0x00A021B8`.

### 1.2 Section Boundaries & Executable `.text` Range
The ELF contains 31 section header entries (string table was sanitized by Sony make_fself). Sections with flag bit `0x4` (`SHF_EXECINSTR`) define all executable guest code:

- **Section 1**: `0x00010200` – `0x0001022C` (`size 0x2C`, `flags=0x6` AX) — Pre-init trampoline.
- **Section 2**: `0x00010230` – `0x00627930` (`size 0x617700`, `flags=0x6` AX) — Main compiled game engine and library functions.
- **Section 3**: `0x00627930` – `0x00627954` (`size 0x24`, `flags=0x6` AX) — Call glue and dispatchers.
- **Section 4**: `0x00627954` – `0x0062C4CC` (`size 0x4B78`, `flags=0x6` AX) — 345 firmware import stubs.
- **Section 5**: `0x0062C4CC` – `0x0062C660` (`size 0x194`, `flags=0x2` A) — Read-only data: begins immediately with string literals `sys_net\0` and `cell...`.

**Result**: Executable code spans strictly from **`0x00010200` to `0x0062C4CC`** (total `0x61C2CC` = 6,406,860 bytes).

### 1.3 Entry Point
- ELF Header `e_entry`: **`0x00808360`** (points to the Official Procedure Descriptor table in Segment 1).
- Official Procedure Descriptor at `0x00808360` (file offset `0x007F8360`):
  - `Word 0` (Code Entry): **`0x00010410`**
  - `Word 1` (Module TOC): **`0x0081EF60`**
  - `Word 2` (Environment): **`0x00010478`**

Disassembly of CRT entry `_start` at `0x00010410`:
```assembly
0x00010410:  38 40 00 00   li      r2, 0
0x00010414:  64 42 00 80   oris    r2, r2, 0x80
0x00010418:  60 42 83 60   ori     r2, r2, 0x8360    ; r2 = 0x00808360 (OPD address)
0x0001041c:  80 42 00 04   lwz     r2, 4(r2)         ; r2 = 0x0081EF60 (Loads TOC from OPD)
0x00010420:  f8 21 ff 91   stdu    r1, -112(r1)      ; Stack frame setup
```

---

## 2. TOC Value: Reference (0x81EF60) vs BCAS-25016

The reference project (`gow-ascension-pc/scripts/windows/relift.ps1:24`) specifies:
`--toc 0x81EF60`

In our `vfs/PS3_GAME/USRDIR/EBOOT.elf`:
1. `ELF e_entry` points to OPD descriptor at `0x00808360`.
2. Offset `+0x04` in that descriptor holds `0x0081EF60`.
3. Inspection of all 7,556 `.opd` descriptors recorded in `analysis/EBOOT.functions.json` confirms that **100% of functions (7,556 / 7,556)** share the exact same TOC value:
   ```json
   "toc": "0x81ef60"
   ```
4. `analysis/EBOOT.loader.json:4-5` confirms:
   ```json
   "entry_toc": "0x81ef60",
   "module_toc": "0x81ef60"
   ```

**Conclusion**: The TOC value in our BCAS-25016 copy is **`0x81EF60`**, identical to the reference.

---

## 3. Code-End: Reference (0x62C4CC) vs Our ELF

The reference project (`gow-ascension-pc/scripts/windows/relift.ps1:24` and `docs/STATUS.md:14`) specifies:
`--code-end 0x62C4CC`

In our `vfs/PS3_GAME/USRDIR/EBOOT.elf`:
1. Section 4 (`.text` import stub table) ends at `0x0062C4CC`.
2. The final 4 instructions of executable code before `0x0062C4CC` (`0x0062C4BC` – `0x0062C4CC`):
   ```assembly
   0x0062C4BC:  e8 01 00 20   ld   r0, 32(r1)
   0x0062C4C0:  7c 08 03 a6   mtlr r0
   0x0062C4C4:  e8 41 00 28   ld   r2, 40(r1)
   0x0062C4C8:  4e 80 00 20   blr                     ; Final return of import stub sysPrxForUser:0xEF87A695
   ```
3. At exactly `0x0062C4CC` (Section 5 start, file offset `0x0061C4CC`):
   ```
   0x0062C4CC:  00 00 00 00 73 79 73 5F 6E 65 74 00  ; \0\0\0\0sys_net\0
   0x0062C4D8:  00 00 00 00 63 65 6C 6C 41 75 64 69  ; \0\0\0\0cellAudi...
   ```
   This is non-executable string data containing library import identifiers.

**Conclusion**: Our code-end is **`0x0062C4CC`**, matching the reference value byte for byte.

---

## 4. Function Table: meta/functions.json vs analysis/functions.json

### 4.1 Comparison Summary
- **Reference**: `gow-ascension-pc/docs/STATUS.md:14` cites:
  > "19,199 functions (`find_functions.py`), every `.opd` descriptor a function start; 7,556 OPD entries. Code ends at `0x62C4CC`, right after the import stubs (`0x627954`-`0x62C494`)."
- **Our Repository**: `analysis/functions.json` contains:
  - Total function entries: **19,199**
  - First function: `0x00010200` – `0x0001022C`
  - Last function: `0x0062C4A4` – `0x0062C4CC`
  - Overlapping functions: **0**
  - Gaps between functions: **4,666** (pure alignment padding, e.g. `nop`s to 16/32/64-byte boundaries)
  - Total code byte coverage: **6,134,688 bytes**
- **OPD Table Seeds**: `analysis/EBOOT.functions.json` contains:
  - Total function entries: **7,556** (exactly matching the 7,556 OPD descriptors in `.opd` at `0x00808340` – `0x00816F60`).

### 4.2 Discrepancies
There are **zero algorithmic or address discrepancies**.  
The only difference is directory path naming:
- Reference repository used the path `meta/functions.json` (untracked, excluded via `gow-ascension-pc/.gitignore:3`).
- Current workspace uses the path `analysis/functions.json` (untracked, excluded via `gowassension/.gitignore:4`).

---

## 5. Firmware Imports: EBOOT.imports.json

### 5.1 Comparison Summary
- Reference expectation (`gow-ascension-pc/docs/STATUS.md:13`):
  > "345 imports from 25 libraries; 308 resolve to a name. About 243 of them are named somewhere in ps3recomp's libraries."
- Our repository (`analysis/EBOOT.imports.json`):
  - Total imported stubs: **345**
  - Distinct libraries: **25**
  - Stub address range: `0x00627954` – `0x0062C4CC` (spanning all of Section 4)

### 5.2 Library Import Breakdown
| Library Name | Import Count | First Stub Address | Last Stub Address |
| :--- | :--- | :--- | :--- |
| `sys_net` | 23 | `0x00627954` | `0x00627E34` |
| `sys_io` | 7 | `0x00627E6C` | `0x00627FBC` |
| `sys_fs` | 26 | `0x00627FF4` | `0x0062856C` |
| `cellGame` | 8 | `0x006285A4` | `0x0062872C` |
| `cellSysutil` | 19 | `0x00628764` | `0x00628B54` |
| `cellSysutilAvconfExt` | 2 | `0x00628B8C` | `0x00628BC4` |
| `cellSysutilNpEula` | 3 | `0x00628BFC` | `0x00628C6C` |
| `cellAudio` | 7 | `0x00628CA4` | `0x00628DFC` |
| `cellVoice` | 15 | `0x00628E34` | `0x00629144` |
| `cellRtc` | 2 | `0x0062917C` | `0x006291B4` |
| `cellGcmSys` | 29 | `0x006291EC` | `0x0062980C` |
| `cellSpurs` | 45 | `0x00629844` | `0x0062A1EC` |
| `libvdec` | 8 | `0x0062A224` | `0x0062A3AC` |
| `cellNetCtl` | 16 | `0x0062A3E4` | `0x0062A72C` |
| `cellSsl` | 7 | `0x0062A764` | `0x0062A8BC` |
| `cellHttp` | 18 | `0x0062A8F4` | `0x0062ACAC` |
| `cellHttpUtil` | 2 | `0x0062ACE4` | `0x0062AD1C` |
| `sceNp` | 44 | `0x0062AD54` | `0x0062B6BC` |
| `sceNpCommerce2` | 2 | `0x0062B6F4` | `0x0062B72C` |
| `sceNpTrophy` | 11 | `0x0062B764` | `0x0062B994` |
| `sceNpTus` | 10 | `0x0062B9CC` | `0x0062BBC4` |
| `sceNp2` | 2 | `0x0062BBFC` | `0x0062BC34` |
| `sceNpUtil` | 4 | `0x0062BC6C` | `0x0062BD14` |
| `cellSysmodule` | 5 | `0x0062BD4C` | `0x0062BE2C` |
| `sysPrxForUser` | 30 | `0x0062BE64` | `0x0062C494` |
| **TOTAL** | **345** | `0x00627954` | `0x0062C4CC` |

All 345 NIDs and library bindings match 100%.

---

## 6. Audit of tools/ppu_switches.json

### 6.1 Disassembly Verification of All 12 Upstream Switches
We disassembled all 12 switch targets from `gow-ascension-pc/tools/ppu_switches.json` against `vfs/PS3_GAME/USRDIR/EBOOT.elf`:

| Index | Target Address | Original Hex | Original PPC Disassembly | Injected Hex | Injected PPC | Env Variable | Status / Audit Finding |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **1** | `0x003E8918` | `0x60000000` | `nop` | `0x396003FC` | `li r11, 1020` | `GOWA_PPU_SCHED_WAIT` | **VALID**: Overwrites pure NOP in `func_003E8298` spin loop |
| **2** | `0x003E891C` | `0x60000000` | `nop` | `0x44000002` | `sc` | `GOWA_PPU_SCHED_WAIT` | **VALID**: Overwrites pure NOP in `func_003E8298` spin loop |
| **3** | `0x00416AAC` | `0x60000000` | `nop` | `0x396003FC` | `li r11, 1020` | `GOWA_PPU_SCHED_WAIT` | **VALID**: Overwrites pure NOP in `func_00416A00` job scheduler |
| **4** | `0x00416AB0` | `0x60000000` | `nop` | `0x44000002` | `sc` | `GOWA_PPU_SCHED_WAIT` | **VALID**: Overwrites pure NOP in `func_00416A00` job scheduler |
| **5** | `0x00467010` | `0x88010093` | `lbz r0, 147(r1)` | `0x396003FC` | `li r11, 1020` | `GOWA_PPU_SCHED_WAIT` | **CORRUPTS GUEST**: Overwrites `readyCount` read! |
| **6** | `0x00467014` | `0x2F800000` | `cmpwi cr7, r0, 0` | `0x44000002` | `sc` | `GOWA_PPU_SCHED_WAIT` | **CORRUPTS GUEST**: Overwrites `cmpwi` branch condition! |
| **7** | `0x00467034` | `0x88010090` | `lbz r0, 144(r1)` | `0x396003FC` | `li r11, 1020` | `GOWA_PPU_SCHED_WAIT` | **CORRUPTS GUEST**: Overwrites `contention` read! |
| **8** | `0x00467038` | `0x2F800000` | `cmpwi cr7, r0, 0` | `0x44000002` | `sc` | `GOWA_PPU_SCHED_WAIT` | **CORRUPTS GUEST**: Overwrites `cmpwi` branch condition! |
| **9** | `0x0040559C` | `0x80BF0020` | `lwz r5, 32(r31)` | `0x38A00000` | `li r5, 0` | `GOWA_NO_DOF` | **VALID**: Disable Depth of Field (1.00 base address) |
| **10**| `0x00408AF0` | `0x80BF0020` | `lwz r5, 32(r31)` | `0x38A00000` | `li r5, 0` | `GOWA_NO_MB` | **VALID**: Disable Motion Blur (1.00 base address) |
| **11**| `0x00369B34` | `0x4BFFFCB5` | `bl 0x3697E8` | `0x60000000` | `nop` | `GOWA_NO_MLAA` | **VALID**: Disable MLAA call 1 (1.00 base address) |
| **12**| `0x00369B64` | `0x4BFFFC85` | `bl 0x3697E8` | `0x60000000` | `nop` | `GOWA_NO_MLAA` | **VALID**: Disable MLAA call 2 (1.00 base address) |

### 6.2 Forensic Analysis of the Upstream Bug in Switches 5–8
In upstream `gow-ascension-pc`, the note for switches 5–8 claimed:
> `"li r11,1020: sleep until SPU progress instead of busy-spinning on cellSpursGetWorkloadInfo readyCount poll"`  
> `"sc: with the li above, in the nop following GetWorkloadInfo"`

Inspection of `func_00466E28` (`0x00467000` – `0x00467040`) reveals the true assembly:
```assembly
0x00467008:  48 1c 18 d5   bl    0x6288DC           ; call cellSpursGetWorkloadInfo
0x0046700C:  60 00 00 00   nop                      ; Delay slot: exactly ONE NOP
0x00467010:  88 01 00 93   lbz   r0, 147(r1)        ; Reads readyCount
0x00467014:  2f 80 00 00   cmpwi cr7, r0, 0         ; Compares readyCount == 0
0x00467018:  40 9e ff d0   bne   cr7, 0x466FE8      ; Loops if readyCount != 0
...
0x0046702C:  48 1c 18 b1   bl    0x6288DC           ; call cellSpursGetWorkloadInfo
0x00467030:  60 00 00 00   nop                      ; Delay slot: exactly ONE NOP
0x00467034:  88 01 00 90   lbz   r0, 144(r1)        ; Reads contention
0x00467038:  2f 80 00 00   cmpwi cr7, r0, 0         ; Compares contention == 0
0x0046703C:  40 9e ff ac   bne   cr7, 0x466FE8      ; Loops if contention != 0
```
Because there was only **one** NOP (`0x0046700C` and `0x00467030`) rather than two, the reference patch at `0x00467010` and `0x00467034` was offset by 4 bytes, completely destroying `readyCount` and `contention` checks. This was the exact cause of the post-logo hang investigated in `.planning/quick/20261102-fix-relift-switches-and-hang`.

**Repository State**:
`gowassension/tools/ppu_switches.json` properly retains only the **8 clean, verified switches** (Entries 1–4 and 9–12), while `tools/relift.py:59-64` enforces a hard assertion aborting any yield patch attempting to overwrite non-NOP instructions.

### 6.3 Regional Specificity Question (BCES-01741 vs BCAS-25016)
**Are these addresses BCES-01741 specific?**  
**NO.** The decrypted `EBOOT.ELF` in both `gowassension` and `gow-ascension-pc` is the universal, multi-region base retail 1.00 executable (`SHA256: 93d94d54...`).  
Strings embedded in the ELF include:
- `BCUS98232` (North America)
- `BCES01741` (Europe)
- `BCES01742` (UK)
- `BCES01838` (Germany)
- `BCJS37008` (Japan)
- `BCAS25016` (Asia)

The switch notes cite alternative addresses (e.g., `1.12 0x43BCBC` for DOF, `1.12 0x43F210` for Motion Blur, `1.12 0x397754` for MLAA). Those secondary addresses belong to **Game Patch 1.12**, whereas `0x0040559C`, `0x00408AF0`, `0x00369B34`, and `0x00369B64` are the genuine addresses in the **1.00 base game** across all regions.

---

## 7. Mismatches & Step-by-Step Generation Guide

### 7.1 Identified Mismatches
1. **Directory Naming**:
   - `gow-ascension-pc` expected metadata in `meta/` (`meta/functions.json`, `meta/EBOOT.imports.json`).
   - `gowassension` stores metadata in `analysis/` (`analysis/functions.json`, `analysis/EBOOT.imports.json`, `analysis/EBOOT.loader.json`, `analysis/EBOOT.functions.json`).
2. **Switch Count in tools/ppu_switches.json**:
   - Upstream has 12 entries (including 4 corrupted entries at `0x00467010`, `0x00467014`, `0x00467034`, `0x00467038`).
   - `gowassension` has 8 verified clean entries with NOP-only guardrails.
3. **Register Preservation during Syscall Injection**:
   - Upstream bare injection of `li r11, 0x3FC; sc` clobbered `gpr[3]` (overwritten by `lv2_syscall` return code 0).
   - `gowassension/tools/relift.py` post-processes generated C++ to preserve `gpr[3]` and `gpr[11]`.

---

### 7.2 Step-by-Step Guide: Generating Metadata Files from Scratch

All tools required to generate metadata files exist directly within `ps3recomp/tools/` and `tools/`.

#### Step 1: Parse ELF Loaders, Segments, and Import Stubs
Generates `analysis/EBOOT.loader.json`, `analysis/EBOOT.image.json`, `analysis/EBOOT.imports.json`, and `analysis/EBOOT.functions.json`:

```powershell
python ps3recomp\tools\ppu_loader.py vfs\PS3_GAME\USRDIR\EBOOT.elf -o analysis/
```
- **Inputs**: `vfs\PS3_GAME\USRDIR\EBOOT.elf`
- **Outputs**:
  - `analysis/EBOOT.loader.json` (Entry point, TOC, OPD base/count)
  - `analysis/EBOOT.image.json` (Segment headers)
  - `analysis/EBOOT.imports.json` (345 imported firmware stubs & NIDs)
  - `analysis/EBOOT.functions.json` (7,556 OPD-seeded functions)

#### Step 2: Recover Full Function Boundaries
Scans all executable instructions (`0x00010200` to `0x0062C4CC`), seeds from the 7,556 OPD descriptors, detects prologues/epilogues, and traverses branch-and-link (`bl`) call graphs:

```powershell
python ps3recomp\tools\find_functions.py vfs\PS3_GAME\USRDIR\EBOOT.elf `
    --seed-json analysis\EBOOT.functions.json `
    --output analysis\functions.json
```
- **Inputs**: `vfs\PS3_GAME\USRDIR\EBOOT.elf`, `analysis\EBOOT.functions.json`
- **Output**: `analysis\functions.json` (19,199 functions)

*(Optional / Alternative: Headless Ghidra / IDA Pro Export)*
If using Ghidra or IDA Pro to enrich function boundaries or export symbols:
```powershell
# Ghidra headless analysis:
python ps3recomp\tools\ghidra_analyze.py vfs\PS3_GAME\USRDIR\EBOOT.elf -o ghidra_out
# IDA Pro headless analysis (requires idalib):
python ps3recomp\tools\ida_analyze.py vfs\PS3_GAME\USRDIR\EBOOT.elf -o ida_out
# Seed find_functions with IDA/Ghidra export:
python ps3recomp\tools\find_functions.py vfs\PS3_GAME\USRDIR\EBOOT.elf `
    --seed-json ida_out\functions.json `
    --output analysis\functions.json
```

#### Step 3: Generate Firmware HLE NID Dispatch Table
Generates C++ dispatcher mapping all 345 imported NIDs to runtime HLE implementations:

```powershell
python ps3recomp\tools\gen_hle_nids.py --all --out src\gen\ppu_hle_nids.cpp
```
- **Output**: `src\gen\ppu_hle_nids.cpp`

#### Step 4: Full Automated Pipeline via relift.py
The workspace script `tools/relift.py` wraps Steps 1–3, applies the verified instruction switches from `tools/ppu_switches.json`, runs `ppu_lifter.py`, and post-processes the generated C++ to preserve registers:

```powershell
python tools\relift.py `
    --elf vfs\PS3_GAME\USRDIR\EBOOT.elf `
    --analysis analysis `
    --insn-switches tools\ppu_switches.json `
    --code-end 0x0062C4CC `
    --chunks 8 `
    --out src\gen
```

---

## 8. Summary & Verification Verdict

1. **ELF Identity**: Our `vfs/PS3_GAME/USRDIR/EBOOT.elf` matches the reference ELF **bit for bit** (`SHA-256: 93d94d54ba5dd76ed2d6c86b6558f4f8d2e02355604abca26e0bb71267943be5`).
2. **Parameters**: TOC (`0x81EF60`), Code-End (`0x62C4CC`), Entry OPD (`0x808360` / Code `0x10410`), and Executable boundaries (`0x00010200` – `0x0062C4CC`) are 100% identical between both projects.
3. **Metadata Completeness**: All 19,199 functions, 7,556 OPD seeds, and 345 firmware imports are completely accounted for in `analysis/`.
4. **PPU Switches**: The 8 active switches in `tools/ppu_switches.json` are verified valid. The 4 omitted upstream switches were proven to be misaligned and corruptive to guest logic.
