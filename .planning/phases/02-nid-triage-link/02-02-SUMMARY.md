# Plan 02-02 Summary: Single-Player HLE Stubs & Entrypoint Harness

**Phase:** `02-nid-triage-link`  
**Plan:** `02`  
**Execution Date:** 2026-10-17  
**Status:** Completed & Verified  

---

## 1. Accomplishments

1. **Single-Player HLE Extensions Implemented:**
   - Created `src/compat/ascension_hle.cpp` with implementations for:
     - SPURS v2 API forwarding (`cellSpursInitializeWithAttribute2`, `cellSpursCreateTaskset2`, `cellSpursCreateTask2`, `cellSpursJoinTask2`, `cellSpursTryJoinTask2`, etc.) to SPURS v1 runtime.
     - FIOS concurrency fix: `sys_lwcond_create` 32-bit EA layout fix into both struct words.
     - `sys_ppu_thread_once`: mutex-protected one-time engine initializer execution.
     - Display info: `cellVideoOutGetDeviceInfo` advertising 720p / 1080p 60Hz RGB modes.
     - Offset file I/O: `cellFsReadWithOffset` and `cellFsWriteWithOffset`.
     - System modules: `cellGamePatchCheck`, `cellSysmoduleSetMemcontainer`, and PRX dynamic loaders.
     - Registration functions: `ascension_hle_register()` and compatibility alias `gow_hle_register_extra()`.
   - Standalone compilation verified: `analysis/ascension_hle.o` compiled cleanly with Clang in C++20 mode.

2. **Native Entrypoint Harness Implemented:**
   - Created `src/main.cpp` modeled on `twistedmetal/src/boot_main.cpp`:
     - Registered Windows SEH crash filter (`gow_crash_filter`) with HLE NID breadcrumb and callstack RVA reporting.
     - Reserved 4 GB guest virtual address space with VEH demand page committer.
     - Configured VFS root (`ppu_vfs_root`).
     - Initialized PPU lifted function tables (`ppu_recomp_register()`), HLE dispatch (`ppu_hle_init()`), PSN stubs (`gow_psn_stubs_register()`), single-player HLE (`ascension_hle_register()`), CRT (`ppu_sysprx_register()`), VFS (`ppu_fs_register()`), and LV2 syscalls (`lv2_init_syscalls()`).
     - Launched D3D12 RSX FIFO ticker background thread and dispatched guest entry OPD via `ppu_run()`.
   - Standalone compilation verified: `analysis/main.o` compiled cleanly with Clang in C++20 mode.

---

## 2. Verification Command Results

- `ascension_hle.cpp` compilation check:
  ```
  VERIFY_ASCENSION_HLE_OK: ascension_hle.cpp compiled successfully
  ```
- `main.cpp` compilation check:
  ```
  VERIFY_MAIN_OK: main.cpp compiled successfully
  ```
