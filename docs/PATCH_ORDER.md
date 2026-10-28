# PATCH_ORDER.md — PS3Recomp Patch Application Sequence

This document defines the deterministic order, source, classification, and purpose of all patches applied on top of upstream ps3recomp base commit `a6d76c8` (commit `a6d76c895f77eb30b90a806c830544e61821b77f`).

Patches are stored sequentially in `patches/series/NNNN-*.patch` and applied via `tools/apply_patches.ps1`.

### Classification Key
- **Upstream (MIT)**: Generic PS3 emulation fixes, RSX graphics enhancements, SPU/PPU runtime optimizations, and POSIX/Win32 threading primitives suitable for merging into upstream `sp00nznet/ps3recomp`.
- **Ascension (Port)**: Specific job-module workarounds, telemetry hooks, or engine-tailored heuristics required specifically for God of War: Ascension.

---

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
| 0050 | `0050-fios-cache-modes.patch` | Ascension (Port) | FIOS / FS | Host FS caching modes and `FsLastOpInfo` tracking last file op/path per thread. |
| 0051 | `0051-task5-mfc-diag.patch` | Ascension (Port) | SPU / MFC | SPU MFC diagnostics, DMA command history ring, and host Edge Zlib decompression hooks. |
| 0052 | `0052-spu-cycle-state-preserve-spurs-accounting.patch` | Upstream (MIT) | SPU / SPURS | SPU instruction cycle accounting preservation across context switches; SPURS workload metrics. |
| 0053 | `0053-fix-spurs-pm-poll-and-dma.patch` | Upstream (MIT) | SPURS / DMA | Fix SPURS policy module polling logic, interrupt status delivery, and DMA completion synchronization. |
| 0054 | `0054-spurs-lane-preemption-idle-wake.patch` | Upstream (MIT) | SPURS | SPURS idle lane preemption signaling (`spurs_preempt_idle_lanes`) triggering interrupt 0x40 on lower-priority lanes. |
| 0055 | `0055-spurs-lane-preemption-idle-wake-and-revert-fake-fios.patch` | Ascension (Port) | SPURS / FIOS | Clean SPU idle preemption wakeups without busy spin, and revert of synthetic FIOS timer completion writes. |

