# Sub-Agent C Report: wid8-static-trace
**Document Path**: `docs/reports/wid8-static-trace.md`  
**Target Program**: `wws_job_pm` (Sony WorldWide Studios SPURS Policy Module / Job Manager)  
**Binary Image**: `spu_programs/wws_job_pm.bin` / `spu_programs/wws_job_pm.elf` (Image ID 2)  
**Lifted Source**: `src/gen/spu/wws_job_pm/spu_recomp.c`  
**Analysis Mode**: Read-Only Static Analysis & Disassembly Trace  

---

## 1. Executive Summary

1. **Origin and Meaning of `0x30014580`**:
   - `0x30014580` is the SPU machine code for `bra 0x00A2C` (`SPU_RR` / branch absolute).
   - In `wws_job_pm`, instruction vector initialization at `0x2230 - 0x2274` (`spu_recomp.c:1795-1810`) constructs `0x30014580` via `ilhu $r0, 0x180; iohl $r0, 0xA2C; shli $r0, $r0, 5` and stores it to Local Store address `0x0000` via `20800000 stqa $r0, 0x0` (`spu_recomp.c:1810`).
   - Because `spu_ilhu`, `spu_iohl`, and `spu_shli` splat to all four 32-bit words (`ps3recomp/runtime/spu/spu_helpers.h:1814-1817`), `LS[0x0..0xF]` contains four identical words: `{ 0x30014580, 0x30014580, 0x30014580, 0x30014580 }`.
   - It is **not** an RSX fence or PPU memory fence; it is the SPU interrupt/reset vector pointing to the SPU interrupt handler at `0x00A2C`.

2. **How `0x30014580` Enters `ls[0x1530]`**:
   - The job list header loaded into LS reservation buffer `0x80` (`0x1A80-0x1A84`) defines buffer base EAs at offset `+0x30` (`build/logs/task4_fixed_run.stderr.log:723`: `747E3580 747E7580 00000000 00000000`).
   - Words 2 and 3 are `0x00000000`. When a job entry references an unpopulated/null buffer slot, the computed buffer page EA (`$r71`) evaluates to `0x00000000` (`spu_recomp.c:1168-1174`).
   - At `0x1BE4-0x1BE8` (`spu_recomp.c:1271-1277`), `wws_job_pm` issues DMA GET `MFC_LSA = 0xD00, MFC_EAL = 0, MFC_Size = 128, MFC_Cmd = 0x40`. This is the exact transfer recorded in logs: `[spurs-pm] DMA cmd=0x40 lsa=0x00D00 ea=0x000000000 size=0x80 tag=8` (`run.log:45`).
   - At `0x1B04` (`spu_recomp.c:1182`), `lqx $r3, $r3, $r61` loads from `0xD00 + $r61`.
   - If the DMA from `ea=0` either:
     - Copied from an uncommitted/zeroed guest page where another mechanism (or stale staging) placed vector 0 words, OR
     - If local store staging at `0xD00` was unpopulated or dereferenced a null LS base pointer pointing to `LS[0x0]`:
   - Register `$r3` loads `0x30014580`. At `0x1B1C` (`spu_recomp.c:1194`), `rotqbyi $r63, $r3, 4` rotates word 1 into `$r63` (also `0x30014580`).
   - Instruction `0x1B84: stqa $r63, 0x1530` (`spu_recomp.c:1234`) writes `0x30014580` into `ls[0x1530]`.

3. **The Infinite Spin Loop (Wid 8 Wedge)**:
   - Command byte `0x30` in `0x30014580` is neither 0 (empty), 1 (ready), 3 (stall), nor 4 (next-buffer).
   - `0x1B30: brhz $r69, 0x1B70` (`spu_recomp.c:1200`) branches to `0x1B70`.
   - At `0x1B70`, `ceq $r57, $r63, $r58` compares `$r63` (`0x30014580`) against existing `ls[0x1530]`. Once cached, `$r57` is TRUE (`1`), and `0x1B7C: brnz $r57, 0x1B40` (`spu_recomp.c:1232`) skips DMA and branches to `0x1B40`.
   - At `0x1B40`, `putllc` is issued on reservation EA `0x009FDF80` (`spu_recomp.c:1205-1212`).
   - At `0x1B5C`: `brhz $r69, 0x1A60` (`spu_recomp.c:1217`) unconditionally branches back to `0x1A60`.
   - The workload loops between `0x1A60` and `0x1B7C` forever at 100% CPU on lane 5 (`build/logs/task4_fixed_run.stderr.log:713`, `799`, `1010`).

4. **Analysis of 0x1E48 (`AuditEndJob`)**:
   - `0x1E48` is the WWS audit logging flush routine. It calculates execution duration (`ls[0xDA0] - ls[0xD90]`) and issues DMA PUT (`MFC_Cmd = 0x20`) of 16 bytes to the audit buffer EA in `ls[0xDE0]`.
   - It is **not** a completion signal, thread notify, or event flag trigger.
   - Branch `0x1E5C: brz $r2, 0x1EF0` (`spu_recomp.c:1478`) checks `ls[0xDD0] | ls[0xDC0]`. Because production games run with audit disabled (high 32-bits of `wkl_data` is 0), `0x1D60` leaves these words 0 and immediately returns (`spu_recomp.c:1422`).
   - Hence `0x1E48` always early-exits at `0x1E5C` without issuing DMA.
   - Furthermore, in `spu_recomp.c:1925-1932`, `0x23B0 - 0x23DC` (the callsite `0x23C0: brsl $r0, 0x1E48`) was omitted by the lifter due to preceding indirect branches `0x23A4` / `0x23AC`.

---

## 2. Detailed Static Trace of Focus Region A: 0x1A60 – 0x1BE8

### 2.1 Function Context and Entry
The subroutine beginning at `0x1A48` is called by `wws_job_pm` from `0x26EC` and `0x2728` (`spu_programs/wws_job_pm.bin:0x26EC`, `0x2728`):
```assembly
000026E0:  lqa   $r3, 0x1450      # $r3 = wkl_data & -16 (Job List EA = 0x009FDF80)
000026E4:  lqa   $r4, 0x1340      # $r4 = SPU number / slot
000026EC:  brsl  $r6, 0x1A48      # Call 0x1A48 with link register $r6
```

### 2.2 Instruction-by-Instruction Breakdown (0x1A48 – 0x1B6C)

| SPU LS Address | Hex Opcode | SPU Instruction | `src/gen/spu/wws_job_pm/spu_recomp.c` Line | Functional Description |
|---|---|---|---|---|
| `0x00001A48` | `42004032` | `ila $r50, 0x80` | `1108` | `$r50 = 0x80` (atomic reservation line address) |
| `0x00001A4C` | `32800033` | `fsmbi $r51, 0x0` | `1109` | `$r51 = 0x00000000...` (constant zero vector) |
| `0x00001A50` | `040001B4` | `ori $r52, $r3, 0` | `1110` | `$r52 = $r3` (Job list header EA, e.g. `0x009FDF80`) |
| `0x00001A54` | `32980035` | `fsmbi $r53, 0x3000` | `1111` | Bitmask for buffer tag fields |
| `0x00001A58` | `18010236` | `a $r54, $r4, $r4` | `1112` | Scaled lane index |
| `0x00001A5C` | `30827C37` | `lqa $r55, 0x13E0` | `1113` | Load job list configuration word |
| **`0x00001A60`** | `40200031` | `nop` | `1118` | Loop target from `0x1B5C` / `0x1B64` |
| `0x00001A64` | `21A00832` | `wrch MFC_LSA, $r50` | `1119` | `MFC_LSA = 0x80` |
| `0x00001A68` | `40806803` | `il $r3, 208` | `1124` | `$r3 = 208 = 0xD0` (`MFC_GETLLAR_CMD`) |
| `0x00001A6C` | `21A00934` | `wrch MFC_EAL, $r52` | `1125` | `MFC_EAL = 0x009FDF80` |
| `0x00001A70` | `40804038` | `il $r56, 128` | `1126` | `$r56 = 128` (128-byte reservation line size) |
| `0x00001A74` | `21A00A83` | `wrch MFC_Cmd, $r3` | `1127` | Issue `getllar` on Job List header |
| `0x00001A78` | `42068003` | `ila $r3, 0xD00` | `1130` | **`$r3 = 0xD00`** (Base address of staging buffer) |
| `0x00001A7C` | `01A00D84` | `rdch $r4, MFC_RdAtomicStat` | `1131` | Wait for atomic reservation grant |
| `0x00001A80` | `34001939` | `lqd $r57, 0x0($r50)` | `1134` | Load Job List header word 0..3 from LS `0x80` |
| `0x00001A84` | `3400D904` | `lqd $r4, 0x30($r50)` | `1135` | Load buffer base EAs from LS `0xB0` (JL offset `+0x30`) |
| `0x00001A88` | `3F821C85` | `rotqbyi $r5, $r57, 8` | `1136` | Extract buffer set index |
| `0x00001A8C` | `3082A23A` | `lqa $r58, 0x1510` | `1137` | Load previously cached JL header tag |
| `0x00001A90` | `0F6102BB` | `shli $r59, $r5, 4` | `1142` | Scale buffer set offset |
| `0x00001A94` | `3EA000BC` | `chd $r60, $r1, 0` | `1143` | Generate mask |
| `0x00001A98` | `0F6082BD` | `shli $r61, $r5, 2` | `1144` | Buffer select byte shift |
| `0x00001A9C` | `3B8F423D` | `rotqby $r61, $r4, $r61` | `1145` | **Extract buffer base EA from `+0x30` into `$r61`** |
| `0x00001AA0` | `780E5D04` | `ceq $r4, $r58, $r57` | `1146` | Compare cached header vs live header |
| `0x00001AA4` | `34031DBA` | `lqd $r58, 0xC0($r59)` | `1147` | Load buffer descriptor from LS `0xC0 + $r59` |
| `0x00001AA8` | `4080043E` | `il $r62, 8` | `1148` | Tag ID 8 |
| `0x00001AAC` | `3082A03F` | `lqa $r63, 0x1500` | `1149` | Load previously cached buffer EA from LS `0x1500` |
| `0x00001AB0` | `40802040` | `il $r64, 64` | `1150` | MFC command 64 (`0x40 = MFC_GET_CMD`) |
| `0x00001AB4` | `3EA11B41` | `chd $r65, $r54, 4` | `1151` | Lane byte mask |
| `0x00001AB8` | `40808042` | `il $r66, 256` | `1152` | Tag mask `1 << 8 = 256` |
| `0x00001ABC` | `1C009B43` | `ai $r67, $r54, 2` | `1153` | Lane offset |
| `0x00001AC0` | `40800144` | `il $r68, 2` | `1154` | Tag update mode `MFC_TAG_UPDATE_ALL = 2` |
| `0x00001AC4` | `3F830245` | `rotqbyi $r69, $r4, 12` | `1155` | Extract header match flag |
| `0x00001AC8` | `182E9A84` | `and $r4, $r53, $r58` | `1156` | Mask buffer cursor |
| `0x00001ACC` | `0F041D46` | `roti $r70, $r58, 16` | `1157` | Rotate cursor limits |
| `0x00001AD0` | `0F60C247` | `shli $r71, $r4, 3` | `1158` | Offset = cursor * 8 bytes |
| `0x00001AD4` | `3B90DD43` | `rotqby $r67, $r58, $r67` | `1159` | Select consumer cursor |
| `0x00001AD8` | `1D004248` | `ahi $r72, $r4, 1` | `1160` | Increment cursor |
| `0x00001ADC` | `180F63BD` | `a $r61, $r71, $r61` | `1161` | `$r61 = buffer base EA + (cursor * 8)` |
| `0x00001AE0` | `590121C7` | `clgth $r71, $r67, $r4` | `1162` | Compare against consumer |
| `0x00001AE4` | `886121C7` | `selb $r67, $r67, $r4, $r71` | `1163` | Select cursor |
| `0x00001AE8` | `14E01EC7` | `andi $r71, $r61, -128` | `1168` | **`$r71 = $r61 & ~0x7F`** (128-byte aligned host EA!) |
| `0x00001AEC` | `59122349` | `clgth $r73, $r70, $r72` | `1169` | Check queue bounds |
| `0x00001AF0` | `780FE3BF` | `ceq $r63, $r71, $r63` | `1174` | Compare computed EA `$r71` vs cached EA `$r63` |
| `0x00001AF4` | `141FDEBD` | `andi $r61, $r61, 127` | `1175` | **`$r61 = $r61 & 0x7F`** (byte offset within 128B page) |
| `0x00001AF8` | `20001DC5` | `brz $r69, 0x1BE4` | `1176` | If header tag mismatched -> Branch to DMA fetch `0x1BE4` |
| `0x00001AFC` | `20001D3F` | `brz $r63, 0x1BE4` | `1177` | If buffer page EA mismatched -> Branch to DMA fetch `0x1BE4` |
| `0x00001B00` | `8711A449` | `selb $r56, $r72, $r70, $r73` | `1181` | Select new cursor |
| `0x00001B04` | `388F4183` | `lqx $r3, $r3, $r61` | `1182` | **Load quadword from `($r3 + $r61) & ~0xF` (`0xD00 + $r61`)** |
| `0x00001B08` | `B70E9C3C` | `shufb $r56, $r56, $r58, $r60` | `1183` | Pack cursor update |
| `0x00001B0C` | `3B8F4183` | `rotqby $r3, $r3, $r61` | `1184` | **Rotate selected command word to word 0 of `$r3`** |
| `0x00001B10` | `3082A63A` | `lqa $r58, 0x1530` | `1190` | Load cached dependency EA from LS `0x1530` |
| `0x00001B14` | `7D00C1BC` | `ceqhi $r60, $r3, 3` | `1191` | Is command == 3? (`kReserveBufSet` / stall) |
| `0x00001B18` | `7D0101BD` | `ceqhi $r61, $r3, 4` | `1192` | Is command == 4? (`kUnreserveBufSets`) |
| `0x00001B1C` | `3F8101BF` | `rotqbyi $r63, $r3, 4` | `1194` | **Extract word 1 (bytes 4..7) into `$r63` (Dependency EA)** |
| `0x00001B20` | `7D0041C5` | `ceqhi $r69, $r3, 1` | `1195` | Is command == 1? (`kRunJob`) |
| `0x00001B24` | `22001303` | `brhz $r3, 0x1BBC` | `1196` | If command == 0 (empty slot) -> Exit via `0x1BBC` |
| `0x00001B28` | `2300033C` | `brhnz $r60, 0x1B40` | `1197` | If command == 3 -> Commit reservation and loop |
| `0x00001B2C` | `230013BD` | `brhnz $r61, 0x1BC8` | `1198` | If command == 4 -> Advance to next buffer |
| `0x00001B30` | `22000845` | `brhz $r69, 0x1B70` | `1200` | **If command != 1 -> Branch to dependency wait at `0x1B70`** |
| `0x00001B34` | `B70E21C1` | `shufb $r56, $r67, $r56, $r65` | `1201` | Update consumer |
| `0x00001B38` | `190DC248` | `ah $r72, $r4, $r55` | `1202` | Advance offset |
| `0x00001B40` | `40805A39` | `il $r57, 180` | `1206` | `$r57 = 180 = 0xB4` (`MFC_PUTLLC_CMD`) |
| `0x00001B44` | `87121C35` | `selb $r56, $r56, $r72, $r53` | `1207` | Merge cursor |
| `0x00001B48` | `24031DB8` | `stqd $r56, 0xC0($r59)` | `1208` | Store updated buffer descriptor to LS |
| `0x00001B4C` | `21A00832` | `wrch MFC_LSA, $r50` | `1209` | `MFC_LSA = 0x80` |
| `0x00001B50` | `21A00934` | `wrch MFC_EAL, $r52` | `1210` | `MFC_EAL = 0x009FDF80` |
| `0x00001B54` | `21A00AB9` | `wrch MFC_Cmd, $r57` | `1211` | Issue `putllc` to commit queue cursor |
| `0x00001B58` | `01A00DB8` | `rdch $r56, MFC_RdAtomicStat` | `1216` | Read atomic status |
| `0x00001B5C` | `227FE0C5` | `brhz $r69, 0x1A60` | `1217` | **If command != 1 ($r69 == 0) -> Branch back to `0x1A60`** |
| `0x00001B60` | `14005C38` | `andi $r56, $r56, 1` | `1218` | Check reservation lost |
| `0x00001B64` | `217FDFB8` | `brnz $r56, 0x1A60` | `1219` | If reservation lost -> Retry via `0x1A60` |
| `0x00001B68` | `2082A633` | `stqa $r51, 0x1530` | `1224` | **Clear `ls[0x1530]` with 0** (success path) |
| `0x00001B6C` | `35000300` | `bi $r6` | `1225` | Return to caller (success) |

---

### 2.3 Instruction-by-Instruction Breakdown (0x1B70 – 0x1C0C)

| SPU LS Address | Hex Opcode | SPU Instruction | `src/gen/spu/wws_job_pm/spu_recomp.c` Line | Functional Description |
|---|---|---|---|---|
| **`0x00001B70`** | `780E9FB9` | `ceq $r57, $r63, $r58` | `1229` | Compare dependency EA `$r63` against cached `ls[0x1530]` (`$r58`) |
| `0x00001B74` | `420A903A` | `ila $r58, 0x1520` | `1230` | `$r58 = 0x1520` (LS target for dependency word) |
| `0x00001B78` | `14FC1FBC` | `andi $r60, $r63, -16` | `1231` | `$r60 = $r63 & ~0xF` (16-byte aligned dependency EA) |
| `0x00001B7C` | `217FF8B9` | `brnz $r57, 0x1B40` | `1232` | **If already cached in `0x1530` -> Skip DMA, branch to `0x1B40`** |
| `0x00001B80` | `40800839` | `il $r57, 16` | `1233` | Transfer size = 16 bytes |
| `0x00001B84` | `2082A63F` | `stqa $r63, 0x1530` | `1234` | **STORE `$r63` TO `ls[0x1530]`** |
| `0x00001B88` | `21A0083A` | `wrch MFC_LSA, $r58` | `1235` | `MFC_LSA = 0x1520` |
| `0x00001B8C` | `21A0093C` | `wrch MFC_EAL, $r60` | `1236` | `MFC_EAL = $r63 & ~0xF` |
| `0x00001B90` | `21A009B9` | `wrch MFC_Size, $r57` | `1237` | `MFC_Size = 16` |
| `0x00001B94` | `21A00A3E` | `wrch MFC_TagID, $r62` | `1238` | `MFC_TagID = 8` |
| `0x00001B98` | `21A00AC0` | `wrch MFC_Cmd, $r64` | `1239` | `MFC_Cmd = 0x40` (MFC GET) |
| `0x00001B9C` | `21A00B42` | `wrch MFC_WrTagMask, $r66` | `1240` | Tag mask = 256 |
| `0x00001BA0` | `21A00BC4` | `wrch MFC_WrTagUpdate, $r68` | `1241` | Update = 2 |
| `0x00001BA4` | `01A00C39` | `rdch $r57, MFC_RdTagStat` | `1246` | Wait for dependency DMA completion |
| `0x00001BA8` | `34001D39` | `lqd $r57, 0x0($r58)` | `1247` | Load fetched 16 bytes from LS `0x1520` |
| `0x00001BAC` | `1C039FBA` | `ai $r58, $r63, 14` | `1248` | Compute halfword rotate amount |
| `0x00001BB0` | `3B8E9CB9` | `rotqby $r57, $r57, $r58` | `1249` | Rotate dependency counter into preferred slot |
| `0x00001BB4` | `227FF1B9` | `brhz $r57, 0x1B40` | `1250` | If dependency halfword == 0 -> Still pending, loop to `0x1B40` |
| `0x00001BB8` | `40800003` | `il $r3, 0` | `1251` | `$r3 = 0` (Dependency satisfied) |
| `0x00001BBC` | `2082A033` | `stqa $r51, 0x1500` | `1256` | Clear `ls[0x1500]` |
| `0x00001BC0` | `2082A633` | `stqa $r51, 0x1530` | `1257` | **Clear `ls[0x1530]`** |
| `0x00001BC4` | `35000300` | `bi $r6` | `1258` | Return to caller |
| `0x00001BC8` | `328007BA` | `fsmbi $r58, 0xF` | `1262` | Next buffer update mask |
| `0x00001BCC` | `180E5D39` | `a $r57, $r58, $r57` | `1263` | Advance buffer |
| `0x00001BD0` | `3280783A` | `fsmbi $r58, 0xF0` | `1264` | Pack mask |
| `0x00001BD4` | `3F821FBC` | `rotqbyi $r60, $r63, 8` | `1265` | Rotate |
| `0x00001BD8` | `872F1CBA` | `selb $r57, $r57, $r60, $r58` | `1266` | Merge |
| `0x00001BDC` | `24001939` | `stqd $r57, 0x0($r50)` | `1267` | Store updated JL header to LS `0x80` |
| `0x00001BE0` | `327FEC00` | `br 0x1B40` | `1268` | Branch to `putllc` at `0x1B40` |
| **`0x00001BE4`** | `21A00803` | `wrch MFC_LSA, $r3` | `1272` | **`MFC_LSA = $r3 = 0xD00`** |
| **`0x00001BE8`** | `21A00947` | `wrch MFC_EAL, $r71` | `1277` | **`MFC_EAL = $r71` (Host EA, becomes 0 on null buffer!)** |
| `0x00001BEC` | `21A009B8` | `wrch MFC_Size, $r56` | `1278` | `MFC_Size = 128` |
| `0x00001BF0` | `21A00A3E` | `wrch MFC_TagID, $r62` | `1279` | `MFC_TagID = 8` |
| `0x00001BF4` | `21A00AC0` | `wrch MFC_Cmd, $r64` | `1280` | `MFC_Cmd = 0x40` (MFC GET) |
| `0x00001BF8` | `21A00B42` | `wrch MFC_WrTagMask, $r66` | `1281` | Tag mask = 256 |
| `0x00001BFC` | `21A00BC4` | `wrch MFC_WrTagUpdate, $r68` | `1282` | Update = 2 |
| `0x00001C00` | `2082A047` | `stqa $r71, 0x1500` | `1283` | Store `$r71` to cached EA `ls[0x1500]` |
| `0x00001C04` | `2082A239` | `stqa $r57, 0x1510` | `1284` | Store `$r57` to cached header `ls[0x1510]` |
| `0x00001C08` | `01A00C38` | `rdch $r56, MFC_RdTagStat` | `1285` | Wait for DMA GET to complete |
| `0x00001C0C` | `327FDE80` | `br 0x1B00` | `1286` | Branch to `0x1B00` to process loaded buffer |

---

## 3. Detailed Static Trace of Focus Region B: 0x1C10 – 0x1C84

### 3.1 Role of Subroutine
Subroutine `0x1C10` (`spu_recomp.c:1289-1335`) is the atomic reservation decrement helper for workload dependency counters (`wws_job_pm_dec_dep`):
- Caller sets `$r3 = atomic_counter_EA`, `$r0 = return_pc`.
- It executes a standard `getllar` / `putllc` loop to atomically decrement a 16-bit counter in host RAM.
- If the counter decrements to 0, it calls `0x2F40` (`spu_recomp.c:1326`) to signal or notify the dependency completion.

### 3.2 Instruction Breakdown

| SPU LS Address | Hex Opcode | SPU Instruction | `src/gen/spu/wws_job_pm/spu_recomp.c` Line | Functional Description |
|---|---|---|---|---|
| `0x00001C10` | `42004002` | `ila $r2, 0x80` | `1290` | `$r2 = 0x80` (Atomic line LSA) |
| `0x00001C14` | `120265A0` | `hbrr 0x1C94, 0x2F40` | `1291` | Branch hint |
| `0x00001C18` | `14E00185` | `andi $r5, $r3, -128` | `1292` | `$r5 = $r3 & ~0x7F` (128-byte aligned counter EA) |
| `0x00001C1C` | `2082A800` | `stqa $r0, 0x1540` | `1293` | Save return address `$r0` to LS `0x1540` |
| `0x00001C20` | `141FC180` | `andi $r0, $r3, 127` | `1294` | `$r0 = $r3 & 0x7F` (byte offset within 128B line) |
| `0x00001C24` | `00200000` | `lnop` | `1295` | NOP |
| **`0x00001C28`** | `40806803` | `il $r3, 208` | `1300` | `$r3 = 208 = 0xD0` (`MFC_GETLLAR_CMD`) |
| `0x00001C2C` | `21A00802` | `wrch MFC_LSA, $r2` | `1301` | `MFC_LSA = 0x80` |
| `0x00001C30` | `40805A06` | `il $r6, 180` | `1302` | `$r6 = 180 = 0xB4` (`MFC_PUTLLC_CMD`) |
| `0x00001C34` | `21A00905` | `wrch MFC_EAL, $r5` | `1303` | **`MFC_EAL = $r5` (Could be 0 if `$r3 == 0`)** |
| `0x00001C38` | `1C038004` | `ai $r4, $r0, 14` | `1304` | Offset for halfword rotate |
| `0x00001C3C` | `21A00A83` | `wrch MFC_Cmd, $r3` | `1305` | Issue `getllar` |
| `0x00001C40` | `01A00D83` | `rdch $r3, MFC_RdAtomicStat` | `1306` | Wait for reservation grant |
| `0x00001C44` | `38808007` | `lqx $r7, $r0, $r2` | `1307` | **Load quadword from `($r0 + $r2) & ~0xF = 0x80..0xF0`** |
| `0x00001C48` | `3B810388` | `rotqby $r8, $r7, $r4` | `1308` | Rotate counter halfword to preferred slot |
| `0x00001C4C` | `3AA08009` | `chx $r9, $r0, $r2` | `1309` | Generate insertion mask |
| `0x00001C50` | `1DFFC40A` | `ahi $r10, $r8, -1` | `1310` | Decrement counter by 1 |
| `0x00001C54` | `3F804403` | `rotqbyi $r3, $r8, 1` | `1311` | Extract flags |
| `0x00001C58` | `3F808404` | `rotqbyi $r4, $r8, 2` | `1312` | Extract notification flag |
| `0x00001C5C` | `B0E1C509` | `shufb $r7, $r10, $r7, $r9` | `1313` | Insert decremented counter back into line |
| `0x00001C60` | `7D004408` | `ceqhi $r8, $r8, 1` | `1314` | Did counter reach zero? (`old == 1`) |
| `0x00001C64` | `21A00802` | `wrch MFC_LSA, $r2` | `1315` | `MFC_LSA = 0x80` |
| `0x00001C68` | `143FC183` | `andi $r3, $r3, 255` | `1316` | Mask flags |
| `0x00001C6C` | `21A00905` | `wrch MFC_EAL, $r5` | `1317` | `MFC_EAL = $r5` |
| `0x00001C70` | `143FC204` | `andi $r4, $r4, 255` | `1318` | Mask notification flag |
| `0x00001C74` | `28808007` | `stqx $r7, $r0, $r2` | `1319` | Store modified line to LS `0x80..0xF0` |
| `0x00001C78` | `21A00A86` | `wrch MFC_Cmd, $r6` | `1320` | Issue `putllc` (180 = `0xB4`) |
| `0x00001C7C` | `01A00D86` | `rdch $r6, MFC_RdAtomicStat` | `1321` | Read atomic reservation status |
| `0x00001C80` | `14004306` | `andi $r6, $r6, 1` | `1322` | Check reservation lost |
| `0x00001C84` | `217FF486` | `brnz $r6, 0x1C28` | `1323` | If reservation lost -> Retry loop at `0x1C28` |
| `0x00001C88` | `22000208` | `brhz $r8, 0x1C98` | `1324` | If counter did not hit 0 -> Return via `0x1C98` |
| `0x00001C8C` | `2082B406` | `stqa $r6, 0x15A0` | `1325` | Save timestamp/flag |
| `0x00001C90` | `20000104` | `brz $r4, 0x1C98` | `1326` | If no notify requested -> Return via `0x1C98` |
| `0x00001C94` | `33025580` | `brsl $r0, 0x2F40` | `1326` | Call completion notification routine |
| `0x00001C98` | `3082A800` | `lqa $r0, 0x1540` | `1333` | Restore return address from LS `0x1540` |
| `0x00001C9C` | `35000000` | `bi $r0` | `1334` | Indirect return to caller |

### 3.3 Assessment of 0x0..0xF LS Address and EA=0 Risk in Region B
- **LS Address 0x0..0xF**: The only memory load is `0x1C44: lqx $r7, $r0, $r2`. Its effective address is `($r0 + $r2) & ~0xF`. Because `$r2` is hardcoded to `0x80` (`ila $r2, 0x80` at `0x1C10`) and `$r0 = $r3 & 0x7F`, the address is strictly confined to `0x80..0xF0`. It **cannot** load from `0x0..0xF`.
- **EA == 0**: If a caller invokes `0x1C10` with `$r3 == 0` (null dependency counter pointer), `$r5` evaluates to `0x00000000`. In that case, `getllar` (`0x1C3C`) and `putllc` (`0x1C78`) will issue atomic operations to host EA `0x00000000`. However, inspection of callers in `wws_job_pm.bin` (`0x2C44`, `0x2C58`, `0x2C6C`, `0x2C80`) shows they all guard the call with `brnz $r3, 0x1C10` (`0x2C58`, `0x2C6C`, `0x2C80`) or `brnz $r80, 0x1C10` (`0x2C44`).

---

## 4. Detailed Static Trace of Focus Region C: 0x1E48 (`AuditEndJob`)

### 4.1 Instruction Breakdown

| SPU LS Address | Hex Opcode | SPU Instruction | `src/gen/spu/wws_job_pm/spu_recomp.c` Line | Functional Description |
|---|---|---|---|---|
| **`0x00001E48`** | `3081BA02` | `lqa $r2, 0xDD0` | `1474` | Load audit state word 1 from LS `0xDD0` |
| `0x00001E4C` | `3081B803` | `lqa $r3, 0xDC0` | `1475` | Load audit state word 0 from LS `0xDC0` |
| `0x00001E50` | `08208182` | `or $r2, $r3, $r2` | `1476` | `$r2 = ls[0xDD0] \| ls[0xDC0]` |
| `0x00001E54` | `3081B003` | `lqa $r3, 0xD80` | `1477` | Load audit buffer pointer from LS `0xD80` |
| `0x00001E58` | `141FC184` | `andi $r4, $r3, 127` | `1478` | Check 128B alignment of buffer pointer |
| **`0x00001E5C`** | `20001282` | `brz $r2, 0x1EF0` | `1478` | **IF AUDITING DISABLED (`$r2 == 0`) -> EARLY RETURN TO `0x1EF0`!** |
| `0x00001E60` | `40800002` | `il $r2, 0` | `1479` | Zero vector for padding |
| `0x00001E64` | `20000584` | `brz $r4, 0x1E90` | `1480` | If already 128B aligned -> Skip pad loop |
| `0x00001E68` | `34000184` | `lqd $r4, 0x0($r3)` | `1487` | Load audit buffer tail |
| `0x00001E6C` | `3EE00185` | `cdd $r5, $r3, 0` | `1488` | Count doublewords |
| `0x00001E70` | `1C020186` | `ai $r6, $r3, 8` | `1489` | Advance pointer by 8 |
| `0x00001E74` | `B0810105` | `shufb $r4, $r2, $r4, $r5` | `1490` | Pad entry with zeroes |
| `0x00001E78` | `141FC305` | `andi $r5, $r6, 127` | `1491` | Check alignment |
| `0x00001E7C` | `24000184` | `stqd $r4, 0x0($r3)` | `1492` | Store padded quadword |
| `0x00001E80` | `1C000303` | `ai $r3, $r6, 0` | `1493` | Update buffer pointer |
| `0x00001E84` | `217FFC85` | `brnz $r5, 0x1E68` | `1494` | Loop until 128B aligned |
| `0x00001E88` | `2081B003` | `stqa $r3, 0xD80` | `1495` | Store aligned pointer to LS `0xD80` |
| `0x00001E8C` | `337FCFCE` | `brsl $r78, 0x1D08` | `1496` | Call audit buffer flush helper |
| `0x00001E90` | `40804002` | `il $r2, 128` | `1501` | Tag mask = 128 (Tag 7) |
| `0x00001E94` | `3081B403` | `lqa $r3, 0xDA0` | `1502` | Load job stop timestamp from LS `0xDA0` |
| `0x00001E98` | `40800104` | `il $r4, 2` | `1503` | Tag update = 2 |
| `0x00001E9C` | `21A00B02` | `wrch MFC_WrTagMask, $r2` | `1504` | Set tag mask |
| `0x00001EA0` | `420AA002` | `ila $r2, 0x1540` | `1505` | `$r2 = 0x1540` (LS address for audit record) |
| `0x00001EA4` | `21A00B84` | `wrch MFC_WrTagUpdate, $r4` | `1506` | Tag wait |
| `0x00001EA8` | `40800805` | `il $r5, 16` | `1507` | Size = 16 bytes |
| `0x00001EAC` | `01A00C06` | `rdch $r6, MFC_RdTagStat` | `1508` | Wait for tag 7 |
| `0x00001EB0` | `40800406` | `il $r6, 8` | `1509` | Tag ID = 8 |
| `0x00001EB4` | `3081B207` | `lqa $r7, 0xD90` | `1510` | Load job start timestamp from LS `0xD90` |
| `0x00001EB8` | `0801C183` | `sf $r3, $r3, $r7` | `1511` | `$r3 = stop_ts - start_ts` (elapsed cycles) |
| `0x00001EBC` | `21A00802` | `wrch MFC_LSA, $r2` | `1512` | `MFC_LSA = 0x1540` |
| `0x00001EC0` | `0F3F4183` | `rotmi $r3, $r3, -3` | `1513` | Divide elapsed cycles by 8 |
| `0x00001EC4` | `21A00985` | `wrch MFC_Size, $r5` | `1514` | `MFC_Size = 16` |
| `0x00001EC8` | `3081BC05` | `lqa $r5, 0xDE0` | `1515` | Load Audit Report EA from LS `0xDE0` |
| `0x00001ECC` | `24000103` | `stqd $r3, 0x0($r2)` | `1516` | Store formatted record to LS `0x1540` |
| `0x00001ED0` | `40801002` | `il $r2, 32` | `1517` | `$r2 = 32 = 0x20` (`MFC_PUT_CMD`) |
| `0x00001ED4` | `21A00A06` | `wrch MFC_TagID, $r6` | `1518` | `MFC_TagID = 8` |
| `0x00001ED8` | `40808003` | `il $r3, 256` | `1519` | Tag mask = 256 |
| `0x00001EDC` | `21A00905` | `wrch MFC_EAL, $r5` | `1520` | `MFC_EAL = ls[0xDE0]` (Audit host EA) |
| `0x00001EE0` | `21A00A82` | `wrch MFC_Cmd, $r2` | `1521` | **Issue DMA PUT (`0x20`) to write audit record** |
| `0x00001EE4` | `21A00B03` | `wrch MFC_WrTagMask, $r3` | `1522` | Tag mask = 256 |
| `0x00001EE8` | `21A00B84` | `wrch MFC_WrTagUpdate, $r4` | `1523` | Tag wait = 2 |
| `0x00001EEC` | `01A00C02` | `rdch $r2, MFC_RdTagStat` | `1524` | Wait for DMA PUT completion |
| **`0x00001EF0`** | `4020007F` | `nop` | `1529` | Return target from `0x1E5C` |
| `0x00001EF4` | `35000000` | `bi $r0` | `1530` | Return to caller |

### 4.2 Why Execution Bypasses 0x1E48
1. **Disabled in Production**: At initialization routine `0x1D60` (`spu_recomp.c:1412-1422`):
   ```c
   spu_ls_write128(ctx, 0xDD0, ctx->gpr[2]); // gpr[2] is 0
   spu_ls_write128(ctx, 0xDC0, ctx->gpr[2]); // gpr[2] is 0
   if (ctx->gpr[4]._u32[0] == 0) { SPU_RET(ctx); } // wkl_data hi32 == 0
   ```
   Because God of War: Ascension does not pass an audit header (high 32-bits of `wkl_data` is 0, see `ps3recomp/runtime/spu/spurs_policy.c:300-325`), `ls[0xDD0]` and `ls[0xDC0]` remain permanently zero.
2. **Deterministic Early Return**: Whenever `0x1E48` is entered, `0x1E5C: brz $r2, 0x1EF0` evaluates `ls[0xDD0] | ls[0xDC0] == 0` as true and returns immediately at `0x1EF0`. The DMA PUT at `0x1EE0` is **never executed**.
3. **Lifter Gap at Callsite `0x23C0`**:
   In `wws_job_pm.bin`, the only branch to `0x1E48` is `0x23C0: brsl $r0, 0x1E48`.
   However, in `src/gen/spu/wws_job_pm/spu_recomp.c:1925-1932`, `spu_wws_job_pm_spu_func_000023A0` ends prematurely on the indirect branches `0x23A4` (`bie $r2`) and `0x23AC` (`bid $r0`). Instructions `0x23B0` through `0x23DC` are entirely absent from the lifted C code. Hence, in the recompiled binary, `0x1E48` is dead code.

---

## 5. Comprehensive Synthesis: The Root Cause of the Wid 8 Loop

Putting all pieces together:

```
[Main Memory: Job List @ 0x009FDF80]
  +0x00: 0077CA80 00003000 58002000 FFFFFFFC (State, cursors)
  +0x30: 747E3580 747E7580 00000000 00000000 (Buffer 0, 1 valid; Buffer 2, 3 NULL)
                      │
                      │ 1. Subroutine 0x1A48 reads Job List +0x30 via lqd @ 0x1A84
                      │ 2. When cursor references Buffer slot 2 or 3: rotqby extracts 0x00000000
                      ▼
[Local Store @ 0x1BE4 - 0x1BE8]
  wrch MFC_LSA, 0xD00
  wrch MFC_EAL, 0x00000000
  wrch MFC_Cmd, 0x40 (GET) ────► Issues: [spurs-pm] DMA cmd=0x40 lsa=0xD00 ea=0
                      │
                      │ 3. DMA copies 128 bytes from guest EA 0 to LS 0xD00
                      │    (Or if null DMA is rejected/stale, LS 0xD00 retains existing data)
                      ▼
[Local Store @ 0x1B04 - 0x1B0C]
  lqx    $r3, 0xD00, $r61   ────► Loads quadword from LS 0xD00
  rotqby $r3, $r3, $r61     ────► If word 0 contains 0x30014580 (or dereferences LS 0):
                                  $r3 = 0x30014580
                      │
                      │ 4. rotqbyi $r63, $r3, 4 extracts word 1:
                      │    $r63 = 0x30014580
                      ▼
[Local Store @ 0x1B70 - 0x1B84]
  stqa   $r63, 0x1530       ────► Stores 0x30014580 into ls[0x1530]!
  wrch   MFC_EAL, 0x30014580────► Issues 16-byte GET from bogus EA 0x30014580
  brhz   $r57, 0x1B40       ────► Halfword counter == 0 (still pending)
                      │
                      │ 5. Branches to 0x1B40 (putllc to commit cursor)
                      ▼
[Local Store @ 0x1B5C]
  brhz   $r69, 0x1A60       ────► Command byte 0x30 != 1 ($r69 == 0)
                                  Unconditionally branches back to 0x1A60!
                      │
                      ▼
[Tight Spin Loop: 0x1A60 ◄──► 0x1B7C]
  - Iteration 2+: ceq $r57, $r63, $r58 at 0x1B70 matches (both 0x30014580)
  - brnz $r57, 0x1B40 immediately skips DMA
  - putllc executed
  - brhz $r69, 0x1A60 jumps to 0x1A60
  - WID 8 IS STUCK IN AN INFINITE SPIN LOOP!
```

---

## 6. Proposed Read-Only Instrumentation Points

To capture live execution and definitively confirm each stage of this sequence without altering guest behavior or modifying `src/gen/`, the following non-invasive, read-only diagnostic hooks are proposed (to be placed in `src/compat/upstream_compat.cpp`):

### 6.1 Probe 1: DMA Buffer Source & Content Capture at 0x1BE4 / 0x1BE8
- **Location**: In `gow_spu_step_hook(spu_context* ctx)` or via `ps3recomp` SPU DMA callback.
- **Trigger Condition**: `ctx->image_id == 2 && (ctx->pc & SPU_LS_MASK) == 0x1BE4`.
- **Logged Data**:
  ```c
  uint32_t lsa = ctx->gpr[3]._u32[0];
  uint32_t ea  = ctx->gpr[71]._u32[0];
  uint32_t sz  = ctx->gpr[56]._u32[0];
  uint32_t buf_sel = ctx->gpr[61]._u32[0];
  fprintf(stderr, "[WWS-PROBE 0x1BE4] wid=%u spu=%u LSA=0x%05X EA=0x%08X size=%u buf_offset=%u tag=%u\n",
          ctx->ls[0x1DC], ctx->spu_id, lsa, ea, sz, buf_sel, ctx->gpr[62]._u32[0]);
  ```
- **Diagnostic Value**: Identifies the exact moment `ea == 0` is passed to the MFC, and reveals which buffer index (`$r61`) produced the zero EA.

### 6.2 Probe 2: Command & Dependency Extraction Capture at 0x1B04 / 0x1B10
- **Location**: `src/compat/upstream_compat.cpp` in `gow_spu_step_hook`.
- **Trigger Condition**: `ctx->image_id == 2 && (ctx->pc & SPU_LS_MASK) == 0x1B10`.
- **Logged Data**:
  ```c
  uint32_t cmd_raw = ctx->gpr[3]._u32[0];
  uint32_t dep_ea  = ctx->gpr[63]._u32[0];
  uint32_t old_cached = ctx->gpr[58]._u32[0];
  fprintf(stderr, "[WWS-PROBE 0x1B10] wid=%u spu=%u cmd_word=0x%08X dep_ea=0x%08X old_cached=0x%08X ls[0x1530]=0x%08X\n",
          ctx->ls[0x1DC], ctx->spu_id, cmd_raw, dep_ea, old_cached,
          *(uint32_t*)(ctx->ls + 0x1530));
  ```
- **Diagnostic Value**: Catches the exact value loaded by `lqx $r3, $r3, $r61` before branching. If `cmd_word == 0x30014580`, it proves whether it came from `ctx->ls[0xD00]` or a null LS index.

### 6.3 Probe 3: Null DMA Destination Payload Inspector
- **Location**: In `ps3recomp/runtime/spu/spu_dma.h` (or guest memory read hook in `src/compat/`).
- **Trigger Condition**: `spu->image_id == 2 && cmd == 0x40 && (uint32_t)ea == 0 && lsa == 0xD00`.
- **Logged Data**:
  ```c
  fprintf(stderr, "[WWS-PROBE NULL-DMA] spu=%u copying 128B from vm_base+0 to LS 0xD00. First 16 bytes at vm_base: %08X %08X %08X %08X\n",
          spu->spu_id,
          *(uint32_t*)(vm_base), *(uint32_t*)(vm_base + 4),
          *(uint32_t*)(vm_base + 8), *(uint32_t*)(vm_base + 12));
  ```
- **Diagnostic Value**: Settles definitively whether `vm_base + 0` contains `0x30014580` or `0x00000000`, distinguishing Hypothesis A (DMA GET from EA 0 loaded vector words) from Hypothesis B (LS null dereference).

### 6.4 Probe 4: Job List Buffer Table Inspector
- **Location**: SPU entry hook or PPU dispatch hook in `src/compat/upstream_compat.cpp`.
- **Trigger Condition**: When `wid == 8` is scheduled on an SPU lane.
- **Logged Data**:
  ```c
  uint32_t jl_ea = 0x009FDF80;
  if (vm_base) {
      uint32_t b0 = *(uint32_t*)(vm_base + jl_ea + 0x30);
      uint32_t b1 = *(uint32_t*)(vm_base + jl_ea + 0x34);
      uint32_t b2 = *(uint32_t*)(vm_base + jl_ea + 0x38);
      uint32_t b3 = *(uint32_t*)(vm_base + jl_ea + 0x3C);
      fprintf(stderr, "[WWS-PROBE JL-BUFS] wid=8 buffers: b0=0x%08X b1=0x%08X b2=0x%08X b3=0x%08X\n",
              __builtin_bswap32(b0), __builtin_bswap32(b1),
              __builtin_bswap32(b2), __builtin_bswap32(b3));
  }
  ```
- **Diagnostic Value**: Identifies why Buffer slots 2 and 3 are NULL in the PPU-built Job List, and whether the PPU intended for this workload to have 2 or 4 double-buffered rings.
