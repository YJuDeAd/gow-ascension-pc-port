# Quick Task Plan: Diagnose and Resolve Main usleep Loop & BrB Driver/Client Blockers

## Problem Statement
After unblocking FIOS and mounting all archives, `tid=1` (`main`) sits in a `sys_timer_usleep(10000)` polling loop, while `tid=12` (`IntroLoaderThread`) flips and clears awaiting progress from `main` or `brbDriver`/`brbClient` (`tid=13`/`14`). Zero `0x1808`/`0x1814` draw calls are emitted.

## Execution Steps

### 1. Disassemble and Analyze `main`'s `usleep(10000)` Loop
- Inspect `tid=1`'s LR (`0x0061B9FC`) from the watchdog log.
- Disassemble the guest function around `0x0061B9FC` using `show_func.py` (with `--asm` on `EBOOT.ELF` and `--dir src/gen`).
- Identify the exact guest memory address, struct offset, or status flag that `main` is polling, and what thread/subsystem is responsible for writing it.

### 2. Inspect Blocked Threads (`tid=9`, `tid=13`, `tid=14`)
- Check `tid=9` (`fs::RefreshGameData`): LR and last HLE calls.
- Check `tid=13` (`brbDriver`): blocked in `sys_timer_usleep(1000)` at `0x006245B8`. Disassemble and identify what it is waiting for.
- Check `tid=14` (`brbClient`): blocked in `event_queue(id=3)` at `0x0062308C`. Disassemble and find which queue it awaits and who posts to it.

### 3. Analyze SPURS Wws_Job Telemetry
- Check dispatch and start counts for `wid=6` and `wid=8..14`.
- Trace Wws_Job Policy Module (`0x0077CA80`, `fp=0xC8D774963A96DCF2`) execution count and channel waits.

### 4. Implement Targeted Fix & Verify
- Implement the required HLE / synchronization fix in `src/compat/ascension_hle.cpp`.
- Rebuild `build/gow_ascension.exe`.
- Run `.\tools\run_port.ps1 -LiveDraw -TimeoutSeconds 40`.
- Verify before/after telemetry: `main` advancing past `usleep`, `brbDriver`/`brbClient` progressing, and RSX draw packet activity.
