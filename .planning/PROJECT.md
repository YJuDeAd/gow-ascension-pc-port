# PROJECT.md — God of War: Ascension (PS3) Static Recompilation Port

## Identity

| Field | Value |
|---|---|
| **Project** | God of War: Ascension (PS3) → Native x86-64 PC Port |
| **Title ID** | BCUS98232 (NA) / BCES01715 (EU) |
| **Engine** | Santa Monica Studio proprietary (GoW engine) |
| **Repo Root** | `C:\IfItWorksItWorks\Project\gowassension` |
| **Planning Model** | Gemini Flash 3.8 High (writes PLAN.md) |
| **Execution Model** | Gemini Flash 3.8 High (executes PLAN.md in fresh context) |
| **Start Date** | 2026-10-16 |

## Objective

Build a native x86-64 Windows executable of **God of War: Ascension** (PS3, 2013) via static recompilation using the `ps3recomp` toolchain. The port uses `sp00nznet/ps3recomp` as the runtime/toolchain and `sp00nznet/twistedmetal` as the structural reference for how a full PS3 AAA title is brought up on PC.

### Success Criteria (Milestone 1)

1. `build/gow_ascension.exe` links with 0 unresolved symbols
2. The executable boots through CRT init, enters `main()`, and mounts FIOS
3. RSX commands flow to D3D12 and at least a 2D loading screen is visible
4. SPU jobs (Edge Zlib, SPURS JobQueue, libedgegeom) dispatch and return on host CPU

## Architecture

```
┌────────────────────────────────────────────────────┐
│  God of War: Ascension Port (this repo root)       │
│  ./src/          ← hand-written port code          │
│  ./src/gen/      ← machine-generated (ppu_lifter)  │
│  ./src/spu_gen/  ← machine-generated (spu_lifter)  │
│  ./src/recomp/   ← lifted PPU headers              │
│  ./src/compat/   ← PSN stubs, Win shims            │
│  ./tools/        ← Python/PS1 automation            │
│  ./CMakeLists.txt                                   │
│  ./build/gow_ascension.exe                          │
├────────────────────────────────────────────────────┤
│  ./ps3recomp/    ← READ-ONLY upstream toolchain     │
│    runtime/      ← ppu_loader, ppu_hle, ppu_fs      │
│    include/      ← ps3emu headers                    │
│    libs/         ← HLE modules (cellGcmSys, etc.)    │
│    tools/        ← elf_parser, ppu_lifter, etc.      │
│    build/ps3recomp_runtime.lib                       │
├────────────────────────────────────────────────────┤
│  ./twistedmetal/ ← READ-ONLY reference port          │
│    src/hle_extra.cpp ← FIOS fix, SPURS v2, etc.     │
│    docs/         ← bringup, first-pixels, in-match   │
├────────────────────────────────────────────────────┤
│  ./vfs/PS3_GAME/USRDIR/EBOOT.ELF  (8.2 MB)          │
│  ./vfs/PS3_GAME/USRDIR/...        (game assets)     │
└────────────────────────────────────────────────────┘
```

## Constraints

| Constraint | Detail |
|---|---|
| **Disk Space** | C: has ~45 GB free. Compile `src/gen/` with `-O2`/`/O2` without `/Zi` PDB. No debug builds on gen code. |
| **Toolchain** | `clang-cl` + Ninja + CMake 3.20+. clang-cl required for `__builtin_bswap` and weak symbols. |
| **Read-Only Repos** | `./ps3recomp/` and `./twistedmetal/` are never modified. All port code lives in `./src/`, `./tools/`, `./docs/`. |
| **No Hand-Edit gen/** | `src/gen/` is machine output from `ppu_lifter.py`/`spu_lifter.py`. Never read into LLM context. |
| **Windows Host** | All scripts must be Python `.py` or PowerShell `.ps1`, never Bash-only. |
| **PLAN.md Protocol** | Claude Opus 4.6 writes PLANs with exact CLI commands and `<verify>` steps; Gemini Flash 3.8 High executes. |

## Key Reference Patterns (from Twisted Metal)

### CMakeLists.txt Structure
- `PS3RECOMP_DIR` → `../ps3recomp`, `PS3RECOMP_RUNTIME_LIB` → `ps3recomp_runtime.lib`
- Source dirs: `src/recomp/*.cpp`, `src/gen/*.cpp`, `src/spu_gen/*.c`
- Runtime files compiled in: `ppu_loader.cpp`, `ppu_hle.cpp`, `ppu_sysprx.cpp`, `ppu_fs.cpp`
- Win32 libs: `ws2_32 bcrypt xinput ole32 d3d12 dxgi dxguid d3dcompiler`
- Stack: `/STACK:268435456` (256 MB for deep recompiled call chains)

### HLE Override Pattern (`hle_extra.cpp`)
- `sys_lwcond_create` struct layout fix (32-bit vs 64-bit EA)
- `sys_ppu_thread_once` must run the initializer
- SPURS v2 → v1 forwarding (`cellSpursCreateTaskset2` → `cellSpursCreateTaskset`)
- Game-specific NID stubs for PSN/multiplayer

### SPU/SPURS Pattern
- `extract_spu_images.py` finds embedded SPU ELFs
- `spu_lifter.py` lifts each to C
- SPURS JobQueue needs real HLE (not stubs): push (0x90E392CF), sync (0x9396BE1D)
- Edge Zlib decompressor → host DEFLATE (`tm_inflate.cpp` pattern)
- JOBCRT Ver13 needs kernel block at `CellSpursJobContext2+0x14`

### RSX Bring-Up Pattern
- Start with null backend → state tracking → D3D12
- Surface size must match game's actual render target
- RSX labels (NV406E OFFSET/ACQUIRE/RELEASE) must be implemented
- FIFO walker needs park-wait and resync mechanisms

## Binary Facts

| Metric | Value |
|---|---|
| `EBOOT.ELF` size | 8,584,256 bytes (8.2 MB) |
| ELF format | ELF64 big-endian PowerPC64 |
| Expected functions | 10,000–50,000 (AAA title) |
| Expected imports | 200–500 (complex engine) |
| Expected SPU images | Multiple (Edge Zlib, Edge Geom, audio, physics) |

## Team / Model Roles

| Role | Model | Responsibility |
|---|---|---|
| **Planner** | Claude Opus 4.6 | Writes PLAN.md, ROADMAP, architectural decisions |
| **Executor** | Gemini Flash 3.8 High | Executes PLAN.md steps in fresh context windows |
| **Knowledge Graph** | Graphify | All agents query `graphify query` before inventing solutions |
