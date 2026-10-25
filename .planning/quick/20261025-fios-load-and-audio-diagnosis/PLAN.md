---
status: in-progress
created: 2026-10-25
slug: 20261025-fios-load-and-audio-diagnosis
description: Diagnose FIOS asset load stalling, audio signal chain, scene surfaces, and git hygiene
---

# Plan: FIOS Load and Audio Diagnosis

## Task 1: Is the Load Stuck?
- [ ] Parse `run_20261025_062151_stderr.log` for every distinct (state, rem, tot) triple with first/last line number and wall time.
- [ ] Add 5s diagnostic logging for the stalled FIOS request (rem, state, issuer tid, last fs read/lseek, FIOS workers tid 2-8 state) and GOW_SYNC_STATS=1.
- [ ] Re-run 150 s.
- [ ] Disassemble/inspect EBOOT callers writing `rem` (using `show_func.py`), determine what decreases `rem` (cellFsRead, SPU Edge zlib, semaphore, event queue) and report exact missing step.

## Task 2: Audio Chain Diagnosis
- [ ] Disassemble/inspect `func_00607980` and callers up the call chain.
- [ ] Analyze condition guarding call to `func_00615A98` (address, value, writer).
- [ ] Determine if that condition depends on the load in Task 1.

## Task 3: Scene Surfaces
- [ ] Check if `state` ever leaves 3 during the 150 s run.
- [ ] Report per-frame surfaces written by draws/clears when state leaves 3, or explicitly state that it never leaves 3.

## Task 4: Hygiene & Root Branch `render-port`
- [ ] Verify `.gitignore` covers `src/gen/`, `analysis/`, `ppu_switches.json`, `job_fragments.json`, `jobmods`, `spu pic` output.
- [ ] Create root branch `render-port` containing our own code, patches, `mods/skip-intro`, `THIRD_PARTY_NOTICES.md`, `README`.
- [ ] Verify submodule pointer and check `git clone --recursive` compatibility.
