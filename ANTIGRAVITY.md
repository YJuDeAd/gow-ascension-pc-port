# ANTIGRAVITY.md — Engineering Guardrails & Architecture Spec
**Project:** God of War: Ascension — PC Static Recompilation
**Primary Target:** Asian Release (`BCAS-25016`, v1.00)
**Reference Implementation:** `arnaldolima1/gow-ascension-pc` (European Release `BCES-01741`)

---

## 0. NON-NEGOTIABLE AGENT LAWS (READ BEFORE EVERY EDIT)

1. **NO STATE-SPOOFING OR PC TELEPORTATION ("THE WID-8 RULE"):**
   - You are **STRICTLY FORBIDDEN** from bypassing PPU/SPU livelocks or synchronization stalls by overwriting guest completion addresses in `upstream_compat.cpp` (e.g., forcing `*pslot40 = 0` at `0x747D3510`) or forcibly mutating an SPU lane's Program Counter / GPRs (e.g., `c5->pc = 0x1BBC; c5->gpr[3]._u32[0] = 0;`) to fake job completion.
   - Every stall must be fixed at its architectural root cause (e.g., fixing `wws_job_pm` consumer pointer advancement at offset `0xB0` when processing `0x3001xxxx` RSX fence barriers, fixing LL/SC reservation semantics, or fixing missing RSX/SPURS wake events).

2. **VERIFY BUILD-GRAPH INCLUSION BEFORE EDITING:**
   - Before modifying any `.c` or `.cpp` file, verify that it is actively compiled and linked into `build/gow_ascension.exe` by inspecting `CMakeLists.txt` and `build/build.ninja`.
   - Never edit unreferenced files (such as `ascension_hle.cpp` or `hle_extra.cpp`) unless your task explicitly includes wiring them into `CMakeLists.txt` and verifying NID table routing.

3. **LOG & TELEMETRY INTEGRITY:**
   - Never conflate `[live-draw] frame %u` command-buffer packet passes with actual presented scanout frames.
   - True presentation counts (`presents`) must be parsed exclusively from `[TELEMETRY 5s] ... presents=... [buf0=X, buf1=Y]` (`buf0 + buf1`) and `[STAGE] ... presents=...`.
   - Never copy aggregate run maxima across historical timestamp buckets (`gp_10`, `gp_20`, `gp_30`, `gp_45`).

4. **DISK & BUILD GUARDRAILS:**
   - Verify free disk space on `C:` is at least `4.0 GB` before and after every build or trace capture.
   - Both `ninja -C ps3recomp/build ps3recomp_runtime` and `ninja -C build gow_ascension` must compile cleanly with exit code `0`.

---

## 1. REGIONAL COMPATIBILITY & VERSION MANAGEMENT (`BCAS-25016`)

Our primary target is **God of War: Ascension – Asian Release (`BCAS-25016`)**. While `arnaldolima1/gow-ascension-pc` (`BCES-01741`) is our architectural reference, **you must never blindly copy raw PPU function addresses, TOC pointers, SPU registration tables, or memory patches from `BCES-01741`.**

* **Version Manifest (`version_manifest.json`):**
  - Maintain and validate at boot: decrypted `EBOOT.BIN` SHA-256 hash, title ID (`BCAS-25016`), ELF segment boundaries, TOC base (`r2`), and verified SPU module fingerprints.
* **Strict Separation of Concerns:**
  - **Shared Runtime (`ps3recomp/runtime/`, `ps3recomp/libs/`):** Architecture-agnostic LV2 synchronization, SPURS kernel/policy (`spurs_policy.c`), WWS Job PM mechanics, `cellVdec`, `cellGcm`, `CellFs`/`FIOS`, and renderer backends.
  - **Region-Specific Target (`src/compat/`, `src/targets/bcas25016/`):** PPU lifted code generated strictly from the `BCAS-25016` executable, symbolic guest memory presets (e.g., mapping `brbDriver`, `brb_StartSession`, and descriptor pools by verified `BCAS-25016` symbols), and region-audited compatibility overrides.

---

## 2. DUAL-RENDERER ARCHITECTURE (D3D12 + RPCS3 RSX)

Do not replace the existing D3D12 `rsx_live_draw` implementation. Instead, maintain a dual-renderer architecture with a runtime-selectable backend (`--renderer=d3d12` vs. `--renderer=rpcs3-rsx`):

* **RPCS3 RSX Integration via `ps3kit`:**
  - Follow the reference `ps3kit` separate `gpu/` module pattern (`RSX_VULKAN=1` built via `build-gpu.ps1` and linked via `ps3rsx_link()`).
* **Memory Mapping & Synchronization Parity:**
  - Both backends must share a unified guest virtual address mapping (`0xC0000000` RSX local memory and `cellGcmMapMainMemory` IO ranges).
  - When IO address space is unmapped (`cellGcmUnmapIoAddress`), immediately hand the range back so vertex streams never read stale command buffer memory.
  - Preserve SPU DMA writes into GPU-protected surfaces and ensure real GPU report writes (`getReport` / RSX fence labels like `0x30014580`) wake `RsxSync` and pace SPU post-processing rounds.
* **Licensing & Build Configuration:**
  - Gate RPCS3 RSX compilation behind `-DGOW_ENABLE_RPCS3_RSX=ON/OFF`. Keep the core repository and D3D12 builds MIT-clean; builds linking RPCS3's RSX code are governed by **GPL-2.0**.

---

## 3. SPU JOB COVERAGE & STREAMED WWS MODULES

Never measure SPU progress solely by the count of statically lifted SPU ELF images. Santa Monica Studio's engine streams Worldwide Studios (WWS) SPURS job modules dynamically via DMA.

* **Streamed Module Discovery & Provenance:**
  - Use `SPU_DUMP_MISS` and MFC DMA `GET` hooks to capture unrecognized SPU code fragments at runtime.
  - Record full provenance for every module: `SHA256(code_bytes)`, byte size, Local Store entry PC, PPU source EA, and workload ID (`wid 0..31`).
* **PIC Fragment Lifting:**
  - Support Position-Independent Code (PIC) lifting for streamed WWS job fragments (all 33 core + post-loading WWS modules, including post-process sync `module 214`).
* **Execution Telemetry & Interpreter Fallback:**
  - Log per-workload `recompiled_calls`, `interpreter_fallbacks`, and `pm_entries` every 5 seconds.
  - Ensure the SPU interpreter fallback honors `SN int_enable` and `BinkSPU selectWorkload (0x9D4)` to prevent SPURS deadlocks.

---

## 4. ISOLATED SPU REPLAY & MULTI-SPURS SCHEDULER TRACING

When diagnosing SPU job bugs (like the `wid 8` `BrBDblBufWrkld` barrier livelock) or geometry corruption, use a two-level capture architecture:

1. **Level A — Isolated SPU Job Replay (`spu_replay_cli`):**
   - Capture pre-execution `256 KB` Local Store, all 128-bit GPRs (`r0..r127`), MFC DMA queue/channel state, and main-memory DMA input buffers.
   - Replay single jobs standalone in milliseconds and diff outbound DMA writes + post-execution Local Store against the reference interpreter.
   - Store failing captures as permanent regression fixtures under `tests/spu_replays/`.
2. **Level B — Multi-SPURS Scheduler & Atomic Event Trace:**
   - Maintain a lock-free ring buffer recording `SPURS_WKL_READY`, `LANE_ASSIGN`, `LL_SC` (`getllar`/`putllc` on offsets `0xB0`/`0xC0`), `TASKSET2_START`, `EVENT_PORT_SEND`, and `RSX_REPORT_WRITE`.
   - Use this trace to debug multi-actor races (e.g., `brbDriver` vs. `wid 8`, or `SPURS Taskset2` duplicate-start/lost-task wakeups).

---

## 5. REPRODUCIBLE CHECKPOINTS & WAIT-STATE DIAGNOSTICS

Track progression strictly against 5 **Reproducible Checkpoints** (only label a checkpoint *Deterministic* once execution and scheduling order are proven repeatable across runs):

* **`CP1_BOOT_ARCHIVE`:** `sys_lwcond_t` layout validation, >2 GB PSARC archive reading, host Edge Zlib decompression, and `SPURS2` 32-workload init (`tempArea`, `flags1`).
* **`CP2_INTRO_SEQUENCE`:** Legal screen, SCE / Santa Monica Studio logos, and online notice presented cleanly.
* **`CP3_OPENING_CINEMATIC`:** `cellVdec` (FFmpeg backend) holding Access Units properly, `BinkSPU` audio/video pacing at ~29.98 FPS, and clean transition past the 28s fade-out stall.
* **`CP4_MAIN_MENU`:** X16 font atlas, sRGB gamma, all 33 WWS job modules active, and `wid 8` (`BrBDblBufWrkld`) executing real barrier/job lists without `upstream_compat.cpp` spoofing.
* **`CP5_FIRST_LEVEL`:** Playable first level and boss fight reached with valid physics collision pairs (`[0x747D3510] >= 0` written organically by `wws_job_pm` epilogue at `0x1E90`).

**Stall Watchdog Requirement:** Whenever `presents` does not advance for 2.0s, dump a structured **Wait-For Graph** pairing every blocked/spinning PPU thread (`tid`, `lr`, syscall or polled EA + value) with every SPU lane (`lane`, `wid`, `pc`, last DMA/LL-SC address).

---

## 6. HLE & SYSTEM COMPATIBILITY AUDIT

Maintain an automated startup and runtime audit (`logs/nid_audit.json`) covering all imported NIDs in `BCAS-25016`:
* Classify each NID as `CUSTOM_HLE`, `RUNTIME_HLE`, `OFFLINE_STUB`, or `UNRESOLVED`.
* Track runtime invocation counts and log any unexpected non-`CELL_OK` (`!= 0`) return codes.
* **Priority Subsystems:** LV2 threading/synchronization (`lwmutex`, `lwcond`, `event_queue`), `SPURS` & `WWS`, `CellGcm`, `FIOS` & `CellFs`, `CellVdec`, `CellAudio`, and `sys_memory`.
* Verify at boot that all custom compatibility handlers are actively registered in the live dispatch table.

---

## 7. PERFORMANCE OPTIMIZATION POLICY

Prioritize **correctness and checkpoint progression (`CP1` -> `CP5`)** before broad optimization.

* **Pre-Gameplay Allowed Optimizations (Progression-Critical Only):**
  - Lifting dynamically streamed WWS PIC modules to prevent interpreter starvation.
  - Enabling **SSE4.1** vector codegen for lifted SPU workloads to maintain video/sync pacing.
* **Post-Gameplay Optimizations (Gated Behind `CP5_FIRST_LEVEL`):**
  - **SPU:** AVX2 builds, native C replacements for hot post-process/geometry workloads (verified byte-for-byte via `spu_replay_cli`), DMA/LS fast paths, and dedicated physical core pinning for SPU worker threads.
  - **PPU:** Register-local lifting (validated against heap-corruption regressions), function-level profiling, and Core 0 pinning.
  - **Graphics:** D3D12 vs. RPCS3 RSX benchmark comparisons, pipeline/shader caching, and surface-transfer overhead reduction.
  - **Validation:** Always maintain a portable baseline CPU build alongside AVX2/register-local builds and benchmark every change against a verified checkpoint baseline.