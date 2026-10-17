# docs/vfs-streaming.md — Zero-Disk-Copy Asset Streaming Report

## Architecture Overview

God of War: Ascension (*BCAS25016* / *BCUS98232*) ships on a dual-layer Blu-ray disc containing **~36.16 GB** of asset packages, primarily stored as compressed PlayStation Archives (`.PSARC`) in `/dev_bdvd/PS3_GAME/USRDIR/`.

Host storage constraints (**LAW 4**) restrict host drive `C:` to approximately 45 GB of free space. Copying the 36 GB asset tree to `C:` would immediately exhaust host storage and risk compile/link aborts.

To achieve full asset availability with **zero host disk copy**, the decrypted ISO image:
`C:\IfItWorksItWorks\Project\gowassension\God of War - Ascension (Asia) (En,Zh,Ko).dec.iso`
is mounted directly as a virtual CD-ROM drive:
- **Mount Drive:** `E:\`
- **Volume Label:** `PS3VOLUME`
- **File System:** CD-ROM (ISO 9660 / UDF)
- **Disk Usage on C:** **0 Bytes**

---

## VFS Path Translation Pipeline

The native port runtime translates guest PS3 virtual paths through a two-tiered resolution mechanism:

```
Guest Path: /dev_bdvd/PS3_GAME/USRDIR/GAME.PSARC
                       │
                       ▼
         ppu_fs.cpp (host_path)
   checks $env:PS3_VFS_ROOT / auto-detects E:
                       │
                       ▼
Host File: E:/PS3_GAME/USRDIR/GAME.PSARC (mounted ISO)
```

1. **Auto-Detection in `src/main.cpp`:**
   - `derive_vfs_root()` checks `$env:PS3_VFS_ROOT`.
   - If unset, it probes `E:\PS3_GAME\USRDIR`. If the directory exists, `ppu_vfs_root` automatically defaults to `"E:"`.
   - Otherwise, it falls back to the directory containing the loaded `EBOOT.ELF` (`./vfs`).
2. **Metadata Fallback in `sys_fs.c` / `ppu_fs.cpp`:**
   - Path lookups that require local decrypted files (such as `EBOOT.ELF`) are served from `./vfs/PS3_GAME/USRDIR/EBOOT.ELF`.
   - Path lookups for disc assets (`ARCHIVES`, `.PSARC`, `PARAM.SFO`, `ICONS/ICON0.PNG`) resolve directly against virtual drive `E:`.

---

## Verified Archive Read Log

Execution verification via `./tools/run_port.ps1` confirmed that the engine opens and reads all initial archives:

| Guest Path | Host File | File Descriptor | Magic / Header | Status |
|---|---|---|---|---|
| `/dev_bdvd/PS3_GAME/USRDIR/ARCHIVES` | `E:/PS3_GAME/USRDIR/ARCHIVES` | fd 3 | `31390D0A` (ASCII text list) | **OK (264 bytes read)** |
| `/dev_bdvd/PS3_GAME/USRDIR/GAME.PSARC` | `E:/PS3_GAME/USRDIR/GAME.PSARC` | fd 4 | `50534152` (`PSAR` header) | **OK (7,344 bytes read)** |
| `/dev_bdvd/PS3_GAME/USRDIR/SHELL.PSARC` | `E:/PS3_GAME/USRDIR/SHELL.PSARC` | fd 5 | `50534152` (`PSAR` header) | **OK (11,386 bytes read)** |
| `/dev_bdvd/PS3_GAME/USRDIR/INTROCIN.PSARC` | `E:/PS3_GAME/USRDIR/INTROCIN.PSARC` | fd 6 | `50534152` (`PSAR` header) | **OK (23,070 bytes read)** |
| `/dev_bdvd/PS3_GAME/USRDIR/RESOURCES.PSARC` | `E:/PS3_GAME/USRDIR/RESOURCES.PSARC` | fd 7 | `50534152` (`PSAR` header) | **OK (165,042 bytes read)** |
| `/dev_bdvd/PS3_GAME/USRDIR/HECA.PSARC` | `E:/PS3_GAME/USRDIR/HECA.PSARC` | fd 8 | `50534152` (`PSAR` header) | **OK (259,094 bytes read)** |

---

## Runner Script Automation

The runner script [`tools/run_port.ps1`](file:///C:/IfItWorksItWorks/Project/gowassension/tools/run_port.ps1) automates this pipeline:
- Validates whether `E:\PS3_GAME\USRDIR\GAME.PSARC` is mounted; mounts the ISO automatically if unmounted.
- Sets `$env:PS3_VFS_ROOT = "E:"`.
- Launches `build/gow_ascension.exe` with a configurable timeout (default 15 seconds) to prevent terminal hang during background thread execution.
