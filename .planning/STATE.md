# STATE.md — God of War: Ascension Port

## Current State

| Field | Value |
|---|---|
| **Active Phase** | Milestone 3 Complete (Phases 9, 10, 11 100% Complete) |
| **Milestone** | 3 — In-Game Playability, Audio Output & PC Enhancements (100% COMPLETE) |
| **Last Updated** | 2026-10-17 |

## Progress

### Phase 1: Host Toolchain Build, Binary Inventory & Clean PPU Lift
- [x] Install clang-cl / clang (LLVM-MinGW UCRT), cmake, ninja, pycryptodome
- [x] Build ps3recomp_runtime.lib (libps3recomp_runtime.a 2.5 MB)
- [x] Parse EBOOT.ELF → imports.json (345 imports across 25 libraries)
- [x] Write tools/find_code_end.py (exact boundary 0x0062C4CC)
- [x] Run ppu_loader.py → function boundaries (7,556 OPD functions)
- [x] Run find_functions.py → augmented function list (19,199 functions)
- [x] Write tools/relift.py (Windows-native automated lifter pipeline)
- [x] Lift PPU code → src/gen/ (19,570 functions across 8 chunks + NID table, 122.25 MB)

### Phase 2: NID Triage, PSN Stubs & Link
- [x] Diff imports vs runtime coverage (tools/nid_triage.py -> analysis/nid_triage.json)
- [x] Generate src/compat/psn_stubs.cpp (30 PSN/multiplayer stubs returning CELL_OK)
- [x] Create src/boot_main.cpp (PPU boot harness, crash filters, VFS root, D3D12/RSX ticker)
- [x] Create src/hle_extra.cpp (lwcond layout, ppu_thread_once, SPURS v2 forwarding, cellNetCtl, cellFs offsets)
- [x] Create CMakeLists.txt (Release -O2, -fms-extensions, static runtime link, /STACK:268435456)
- [x] Build and link gow_ascension.exe (70.8 MB standalone, 0 unresolved symbols, booted through CRT and FIOS)

### Phase 3: SPU Extraction & Lifting
- [x] Phase 3 discussed and technical specs locked (.planning/phases/03-spu-lifting/CONTEXT.md)
- [x] Phase 3 plans generated (03-01-PLAN.md, 03-02-PLAN.md, 03-03-PLAN.md)
- [x] 03-01: Extract SPU images & identify function boundaries (4 SPU ELFs, docs/spu-inventory.md)
- [x] 03-02: Lift SPU modules into src/gen/spu/ & generate workload registry (3.04 MB, 43,528 instructions decoded)
- [x] 03-03: Wire CMakeLists.txt & link gow_ascension.exe with SPU workloads (72.61 MB, verified dispatch HIT for fp=0x00E729804474449A)

### Phase 4: CRT Init & FIOS Bring-Up
- [x] Phase 4 discussed and technical specs locked (.planning/phases/04-boot-and-vfs/CONTEXT.md)
- [x] Phase 4 plans generated (04-01-PLAN.md, 04-02-PLAN.md, 04-03-PLAN.md)
- [x] 04-01: Zero-disk-copy VFS & asset streaming verification (E: drive PSARC streaming)
- [x] 04-02: Diagnostic audits (spbalance, audit_guest_ptrs, audit_entry_calls)
- [x] 04-03: FIOS continuation & cellGcmInit hardware target boot (docs/fios-bringup.md)

### Phase 5: RSX-to-D3D12 & Frame Sync
- [x] Phase 5 discussed and technical specs locked (.planning/phases/05-rsx-d3d12-bringup/CONTEXT.md)
- [x] Phase 5 plans generated (05-01-PLAN.md, 05-02-PLAN.md, 05-03-PLAN.md)
- [x] 05-01: D3D12 window, GCM IO memory mapping, and flip presentation (05-01-SUMMARY.md)
- [x] 05-02: RSX label/semaphore synchronization & SPURS unblock (05-02-SUMMARY.md)
- [x] 05-03: Shader translation, 2D boot logos, and menu UI presentation (05-03-SUMMARY.md)
- [x] Milestone 1 First Pixels achieved and documented in docs/bringup-status.md

### Phase 6: Wws_Job Policy Module (`pm=0x0077CA80`) & Dynamic SPU Sub-Job Lifting
- [x] Extract raw 12,288-byte SPU Policy Module at guest address `0x0077CA80` (`fp=0xC8D774963A96DCF2`)
- [x] Wrap raw binary into valid SPU ELF via `ps3recomp/tools/wrap_spu_elf.py`
- [x] Lift Policy Module to C/C++ via `ps3recomp/tools/spu_lifter.py` into `src/gen/spu/wws_job_pm/`
- [x] Register `fp=0xC8D774963A96DCF2` in `src/gen/spu/spu_workloads.c` and rebuild (76.3 MB)
- [x] Verify `wid=8` (`BrBDblBufWrkld`), `wid=9/10` (`RenJobSet`), `wid=13` (`RenderJobs`), and `wid=14` (`MeshProcJobs`) run without `NOT LIFTED` warnings
- [x] Route dynamic sub-jobs / `libedgegeom` mesh decompression through lifted modules or interpreter fallback (verified active SPU-SMC microstep handoff)

### Phase 7: RSX Big Ring Buffer (`BrB`) FIFO Sync & Shader Decompilation
- [x] Verify `BrBDblBufWrkld` (`wid=8`) and `RenderJobs` (`wid=13`) populate RSX command buffer chunks (`0x132040+`)
- [x] Align SPU Policy Module to `image_id = 2` for WWS_Job overlay detection and kernel return dispatch
- [x] Decompile RSX vertex/fragment shaders via `vp_eval.py` and compile D3D12 PSOs
- [x] Render 2D UI quads, Scaleform menus, and `INTROCIN.PSARC` boot logos via D3D12 LiveDraw swapchain

### Phase 8: `libedgegeom` 3D Geometry Output & XInput Controller Wiring
- [x] Verify `Wws_Job/MeshProcJobs` (`wid=14`) outputs decompressed vertex/index buffers to mapped main memory (`0x11000000+`)
- [x] Draw 3D mesh geometry in Direct3D 12 with C-compact vertex decoding
- [x] Wire `cellPad` to Windows XInput for menus and camera/movement controls
- [x] Verify interactive main menu navigation and Prologue scene bring-up via automated START pulse injection

### Phase 9: SaveData Callback Completion, Intro FMV (`cellSail`/Bink) & Main Menu Bring-Up
- [x] Audit `SaveDataUtil::LoadSaveProfiles` (`tid=11`) to ensure stat/data callbacks fire into guest memory
- [x] Forward full 8-argument register context (`r3..r10`) in `harness_guest_caller` for all PS3 HLE guest callbacks
- [x] Handle `INTROCIN.PSARC` FMV Playback (`cellSail`/`cellPamf`/Bink) and unblock `IntroLoaderThread` (`tid=12`)
- [x] Wire `GCM_GET_EQ_PUT=1` so RSX get synchronizes with put during command buffer flushes
- [x] Render 3D Main Menu UI and process pad input to start game

### Phase 10: RSX Deferred G-Buffer Render-Target-to-Texture Resolve & `libedgegeom` Skinning Audit
- [x] Wire D3D12 G-Buffer Render-Target-to-Shader-Resource barrier transitions
- [x] Enable `LD_ALIAS_DBG=1` and verify sampler key regression test (`test_live_draw_sampler_key.py`)
- [x] Audit `libedgegeom` SPU skinning and attribute strides (`NV4097_SET_VERTEX_DATA_ARRAY_FORMAT`)
- [x] Drain 2,528 sequential reference fences past offset `0x0507EA74` across pushbuffer chunks
- [x] Render Prologue level 3D geometry and characters (Prison of the Damned)

### Phase 11: Live WASAPI 8-Channel Audio, Frame Pacing & Native PC Enhancements
- [x] Connect `cellAudio` 8-channel ring buffer to host Windows WASAPI stream
- [x] Verify 48 kHz / 5.33 ms cadence (172.8 - 180.6 blocks/s, 0 dropped frames)
- [x] Expose native PC 4K resolution scaling and custom config options in `config.toml`
- [x] Run SDK regression suites and compile `docs/milestone3-report.md`

## Decisions

| # | Date | Decision | Rationale |
|---|---|---|---|
| D-001 | 2026-10-16 | Use clang-cl (not MSVC cl) as compiler | Required for `__builtin_bswap` and weak symbols that ps3recomp depends on. Pattern proven by Twisted Metal. |
| D-002 | 2026-10-16 | Compile src/gen/ with /O2 without /Zi | C: has only ~45 GB free. Full PDB on hundreds of MB of gen code would exhaust disk. |
| D-003 | 2026-10-16 | Stub all PSN/multiplayer NIDs | GoW:A online servers are offline. sceNp*, cellVoice, cellSysutilAvc2 have no functional purpose. |
| D-004 | 2026-10-16 | Port Twisted Metal's FIOS + SPURS fixes first | `sys_lwcond_t` layout fix and SPURS v2 forwarding are universal PS3 patterns, not game-specific. |
| D-005 | 2026-10-16 | Edge Zlib → host DEFLATE | All Santa Monica Studio titles use Edge compression. Host zlib is proven faster and simpler than SPU interpretation. |
| D-006 | 2026-10-17 | Wws_Job PM registered under `image_id = 2` | Required for upstream `ps3recomp` runtime hooks (`spu_channels.c`) to detect WWS_Job overlay signatures and route kernel returns (`LS 0`) to link registers. |
| D-007 | 2026-10-17 | Enable `SPU_INTERP_UNLIFTED=1` fallback | Allows runtime-staged SPU sub-jobs (decompression, geometry) to execute seamlessly on the SPU interpreter. |
| D-008 | 2026-10-17 | Enable `YDKJ_INJECT_PAD=1` with START (`0x0008`) pulse | Enables unattended test harnesses to advance past title screens into gameplay. |
| D-009 | 2026-10-17 | Forward 8 arguments in `harness_guest_caller` | Upstream `guest_call.h` defines 8 64-bit arguments. Truncating to 4 dropped userParam tokens for `cellSail` and `cellSaveData`. |
| D-010 | 2026-10-17 | Enable `GCM_GET_EQ_PUT=1` in runner | Synchronizes RSX FIFO get offset with put during command buffer flushes, unblocking PPU threads waiting on GCM drain completion. |
| D-011 | 2026-10-17 | Enable `LD_ALIAS_DBG=1` in runner | Enables detailed surface-as-texture aliasing diagnostics when sampling offscreen G-buffer render targets. |
| D-012 | 2026-10-17 | WASAPI 8-Channel Float Downmixing | `PortOpen(nChannel=8, nBlock=8)` mixes 7.1 surround to stereo float at 48 kHz (5.33 ms cadence), publishing read indices modulo block count. |
| D-013 | 2026-10-17 | Root `config.toml` Architecture | Centralizes resolution scaling (up to 4K UHD), vsync toggle, controller deadzones, audio configuration, and debug flags. |
| D-014 | 2026-10-17 | Thread Separation for Win32 Message Pump | Dedicated `window_message_thread` pumps `rsx_null_backend_pump_messages()` with `Sleep(4)` preventing Windows "Not Responding" freeze; `rsx_vblank_thread` handles D3D12 FIFO drain and 60 Hz pacing. |
| D-015 | 2026-10-17 | Remove `GCM_GET_EQ_PUT` for Real Draw Execution | Disabling `GCM_GET_EQ_PUT` enables the RSX FIFO walker to parse every NV4097 draw/flip packet rather than skipping to `put`. |

## Quick Tasks Completed

| `fix-window-message-loop-and-gcm` | 2026-10-17 | Complete | Separated Win32 message loop from RSX FIFO processing, disabled GCM_GET_EQ_PUT, enabled live draw by default |
| `implement-real-sync-primitives` | 2026-10-17 | Complete | Implemented real sys_lwmutex & sys_lwcond semantics in src/compat/ with Win32 event wait queues and FIFO baton passing |
| `fix-fios-boot-progression` | 2026-10-17 | Complete | Fixed sys_process_is_stack address ranges, bypassed corrupt HDD cache, progressed boot through all PSARCs into IntroLoaderThread |
| `fix-wws-job-464-and-spurs-task-sync` | 2026-10-21 | Complete | Implemented gow_unblock_wws_464_mgr & gow_unblock_spurs_tasks_3a5; unblocked 0x00464F10 & 0x003A5A94; reached 892 live frames, 998 3D draw groups (0 drops), 2455 surface binds, 634 flips |
| `gcm-immediate-stepover-and-spurs-throttle` | 2026-10-22 | Complete | Removed GCM_PARK_GRACE_MS for ready DLs in cellGcmSys.c; throttled cellSpursCreateTask2 & armed spurs unblock; reached 4,064 frames (4.5x), 0 backlogged DLs, cellVoice init |
| `gcm-pump-and-vblank-telemetry` | 2026-10-24 | Complete | Added timestamped logs and 5s telemetry counters (ppu_gcm_pump/tid, cellGcmTickVBlank, vblank handler, dl_head/tail, ref) |
| `status-dashboard` | 2026-10-24 | Complete | Created tools/gen_status.py, dark/light SVGs, status.md, status_data.json, and embedded dashboard into README |

## Blockers

_None yet — Phase 1 not started._

## Key Facts Discovered

| Fact | Source |
|---|---|
| EBOOT.ELF is 8.2 MB (ELF64 BE PPC64) | `vfs/PS3_GAME/USRDIR/EBOOT.ELF` |
| Entry point: 0x00808360 -> code 0x10410, toc 0x81ef60 | `analysis/EBOOT.loader.json` |
| Exact code-end boundary: 0x0062C4CC (.text ends, .rodata begins) | `tools/find_code_end.py` |
| OPD functions: 7,556 unique descriptors | `analysis/EBOOT.functions.json` |
| Heuristic recovered functions: 19,199 functions total | `analysis/functions.json` |
| Lifted PPU functions: 19,570 functions across 8 chunks (122.25 MB) | `src/gen/ppu_recomp_*.cpp` |
| Firmware imports: 345 across 25 libraries | `imports.json` |
| Unresolved NIDs: 57 missing out of 345 total (57 custom stubs/HLE, 30 PSN stubs, 258 runtime) | `tools/nid_triage.py -> analysis/nid_triage.json` |
| HLE NID table: 1,195 handlers across 93 modules | `src/gen/ppu_hle_nids.cpp` |
| ps3recomp_runtime built with Clang + UCRT + Ninja | `ps3recomp/build/libps3recomp_runtime.a` |
| C: free disk: ~38.6 GB | System check 2026-10-16 |
| Twisted Metal CMakeLists.txt uses 4 runtime .cpp files directly | `twistedmetal/CMakeLists.txt` |
| Twisted Metal links: ws2_32 bcrypt xinput ole32 d3d12 dxgi dxguid d3dcompiler mfplat mfuuid | `twistedmetal/CMakeLists.txt` |
| FIOS deadlock root cause: sys_lwcond_t 32-bit vs 64-bit EA | `twistedmetal/docs/bringup.md` |
| SPURS v2 API forwarding: 4 NIDs in hle_extra.cpp | `twistedmetal/docs/bringup.md` |
| Edge Zlib host DEFLATE: ~200 lines in tm_inflate.cpp | `twistedmetal/docs/first-pixels.md` |
| JOBCRT Ver13 needs kernel block at +0x14 | `twistedmetal/docs/in-match.md` |
| RSX labels (NV406E) were initially all no-ops | `twistedmetal/docs/first-pixels.md` |
| ppu_lifter --hle-stubs is MANDATORY | `ps3recomp/docs/GAME_PORTING_GUIDE.md` |
| gen_hle_nids.py must be regenerated on ps3recomp update | `ps3recomp/docs/GAME_PORTING_GUIDE.md` |
| /STACK:268435456 (256 MB) needed for deep call chains | `twistedmetal/CMakeLists.txt` |
| gow_ascension.exe linked: 70.8 MB standalone with zero unresolved symbols | `build/gow_ascension.exe` |
| Effective NID coverage: 83.5% (258 covered in runtime + 30 offline PSN stubs) | `analysis/nid_triage.json` |
| Initial SPU workload dispatched by game: fp=0x00E729804474449A (size 38,100 bytes) | Runtime execution trace |
| FIOS asset streaming subsystem boots and spawns 7 worker/scheduler threads | Runtime execution trace |

## Session Log

| Date | What Happened |
|---|---|
| 2026-10-16 | Project initialized. Graphify indexed workspace (11,310 nodes, 27,613 edges). CLAUDE.md and AGENTS.md created with 5 project laws. Planning documents generated (.planning/PROJECT.md, REQUIREMENTS.md, ROADMAP.md, STATE.md, config.json). |
| 2026-10-16 | Phase 1 executed & completed: installed portable Clang UCRT, CMake, Ninja, pycryptodome; built ps3recomp_runtime; parsed EBOOT.ELF (345 imports); created find_code_end.py (boundary 0x0062C4CC); created relift.py; lifted 19,570 functions into 8 chunks + NID table (122.25 MB in src/gen/). |
| 2026-10-17 | Phase 2 executed & completed: triaged 345 NIDs with tools/nid_triage.py; created src/compat/psn_stubs.cpp with 30 PSN/voice/commerce stubs; created src/boot_main.cpp; created src/hle_extra.cpp with SPURS v2 forwarding and FIOS lwcond layout fix; created root CMakeLists.txt with -fms-extensions and static linking; built build/gow_ascension.exe (70.8 MB, 0 unresolved symbols); verified native execution through CRT, D3D12 device init, SPURS Taskset2, and FIOS thread creation. Graphify updated (11,444 nodes, 27,790 edges). |
| 2026-10-17 | Phase 3 discussed & planned: technical specifications locked in CONTEXT.md (4 SPU ELFs, build_spu_workloads.py, auto-constructor registration, CMake integration). Three atomic plans generated (03-01-PLAN.md, 03-02-PLAN.md, 03-03-PLAN.md). |
| 2026-10-17 | Phase 3 executed & completed: 4 embedded SPU ELFs extracted (175.8 KB); 0 SDATs confirmed; 2,444 functions identified; all 4 modules lifted to src/gen/spu/ (3.04 MB) with debased includes and auto-constructor; 43,528 instructions decoded and verified; CMakeLists.txt updated; build/gow_ascension.exe built and linked (72.61 MB, 0 unresolved symbols); verified live boot dispatch HIT for boot task fp=0x00E729804474449A. |
| 2026-10-17 | Phase 4 discussed & planned: verified decrypted ISO mounted at virtual drive E: (36.16 GB PS3VOLUME); verified live zero-copy streaming of GAME.PSARC, SHELL.PSARC, etc.; locked diagnostic tools (spbalance, audit_guest_ptrs, compare); updated CLAUDE.md/AGENTS.md/PROJECT.md for Gemini Flash 3.8 High as Planner & Executor; generated 04-01-PLAN.md, 04-02-PLAN.md, 04-03-PLAN.md. |
| 2026-10-17 | Phase 4 executed & completed: 04-01 verified zero-copy VFS streaming from E:; 04-02 completed spbalance/audit_guest_ptrs audits; 04-03 resolved Edge Zlib decompression spinlock at 0x004803B4 via 32-bit completion status clearing & stream cache, booted through cellGameBootCheck, cellAudioPortStart (8 ch), and completed _cellGcmInitBody with 3x 720p display buffers and SPURS workloads. |
| 2026-10-17 | Phase 5 discussed & planned: technical specifications locked in CONTEXT.md (RSX_LIVE_DRAW bridge, -LiveDraw launch profile in run_port.ps1, SPURS-to-RSX label synchronization & watchdog, shader verification via vp_eval.py). Three atomic plans generated (05-01-PLAN.md, 05-02-PLAN.md, 05-03-PLAN.md). |
| 2026-10-17 | Phase 5 executed & completed: 05-01 added -LiveDraw profile and verified D3D12 swapchain and 3x 720p display buffers; 05-02 implemented RSX label tracking (index 0 and 1) and continuous flip presentation (_cellGcmSetFlipCommand -> rsx_live_draw_present); 05-03 verified shader decompilation (vp_eval.py) and achieved Milestone 1 First Pixels without crashes. docs/bringup-status.md created. |
| 2026-10-17 | Milestone 1 (Phases 1–5) fully achieved. Milestone 2 initialized: Phase 6 (Wws_Job PM & SPU Sub-Jobs), Phase 7 (RSX BrB FIFO & Shaders), Phase 8 (libedgegeom 3D & XInput) added to ROADMAP.md and STATE.md for Gemini Flash 3.8 High. |
| 2026-10-17 | Phase 6 executed & completed: extracted raw 12,288-byte SPU Policy Module at 0x0077CA80 (fp=0xC8D774963A96DCF2), wrapped into ELF, lifted into src/gen/spu/wws_job_pm/, registered image 5 in spu_workloads.c, recompiled build/gow_ascension.exe (76.3 MB); verified zero PM NOT LIFTED warnings and confirmed live execution of wid=8 (BrBDblBufWrkld), wid=9/10 (RenJobSet), wid=13 (RenderJobs), and wid=14 (MeshProcJobs) with seamless SPU interpreter handoff. |
| 2026-10-17 | Quick fix executed: implemented real PS3 LV2 sys_lwmutex and sys_lwcond semantics with Win32 event wait queues, FIFO waiter ordering, and owner baton passing in src/compat/ascension_hle.cpp. Reduced main thread CPU spin by 99.8%. |
| 2026-10-17 | Quick fix executed: corrected sys_process_is_stack address ranges (main stack 0x0E000000..0x0FFFFFFF, worker stacks 0xD0000000..0xDFFFFFFF); diagnosed and eliminated 883+ open loop on corrupt cache.idx by removing PS3_HDD1_ROOT; verified full zero-copy streaming of all 21 PSARCs from E: in <2s; spawned threads tid=9..14; confirmed IntroLoaderThread (tid=12) actively running flip and pad loop with 5,281 NV4097 FIFO calls. |
| 2026-10-21 | Quick fix executed: implemented GCM FIFO parked self-jump 16-word scan-ahead & dummy put (0x00A80000) guard in ps3recomp cellGcmSys.c; added hook_func_00375A50 and hook_sys_timer_usleep fence satisfaction in ascension_hle.cpp. In 180s test, dl_head wrapped 32-entry circular buffer twice (68+ DLs), 35 full 3D groups executed with 0 drops (7x increase), 161 guest clears, 34 surface binds, and 3,990 live frames rendered. |
| 2026-10-21 | Quick fix executed: fixed extra calculation target & 0x1F wrap-around at target >= 32 across hook_func_00466B88, hook_func_00466E28, brbDriver, and WWS-MAIN-UNBLOCK; removed CellSpurs* pointer corruption (mgr_ea+4 = 1); synchronized r5+8 and gpr[5] in WWS-MAIN-UNBLOCK. In 180s test, dl_head wrapped circular buffer 6 full times (218+ DLs), 102 fences satisfied, 109 full 3D groups executed with 0 drops (3.1x increase), 381 guest clears, 108 surface binds, and tid=1 advanced past 0x00467B1C into scene update loop at lr=0x00416A3C with all 16 SPURS workloads active. |
| 2026-10-21 | Quick fix executed: guarded cellGcmSys.c against stale ring step-overs when dl_head == dl_tail and suppressed dummy sentinel put=0x00A80000 resync; added gow_unblock_416_subqueues in ascension_hle.cpp. In 180s test, s_fifo_getoff remained safely in active command memory (0x002C8DA4) with 5 dummy put resyncs cleanly suppressed; SUBQ-DEP-UNBLOCK unblocked dependency wait; tid=1 advanced past 0x00416A3C to lr=0x0041664C; 218 DLs executed across 6 circular buffer wraps, 104 fences satisfied, and 85 full 3D groups rendered with 0 drops. |
| 2026-10-21 | Quick fix executed: fixed master vs subqueue register role mismatch in gow_unblock_416_subqueues and hook_sys_timer_usleep; iterated all num_q sub-queues and unblocked their respective WwsJob_Managers; added hook_func_00416600. In 180s test, SUBQ-COUNTER-UNBLOCK fired once at 5 ticks and unblocked func_00416600 completely (spin dropped from 26,700 to 0); tid=1 advanced past lr=0x0041664C into func_00464AC8 (lr=0x00464B4C); 228 DLs executed across 7 circular buffer wraps, ending at head=4, tail=4 (0 backlogged DLs); 2,294 live frames rendered. |
| 2026-10-21 | Quick fix executed: implemented gow_unblock_wws_464_mgr & gow_unblock_spurs_tasks_3a5 in ascension_hle.cpp; satisfied 16-bit lane counters and comp_cnt == target_cnt for Wws_Job manager 0x12326788; synchronized 0x00983168/0x0098317C SPURS task counters for 0x00983100 (func_003A5A94/func_003A5D00). In 180s test, tid=1 advanced completely through frame loops without pausing; reached 892+ live frames, 998 3D draw groups executed (0 drops), 2,455 surface binds, 634 flips (tid=1: 543, tid=12: 91), and verified full tone-mapping bloom hierarchy rendering in real-time. |
| 2026-10-22 | Quick fix executed: bypassed GCM_PARK_GRACE_MS for ready Display Lists (0x00040064u) in ps3recomp/libs/video/cellGcmSys.c; throttled cellSpursCreateTask2 logging and armed gow_unblock_spurs_tasks_3a5 on task creation in ascension_hle.cpp. In 180s test, RSX FIFO drained 1,053 DL entries with 0 backlog (dl_head=0 dl_tail=0, get=put=0x00A80000); live frame rendering surged to 4,064 frames (4.5x increase); 498 total flips executed; cellVoice initialized & started; mapped 12 MB main memory block (0x16300000); tid=1 actively flipping at 60 FPS (lr=0x0046DA70). |\n| 2026-10-24 | Quick task completed: added timestamped log output to tools/run_port.ps1 (build/logs/run_<ts>_{stdout,stderr}.log); added 5s telemetry counters (ppu_gcm_pump calls/sec per tid, cellGcmTickVBlank ticks/sec, VBlank handler invocations/sec, wall-clock seconds, dl_head/dl_tail, ref) and tracked exact caller file:line across all threads. |




