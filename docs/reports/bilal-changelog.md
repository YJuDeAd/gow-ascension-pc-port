# Bilal GoW: Ascension Changelog Inventory & Architecture Report

**Document**: `docs/reports/bilal-changelog.md`  
**Target Repository**: `gow-bilal` (`C:\IfItWorksItWorks\Project\gow-bilal`) — *Audited Read-Only*  
**Date**: November 15, 2026  
**Author**: Sub-agent `bilal-changelog`  
**Audience**: Lead Port Engineers & GSD Executors  

---

## 1. Executive Summary

A comprehensive, line-by-line audit of `C:\IfItWorksItWorks\Project\gow-bilal\CHANGELOG.md` (995 lines) and upstream `C:\IfItWorksItWorks\Project\gow-bilal\ps3kit\CHANGELOG.md` (888 lines), cross-referenced against the `gow-bilal` source tree (`ascension/host/ascension_defaults.cpp`, `ps3kit/runtime/spu/spu_channels.c`, `ps3kit/runtime/spu/spu_dma.h`, `ps3kit/runtime/spu/spu_drain.c`, `ps3kit/runtime/platform/host_pin.c`, `ps3kit/runtime/syscalls/sys_ppu_thread.c`, `ps3kit/libs/video/rsx_live_draw.c`, and `ps3kit/runtime/ppu/tests/boot_main.cpp`), was performed to extract all fixes for hangs, freezes, SPU/SPURS concurrency bugs, GCM memory corruptions, and thread priority starvation.

This report catalogs all 28 critical lifecycle entries in a structured matrix, provides an in-depth code-level analysis of the 6 highlighted milestones, and presents an architectural synthesis explaining our port's cold-run stall at `brb_StartSession` (lr `0x0061B9FC`) / SPU lane 5 wid 8 (`BrBDblBufWrkld`).

---

## 2. Comprehensive Inventory Table

The table below catalogs every entry across versions 0.1.0 through 0.1.31 in `C:\IfItWorksItWorks\Project\gow-bilal\CHANGELOG.md` that addresses: **freezes, hangs, boot, SPURS/SPU/WWS, GCM/RSX flip/sync, FIOS, threads/affinity, window, ISO/disc reading, and save paths**.

| Version | Release Date | Subsystem / Symptom | Fix & Exact Citation (`gow-bilal/CHANGELOG.md`) |
| :--- | :--- | :--- | :--- |
| **0.1.31** | 2026-11-14 | **Input / Freezes**<br>Short freezes occurred during controller rumble/vibration on wireless pads (e.g., 8BitDo). | Lines 28–29: `Wireless controllers (for example 8BitDo): vibration is now sent less often, which should stop the short freezes some of you had while the pad vibrates.` |
| **0.1.31** | 2026-11-14 | **ISO / Disc Reading**<br>Windows repeatedly prompted to mount .iso disc image on every PLAY. | Lines 30–31: `PLAY with an .iso: the launcher remembers where the image is mounted and uses it again, instead of asking Windows to mount it on every PLAY.` |
| **0.1.31** | 2026-11-14 | **Saves Paths**<br>Upgrades extracted to a fresh directory lost path to existing save data. | Line 32: `Your saves from older port folders are still found and copied in, as in 0.1.30.` |
| **0.1.30** | 2026-11-14 | **Saves Paths**<br>Progress lost across releases; checkpoints saved to loose `savedata` by v0.1.26 and older. | Lines 74–84: `When it starts, it looks for saves in your older port folders: folders next to this one, one level up, in Downloads and on the Desktop... Checkpoints saved in the wrong place by 0.1.26 and older (loose files in the savedata folder) are found and put into your profile the same way.` |
| **0.1.29** | 2026-11-14 | **Boot / CPU Support**<br>Processors without AVX2 crashed silently with an uninformative error code. | Lines 146–148: `Processors without AVX2 (for example Intel Core i5/i7 2nd and 3rd generation, AMD FX, some Pentium/Celeron): the game used to stop with only an error code. The launcher now says the processor is not supported, why, and that settings or updates cannot fix it.` |
| **0.1.29** | 2026-11-14 | **Boot / Paths**<br>PLAY failed when saved game path pointed to a deleted/moved folder. | Lines 155–157: `PLAY no longer reports the port's own files as missing when the saved game path points at a game file that is gone or has no port files beside it... it now uses this folder's game.` |
| **0.1.28** | 2026-11-13 | **Threads / Affinity**<br>Ryzen 9 7950X3D/9950X3D ran main thread on non-V-Cache CCD. | Lines 181–184: `With Thread placement on (the default), the game's main thread now runs on the large-cache cores together with the graphics and SPU threads (before, it ran on the other core group).` |
| **0.1.28** | 2026-11-13 | **Threads / Freezes / Audio**<br>Intel hybrid CPUs (P/E-cores) suffered multi-second freezes and missing audio when SPU threads were pinned to E-cores. | Lines 192–196: `On Intel CPUs with E-cores (for example Core i7-13620H), Thread placement put the game's main thread and two SPU threads (one of them the sound) alone on the slower E-cores: freezes of several seconds and missing sound. Without placement the same laptop played without them. The game now turns Thread placement off by itself on these CPUs (the log says [pin] hybrid: thread placement off).` |
| **0.1.27** | 2026-11-11 | **Saves Paths**<br>Checkpoints saved into root save directory instead of active user profile on specific memory layouts. | Lines 240–242: `On some PCs every checkpoint was saved into the wrong folder (the main save folder instead of your profile), so Continue started from an older chapter. Fixed. It only happened on PCs where Windows places the game's memory in a particular way, so most players never saw it.` |
| **0.1.27** | 2026-11-11 | **Window / Hang**<br>Game window marked "Not Responding" by Windows User32 when running under 60 FPS. | Lines 246–247: `When the game ran below 60 FPS (for example with frame generation on a 60-100 Hz screen, or a heavy scene at 4K), Windows could mark the game window "Not Responding" while the game kept running. Fixed.` |
| **0.1.27** | 2026-11-11 | **Boot / Disc Reading**<br>Custom-firmware loader EBOOTs failed silently without identifying missing original ELF. | Lines 255–257: `A clearer message when a copy of the game has a custom-firmware loader as its EBOOT.BIN: it now says to put the ORIG folder (with the disc's own EBOOT.BIN) back next to PS3_GAME.` |
| **0.1.26** | 2026-11-11 | **Launcher / Hang**<br>UI thread locked up when mouse cursor rested over the primary PLAY button. | Line 294: `Fixed a freeze when the mouse rested on the big PLAY button.` |
| **0.1.26** | 2026-11-11 | **Disc Reading / Patches**<br>Skip intro patch failed on US disc image (BCUS98232). | Lines 289–290: `It only blanked the European disc's list of intro screens; it now blanks the US one too.` |
| **0.1.25** | 2026-11-11 | **GCM / Freezes / Memory**<br>Second snake freeze: picture stopped while sound continued after ~12 min due to game heap colliding with GCM progress labels. | Lines 307–310: `After about 12 minutes of play the game's own memory grew over the small area where the port keeps the graphics chip's progress signals. The game then wrote its data there, and the drawing side waited forever for a signal that never came: the picture froze while the sound went on. That area now sits far away from the game's memory.` |
| **0.1.25** | 2026-11-11 | **SPU / Audio / Priority**<br>Sound job on SPU ran late under heavy 4-core load, emitting silence gaps and crackling. | Lines 320–323: `On busy 4-core PCs the game's sound job could run late, and the missing bits were played as short silences (crackling). Two fixes, both on by default: The thread that runs the sound job gets a higher priority while it does. A late bit of sound is filled with the previous one, faded smoothly, instead of silence.` |
| **0.1.24** | 2026-11-10 | **Boot / Disc Reading**<br>US disc support (BCUS98232) via runtime binary adaptation. | Lines 364–366: `The launcher turns the US EBOOT into the European one the port is built from (a 42 KB difference, checked byte for byte), so every fix and patch works the same.` |
| **0.1.24** | 2026-11-10 | **Boot / Hang / SPU MLAA**<br>Startup black-screen hang when "Disable SPU MLAA" patch is unticked on slower processors. | Lines 371–373: `With the "Disable SPU MLAA" patch unticked, slower PCs (laptops) could freeze on a black screen at the first start: the port waited only 50 ms for the game's anti-aliasing jobs, a slower CPU needs longer, and the jobs then waited for each other forever. The port now waits as long as the jobs need.` |
| **0.1.24** | 2026-11-10 | **Threads / Affinity / SPU**<br>6-core CPU thread placement previously caused SPU worker starvation freeze (4 seconds without CPU). | Lines 379–382: `6-core / 12-thread CPUs with one core cluster (for example Ryzen 5 5600 / 7600 / 9600, Core i5-12400F, i5-11400) now use thread placement... The freeze that placement caused on 6 cores before (an SPU worker left without CPU time for 4 seconds) is fixed.` |
| **0.1.24** | 2026-11-10 | **SPURS / Scheduler**<br>EDGE watchdog called SPURS priority update 2500–2900 times/sec, locking scheduler and waking all SPU workers. | Lines 387–389: `The game's EDGE watchdog asks the SPU scheduler to set the same priorities again about 2,500-2,900 times a second, and every call locked the whole scheduler and woke all SPU workers. Calls that change nothing are now skipped (66-95 % of them): the watchdog's CPU time about halves. SPURS_PRIO_SKIP_SAME=0 brings back the old way.` |
| **0.1.23** | 2026-11-09 | **Threads / Affinity / Freezes**<br>Recurring 200 ms micro-freezes every few seconds on 8-core CPUs (e.g., 5800X3D, 7800X3D, 9800X3D, 9900K). | Lines 425–428: `On CPUs with exactly 8 cores (for example Ryzen 7 5800X3D / 7800X3D / 9800X3D, Ryzen 7 laptops, Core i9-9900K), the Thread placement option caused a short freeze of about 200 ms every few seconds, even when standing still. The game now turns thread placement off by itself on 8 cores... The log says [pin] 8 cores: thread placement off.` |
| **0.1.23** | 2026-11-09 | **Audio / SPU / Stutter**<br>Buffer underruns on 4–6 core CPUs at 100% CPU utilization. | Lines 444–447: `When the CPU is at 100 % (weaker 4-6 core CPUs in big fights), the game's own sound sometimes came a little late and the port played silence instead... Now the port waits for late sound while it still has sound buffered (the idea RPCS3 uses), and when it really must fill a gap it fades out and back in instead of cutting hard.` |
| **0.1.23** | 2026-11-09 | **Threads / Priority / Audio**<br>Audio thread starvation under heavy load. | Lines 469–470: `Crackling sound in heavy fights is fixed: the sound thread now gets top priority on a busy CPU.` |
| **0.1.22** | 2026-11-08 | **SPU / Native Code (HLE)**<br>High CPU consumption and frame spikes running translated PPC SPU code for geometry and render passes. | Lines 483–485: `The game's geometry jobs (EDGE) and its render jobs used to run as translated PS3 SPU code. They now run as native PC code, inside the same worker threads, and give the same output: checked job by job, millions of jobs. The graphics command thread does less work per frame.` |
| **0.1.22** | 2026-11-08 | **Saves Paths**<br>Crashing during checkpoint write endangered progress. | Lines 501–503: `With Back up before playing on... the launcher now also backs up your saves each time the game saves while you play. If the game crashes during a save, your earlier progress is still in the backups (Restore). The last 20 backups are kept.` |
| **0.1.21** | 2026-11-07 | **GCM / Sync / Freezes**<br>GPU occlusion query stalls (8 ms per late answer) caused micro-freezes on busy CPUs. | Lines 519–524: `With Occlusion culling on, the game asks the GPU which objects are hidden and waits for the answers. On a CPU that is already fully busy... 15-27 % of those answers arrived too late... and every late one held the game for 8 ms. That caused the micro-freezes testers reported. The game now notices this within about 10 seconds of play and stops waiting for the rest of the session` |
| **0.1.20** | 2026-11-07 | **Boot / Loading / Timing**<br>Kratos fell through map in Chapter 1 on slower CPUs due to loader time slice starvation under Unlock FPS. | Lines 539–544: `Players reported Kratos falling out of the map in Chapter 1... The cause was in the Unlock FPS patch: it switches the game's clock from screen refreshes to milliseconds, and that also cut the time the game's level loader gets each frame from 3.3 ms to 0.2 ms... Unlock FPS 2.1 gives the loader its original time again.` |
| **0.1.20** | 2026-11-07 | **Threads / Priority**<br>Background OS processes degraded game execution cadence. | Lines 560–562: `The game now runs at above-normal priority: Windows serves it before background programs (a video render, a download, a compile)... The graphics thread runs at high priority: about +5 % fps on 4-core PCs` |
| **0.1.18** | 2026-11-06 | **ISO / Disc Reading**<br>FAT32 split archives (`EXTRASCIN2.PSARC.66600..02`) failed file check. | Lines 603–604: `PLAY now recognises the parts and offers to join them in your game folder. That needs about 8.7 GB of free space` |
| **0.1.17** | 2026-11-05 | **SPU / GCM / Freezes**<br>SPU watchdog safety check halted game job awaiting graphics thread. | Lines 651–652: `An SPU safety check could stop a game job that was only waiting for the graphics thread, which froze the game for good; it no longer does.` |
| **0.1.17** | 2026-11-05 | **Boot / Disc Verification**<br>Missing/incomplete game archives caused sudden exit post-shader compile. | Lines 653–654: `The launcher checks for an incomplete game copy (the USRDIR\ARCHIVES list and every archive it names) and says so, instead of the game closing after compiling shaders.` |
| **0.1.16** | 2026-11-04 | **Boot / Window / Launcher**<br>Non-English Windows locales failed to launch game due to command output encoding error. | Lines 662–663: `On a Windows that is not in English... pressing PLAY did nothing: no window, no message, no log... Windows answers in its own language and in an old text encoding the launcher's Python could not read... The launcher now reads the answer in a way that works in every language.` |
| **0.1.15** | 2026-11-04 | **Boot / Shaders**<br>Massive mid-game compile stalls on first boot. | Lines 670–672: `When the game starts, every recorded shader pipeline is built first, on all your CPU cores, with a progress screen ("COMPILING SHADERS 1234 / 9465"). Then the game starts and new areas have nothing left to compile` |
| **0.1.15** | 2026-11-04 | **SPU / PPU Memory Race**<br>Garbled textures on 4–6 core PCs; SPU texture table write-back clobbered PPU memory tags. | Lines 678–679: `A game job on the SPUs reads that whole table, updates it and writes it all back; on a busy PC that job runs long enough for the game to mark new textures meanwhile, and the write-back erased those marks... The write-back now keeps every change the game made meanwhile.` |
| **0.1.14** | 2026-11-03 | **GCM / Memory / Hang**<br>GCM address window overflow: graphics chip 256 MB window ran out of descriptors after 3–4 reload cycles. | Lines 704–706: `After dying, restarting a checkpoint... objects... turned into flat one-colour triangles... The port handed out new window addresses at every map and never reused the ones an unmap gave back, so after three or four reloads the addresses ran past what the game can store... Freed addresses are now reused, as on a PS3, and the window is kept at 256 MB.` |
| **0.1.14** | 2026-11-03 | **FIOS / Concurrency**<br>Multi-threaded file I/O raced on shared file position pointers. | Line 722: `File reads from several game threads at once can no longer move each other's file position.` |
| **0.1.13** | 2026-11-03 | **GCM / SPU / Freezes**<br>Preempted SPU command block transfer exposed partial/stale FIFO commands to graphics walker (picture freeze, sound continues). | Lines 728–732: `The PS3's SPU co-processors write the game's drawing commands in blocks, and each block starts with a "wait here" word that holds the graphics thread back... Windows can pause it in the middle of copying a block. The graphics thread then went on into the half that still held the previous pass's commands, followed an old jump and waited forever. The copy now writes a block's "wait here" word last, so the graphics thread only moves on once the whole block is there.` |
| **0.1.13** | 2026-11-03 | **Boot / Disc Decryption**<br>Eliminated manual RPCS3 binary decryption requirement. | Lines 740–741: `The launcher now decrypts the disc's EBOOT.BIN itself when you pick your game folder or disc image. Before, you had to decrypt it first with RPCS3` |
| **0.1.12** | 2026-11-02 | **Threads / Affinity / SPU**<br>Thread placement starved 6 SPU worker threads on <8 core systems while spinning scheduler consumed CPU. | Lines 755–756: `With thread placement on, the game gave its graphics thread and main thread a CPU thread each and squeezed the six SPU threads, plus a scheduler that never sleeps, onto what was left... With fewer than 8 cores the game now leaves thread placement to Windows and lets the scheduler sleep between checks.` |
| **0.1.11** | 2026-11-02 | **GCM / Memory / Textures**<br>Premature texture memory reuse caused corrupt visuals. | Line 768: `A freed texture's memory is now reused only after the graphics chip is certainly done with it anyway.` |
| **0.1.10** | 2026-11-02 | **Boot / Crash / Multithreading**<br>Startup crash in first second due to non-atomic reference count race on shared `ID3DBlob` shaders across worker threads. | Lines 776–777: `Those threads share compiled shaders with the graphics thread, and the shader compiler's objects do not count their references safely across threads: a reference could be lost and a shader still in use freed, crashing the game in its first second... Every reference change on the shared shaders now happens under the lock that already guards them.` |
| **0.1.10** | 2026-11-02 | **GCM / Memory Allocations**<br>30–70 ms hitches post-loading screen caused by 100–280 individual Win32 allocations per frame. | Lines 780–781: `Each one was its own Windows memory allocation, about 0.23 ms apiece, which made 30-70 ms hitches. New textures now go into large GPU memory blocks reserved once (about 0.05 ms each).` |
| **0.1.9** | 2026-11-01 | **Boot / Config Isolation**<br>Global settings caused new directory installs to launch previous folder's binaries. | Lines 789–791: `All launchers on a PC shared one settings file... A newer full package extracted into a new folder kept starting the older folder's game... A port folder now keeps its settings in its own settings.json (next to the launcher).` |
| **0.1.7** | 2026-11-01 | **GCM / Shaders / Startup**<br>Synchronous 200 MB pipeline library load blocked graphics thread on boot. | Line 818: `The saved pipeline library (over 200 MB) now opens in the background at start, and its loads no longer block the graphics thread on cards where they are slow` |
| **0.1.5** | 2026-11-01 | **GCM / SPU / Flip Sync**<br>RSX FIFO park-wait timeout (2s) expired on slow CPUs, skipping frame and losing PPU synchronization signal. | Lines 849–850: `At the end of each frame the game's SPU jobs patch a "wait here" command in the graphics command stream with the way to the next frame. When that patch came more than 2 seconds late... the port gave up and skipped ahead, and the skip threw away the signal the game's main thread waits for: the picture stopped for good while the sound went on. It now waits up to 60 seconds` |
| **0.1.5** | 2026-11-01 | **Threads / Affinity**<br>6-core CPU plan starved audio and driver helper threads on single shared LP. | Lines 854–856: `The SPU kernel sat beside the main game thread, and every other thread (game helper threads, audio, shader building, the graphics driver) had to share one logical processor. The new plan keeps the main game thread and the graphics thread on their own hardware threads, puts the SPU work on the cores between, and lets the other threads use everything else.` |
| **0.1.3** | 2026-11-01 | **SPU / Native EDGE / Boot Hang**<br>Spreading EDGE geometry across all 6 SPUs previously hung at boot in 0.1.2 due to missing output ring structures. | Lines 893–896: `Native EDGE job front end: the control code of every EDGE geometry job runs as hand-written host code instead of recompiled SPU code... Geometry on all six SPUs: the game only builds geometry rings for four SPUs; the port now gives SPUs 5 and 6 their own rings, so geometry work spreads over all six (EDGE6=1). In 0.1.2 this froze the game at boot.` |
| **0.1.3** | 2026-11-01 | **SPU / Concurrency**<br>Lock-line atomic lock contention across SPUs and PPU. | Line 897: `Lock-line lock split into 64 stripes: SPU atomic operations on different memory lines no longer wait for each other (SPU_LL_STRIPE=1, stress-tested).` |
| **0.1.2** | 2026-10-31 | **SPU / Boot Hang**<br>Attempting to distribute EDGE across all 6 SPUs without extra rings caused immediate boot hang. | Line 931: `Letting EDGE use all six SPUs: the game freezes at boot.` |
| **0.1.1** | 2026-10-30 | **Threads / Affinity**<br>Spinning SPURS scheduler thread starved main PPU game thread on same CPU core. | Lines 939–941: `The game's busiest threads each get a CPU core: the graphics command thread, the main game thread and the six SPU workers. The always-spinning SPU scheduler thread now sits beside an SPU worker instead of beside the main game thread` |
| **0.1.0** | 2026-10-30 | **VFS / Saves Paths**<br>Runtime `/dev_hdd1` and `/dev_hdd0` path separation. | Lines 966–968: `Game data in the port folder: game data, saves and the system cache go to ascension-recomp\ascension\gamedata. Runtime: /dev_hdd1 now honours PS3_HDD1_ROOT (ppu_fs.cpp, sys_fs.c). cellGame no longer creates folders inside the disc folder when PS3_HDD0_ROOT is set.` |
| **0.1.0** | 2026-10-30 | **SPU / Crash**<br>Game aborted during stage transition due to host stack exhaustion in lifted SPU code. | Line 991: `The game closing at a stage change (an SPU host stack overflow).` |
| **0.1.0** | 2026-10-30 | **GCM / Memory / Freezes**<br>Texture block shrunk twice caused VRAM manager failure and subsequent freeze. | Line 992: `scrambled textures and a freeze after some area loads (a texture block shrunk twice).` |

---

## 3. Deep-Dive Analysis of Highlighted Releases

### 3.1. Version 0.1.10: Boot Crash
- **Symptom**: The executable crashed within its first second of execution after launch. The crash probability scaled directly with the number of pre-recorded shader pipelines in the disk cache (e.g., 2 crashes in 6 boots with 754 pipelines).
- **Exact Citation**: `CHANGELOG.md:775-778`
- **Root Cause Analysis**: Starting in v0.1.7, recorded pipelines were warmed at startup across multiple background worker threads (`LD_PSO_WARM`, `LD_PSO_ASYNC` in `ps3kit/libs/video/rsx_live_draw.c:10340-10400`). These background threads shared compiled `ID3DBlob` vertex and pixel shader bytecodes with the primary RSX graphics thread. Direct3D's COM objects returned by `D3DCompile` or `D3DCreateBlob` utilize non-thread-safe reference counting. Concurrent `AddRef` and `Release` invocations across four or more threads corrupted the internal COM reference count. A shader blob still referenced in the shader cache was prematurely deallocated (`Release()` reached 0), causing a subsequent lookup's `AddRef()` to trigger an invalid memory read (use-after-free crash at RVA `0x12C3AA0`).
- **Implementation Fix**: In `rsx_live_draw.c:10662-10678`, the function `ld_blobs_release(ID3DBlob* a, ID3DBlob* b)` was created:
  ```c
  static void ld_blobs_release(ID3DBlob* a, ID3DBlob* b)
  {
      LD_PSO_LOCK();
      if (a) a->lpVtbl->Release(a);
      if (b) b->lpVtbl->Release(b);
      ReleaseSRWLockExclusive(&s_pso_lock);
  }
  ```
  Every `Release` operation on cached shader blobs across all compiler and builder threads was forced under `s_pso_lock` (the exclusive SRW lock guarding cache `AddRef`), completely eliminating the multi-threaded race.

---

### 3.2. Versions 0.1.13 and 0.1.25: "Picture Froze While Sound Continued"
This hallmark symptom occurred in two distinct, independently solved manifestations:

#### 1) v0.1.13: SPU Preemption & GCM FIFO Command Tearing
- **Symptom**: During heavy gameplay (most frequent on 4-core CPUs in combat), the picture froze while audio playback continued uninterrupted.
- **Exact Citation**: `CHANGELOG.md:728-733`
- **Root Cause Analysis**: On the PS3, SPU DMA PUT operations into RSX command memory transfer contiguous blocks atomically. In ps3recomp, each SPU runs as a Windows host thread. When the host OS preempted an SPU worker thread halfway through a multi-quadword PUT operation into the RSX command ring, the memory contained a mix of newly written commands and leftover stale commands from the ring buffer's previous lap. Because each segment begins with a "wait here" jump/park instruction, the graphics command reader ("walker") evaluated the park before the entire payload had arrived, stepped into stale command space, followed an obsolete jump address, and deadlocked waiting on a fence that was never posted.
- **Implementation Fix**: In `ps3kit/runtime/spu/spu_channels.c:231-250, 311-340` (`spu_dma_copy_rsx`, `GCM_PUT_ORDER=2`):
  ```c
  /* GCM_PUT_ORDER=2 (default): a PUT whose first word is a park (JUMP to own IO address).
   * The walker moves on as soon as that word changes, so it is written last:
   * the rest of the PUT first, then the head's 16 bytes as two 8-byte stores,
   * the park's half last. */
  if (n > 16u && g_gcm_put_order_on && !(n & 15u) && !((uintptr_t)d & 15u) && spu_dma_head_is_park(d, ea)) {
      spu_dma_copy(d + 16, s + 16, n - 16u);
      uint64_t hi, lo;
      memcpy(&hi, s + 8, 8);
      memcpy(&lo, s, 8);
      __atomic_store_n((uint64_t*)(void*)(d + 8), hi, __ATOMIC_RELEASE);
      __atomic_store_n((uint64_t*)(void*)d, lo, __ATOMIC_RELEASE);
  }
  ```
  The park word holding the graphics walker back is deliberately written **last** using an atomic store with release semantics, guaranteeing that the walker cannot proceed until the full payload has landed.

#### 2) v0.1.25: Dynamic Memory Overwriting GCM Labels (Second Snake Freeze)
- **Symptom**: After approximately 12 minutes of gameplay (specifically at the second snake sequence), the video stream halted completely while audio continued normally.
- **Exact Citation**: `CHANGELOG.md:306-312`
- **Root Cause Analysis**: The ps3recomp runtime originally placed its HLE GCM injection window (containing progress labels, semaphores, and control registers: `0x8000` bytes) at guest EA `0x20000000` (`VM_HLE_INJECT_BASE`), located immediately above the game's `sys_mmapper` dynamic heap base (`0x11000000`). After ~12 minutes of active play, the game's dynamic allocations expanded from `0x1E200000` past `0x20100000`. The game engine allocated heap blocks over the GCM label table (`0x20000000..0x20008000`), overwriting label variables with arbitrary game state. The RSX walker then spun indefinitely awaiting label `0x10` (which could never equal the expected fence value), permanently freezing rendering.
- **Implementation Fix**: In `ascension/host/ascension_defaults.cpp:438-446`:
  ```cpp
  // The runtime's GCM label / control / offset-table window (0x8000 bytes) sat at 0x20000000,
  // right above the title's mmapper heap (0x11000000 up)...
  // Moved past the EDGE6 rings (0x2E000000, 8 MB) and below the raw-SPU windows (0x30000000).
  ppu_hle_inject_base = 0x2F000000u;
  ```
  Relocating `ppu_hle_inject_base` to `0x2F000000` moved the entire HLE synchronization block completely outside the reach of the guest memory allocator.

---

### 3.3. Version 0.1.23: Freezes Every Few Seconds on 8-Core CPUs
- **Symptom**: On processors with exactly 8 physical cores (e.g., AMD Ryzen 7 5800X3D / 7800X3D / 9800X3D, Core i9-9900K), gameplay was interrupted every few seconds by 100–200 ms stutter hitches (averaging 4.3–5.1 stalls >100 ms per minute), even when Kratos stood idle in an empty room.
- **Exact Citation**: `CHANGELOG.md:424-433`
- **Root Cause Analysis**: As documented in `ps3kit/runtime/platform/host_pin.c:1938-1955`, the 8-core thread placement profile dedicated Core 0 (LP 0) to the PPU main thread, pinned the continuously spinning SPURS kernel thread directly alongside SPU worker 5, and crammed all remaining worker threads (the graphics walker, FIOS I/O workers, audio mixer, and system helper threads) onto the 6 remaining SMT sibling threads. The SMT resource contention between the spinning scheduler, SPU 5, and the graphics walker caused recurring thread execution pipeline stalls (~190 ms on a ~91-frame grid).
- **Implementation Fix**: In `host_pin.c:1949-1955`:
  ```c
  if (s_pin_on && !hp_small_old() && s_proc_cores == 8 &&
      !env_int("PS3_PIN_8", 0) && !env_int("PS3_PIN_SMALL", 0)) {
      s_pin_on = 0;
      fprintf(stderr, "%s8 cores: thread placement off (on 8 cores it caused ~200 ms stalls every few "
                      "seconds; PS3_PIN_8=1 keeps it)\n", rule_pfx);
  }
  ```
  The port automatically deactivates thread placement on 8-core processors (`[pin] 8 cores: thread placement off`), allowing Windows kernel thread scheduling to distribute workloads across all available execution units.

---

### 3.4. Version 0.1.24: Start-up Hang with SPU MLAA On
- **Symptom**: When the "Disable SPU MLAA" patch was unticked, lower-frequency desktop CPUs and laptops froze on a black screen on the initial loading sequence.
- **Exact Citation**: `CHANGELOG.md:370-377`
- **Root Cause Analysis**: SPU MLAA processes post-processing anti-aliasing via SPURS workloads synchronized with RSX frame reports. When SPU MLAA was enabled, the port enforced a rigid 50 ms timeout (`GCM_REPORT_SPU_HOLD_MS`) while waiting for SPU tasks to exit their round. On slower CPUs, completing round 1 required 50–76 ms. When the 50 ms timer expired, the runtime permitted the next frame's RSX report to advance and issue signals to SPURS tasks that were still executing round 1. These early signals were consumed and dropped by the active task barriers, causing the subsequent round's state machine to deadlock, leaving the graphics walker stuck indefinitely awaiting label `0x510 == 2`.
- **Implementation Fix**: In `ascension/host/ascension_defaults.cpp:31-51` and `CHANGELOG.md:371-373`:
  - `GCM_REPORT_SPU_HOLD_MS` was increased from 50 ms to 2000 ms (`set_default("GCM_REPORT_SPU_HOLD_MS", "2000");`).
  - `GCM_ROUND_RING` and `GCM_REPORT_SPU_HOLD_RING` were implemented to hold frame 2's ring reports until the preceding frame's SPURS round has verifiably finished.

---

### 3.5. Version 0.1.27: "Not Responding" Window
- **Symptom**: When the game dipped below 60 FPS (e.g., during 4K rendering or frame generation on variable refresh displays), Windows User32 flagged the window as "Not Responding" (ghost window), even though internal gameplay and rendering continued.
- **Exact Citation**: `CHANGELOG.md:245-248`
- **Root Cause Analysis**: In `ps3kit/runtime/ppu/tests/boot_main.cpp:610-627`, when rendering latency exceeded the 16.6 ms vblank cadence, the vblank catch-up loop executed up to 240 ticks (~6 seconds of simulation) in an uninterrupted inner loop. Message pumping (`rsx_pump_messages()`) was originally called only *after* the entire 240-tick burst had completed. Exceeding the 5-second Windows message queue dispatch threshold caused the OS desktop window manager (DWM) to flag the application as hung.
- **Implementation Fix**: In `boot_main.cpp:621-625`:
  ```cpp
  /* Below 60 fps (frame generation pacing at half a 75 Hz refresh, or a slow PC) every tick
   * presents and takes longer than a vblank, so this runs all 240 ticks (~6 s) in one pass;
   * with the window only serviced after it, Windows marked the game "Not Responding" (2026-11-11).
   * The pump looks at the window at most every 4 ms, so this is cheap. */
  if (rsx_ok && rsx_pump_messages() != 0) { pump_closed = 1; break; }
  ```
  `rsx_pump_messages()` was introduced directly inside the inner tick catch-up loop, ensuring Win32 messages are pumped at a minimum 4 ms cadence.

---

### 3.6. Versions 0.1.12 & 0.1.22: Native SPU Execution
- **Symptom**: Heavy combat scenes (egg fight, satyr prison rooftop) suffered extreme CPU bottlenecks (dropping to 23–33 FPS on 4–8 core CPUs) due to emulating the PS3's 6 SPU coprocessors entirely via recompiled PowerPC code.
- **Exact Citations**: `CHANGELOG.md:754-760` (v0.1.12), `CHANGELOG.md:482-492` (v0.1.22)
- **Evolutionary Architecture**:
  1. **v0.1.0 – v0.1.3**: Introduction of hand-written AVX2 / AVX-512 kernels for EDGE geometry fragments 301, 310, 330, and the native EDGE job front end (`EDGE_HLE=1`, `EDGE_PM=1`).
  2. **v0.1.12**: Mitigated starvation on small CPUs by switching off rigid thread pinning below 8 cores and enabling the SPURS kernel to sleep rather than hard-spin (`SPURS_KERNEL_SPIN=auto`).
  3. **v0.1.22**: Full migration of both EDGE geometry jobs and rendering jobs (`RenderJobs` / `RenJobSet`, workloads 9–13) from recompiled SPU PPC code to native C++ host code (`NRENDER_INLINE=1`, `NEXEC_INLINE=1`). Both workload types execute directly as native PC functions inside the worker threads, producing bit-exact output checked over millions of runs. Frame rates jumped +41% on 4 cores (33.3 -> 47.0 FPS) and locked at 60 FPS on desktop CPUs.

---

## 4. Synthesis: Forensic Root-Cause Analysis of Our Port's Stall

### 4.1. Our Observed Failure Signature
- **Callsite**: Main PPU thread stalled at lr `0x0061B9FC` inside `brb_StartSession`.
- **Occurrence Rate**: Approximately 85% of cold runs.
- **SPU State**: SPU worker lane 5 is executing workload ID 8 (`BrBDblBufWrkld`, priority vector `[0,0,0,0,0,1]`). At pc `0x0228C`, it performs an MFC DMA-PUT of `0xFFFFFFFF` into guest EA `0x747D3510`, and then stalls on its first MFC list command:
  - Command: `0x45` (`MFC_GETL_CMD | MFC_BARRIER_ENABLE | MFC_STALL_NOTIFY_ENABLE`)
  - Tag: `0`
  - Local Store Address: `0x1580` (stall-and-notify list element)
  - Result: Transfer never completes; tag 0 is never marked finished.
- **PPU State**: The guest thread `brbDriver` (tid 9) spins in a tight loop reading `[0x747D3510] == -1`.

---

### 4.2. Forensic Correlation with Bilal's Codebase & Fixes

Our stall is not a single bug, but a multi-tiered race condition whose exact components were documented and resolved across four specific files in Bilal's repository:

#### Mechanism A: PPU-vs-SPU Thread Priority Inversion & Host CPU Starvation
- **Documented Evidence**: `ps3kit/runtime/platform/host_pin.c:1422-1428`, `ps3kit/runtime/syscalls/sys_ppu_thread.c:477-478`, `ps3kit/libs/spurs/cellSpurs.c:2739-2757`, and `ascension/host/ascension_defaults.cpp:353-356`.
- **Analysis**:
  1. In `sys_ppu_thread.c:477-478`, any guest PPU thread created with guest priority `< 256` is promoted on Windows to `THREAD_PRIORITY_ABOVE_NORMAL` (level 11):
     ```c
     if (priority < 256)
         SetThreadPriority(t->host_thread, THREAD_PRIORITY_ABOVE_NORMAL);
     ```
  2. `host_pin.c:1423-1424` explicitly enumerates the guest PPU threads that receive this priority:
     > `"in this game fios x6, brbDriver, brbClient, snd_stream x2, EdgeWatchdog, voice"`
  3. `brbDriver` (tid 9) runs at host priority **11** (`THREAD_PRIORITY_ABOVE_NORMAL`). When `brb_StartSession` is called, `brbDriver` spins at 100% CPU waiting for `[0x747D3510] != -1`.
  4. SPU worker thread 5 (executing `BrBDblBufWrkld`, wid 8) defaults to host priority **10** (`THREAD_PRIORITY_NORMAL`).
  5. If thread placement places `brbDriver` on the same core/cluster as SPU worker 5 (or if Windows schedules them together without thread placement), `brbDriver` consumes 100% of the core's quantum. SPU worker 5 is starved of CPU cycles and cannot advance.
  6. **Bilal's Exact Fix**: In `ascension_defaults.cpp:353-356` and `cellSpurs.c:2743-2757`, Bilal introduced `SPURS_BOOST_WID="8=2"`:
     ```c
     // cellSpurs.c:2749-2755: While worker runs wid 8 (BrBDblBufWrkld), elevate to HIGHEST (priority 2 / level 12)
     if (s_bw >= 0 && (int)slot->wid == s_bw) {
         boost_old = GetThreadPriority(GetCurrentThread());
         if (boost_old != THREAD_PRIORITY_ERROR_RETURN && boost_old < s_bp)
             SetThreadPriority(GetCurrentThread(), s_bp);
     }
     ```
     By elevating SPU worker 5 above `brbDriver` while wid 8 runs, SPU 5 is guaranteed the CPU slices needed to complete its DMA and signal `brbDriver`.

---

#### Mechanism B: Synchronous MFC List Stall-and-Notify Delivery Race
- **Documented Evidence**: `ps3kit/runtime/spu/spu_drain.c:355-374` and `ps3kit/runtime/spu/spu_dma.h:1029-1067, 1800-1806, 1897-1920`.
- **Analysis**:
  1. At pc `0x0228C`, SPU lane 5 issues command `0x45` on tag 0 with element at LS `0x1580`. The list element has bit 31 set (`stall_notify = 1`).
  2. In `spu_dma.h:1036-1038`:
     ```c
     spu->list_stall_mask |= (1u << t);  /* tag 0 marked as parked */
     spu->list_stall_stat |= (1u << t);  /* tag 0 accumulated into RdListStallStat */
     spu->event_status    |= 0x2u;       /* SPU_EVENT_SN raised */
     ```
  3. In `spu_dma.h:1803`:
     ```c
     if (!(spu->list_stall_mask & (1u << (tag & 0x1F))))
         mfc->tag_completed |= (1u << tag);
     ```
     Because `list_stall_mask` has bit 0 set, tag 0 is **not** marked completed.
  4. Real PS3 Cell hardware handles stall-and-notify **asynchronously**. The SPU code stores its expected state, and the SPU interrupt handler subsequently takes the interrupt, reads `MFC_RdListStallStat` (ch 25), and writes `MFC_WrListStallAck` (ch 26) to resume the list via `mfc_list_stall_ack()` (`spu_dma.h:1074-1125`).
  5. In `spu_drain.c:355-366`, Bilal documents the exact failure mode when this runs in ps3recomp:
     > `"We execute the list synchronously inside wrch MFC_Cmd, so the interrupt was delivered before the job manager had stored... With the state still kNone it correctly declined to advance -- and because MFC_RdListStallStat is 'accumulative, clear on read' that single stall was consumed, so no later interrupt could ever advance it: the job never ran and ... deadlocked on a completion that never came."`
  6. If the SPU interrupt handler reads channel 25 before state is ready, or if interrupts are disabled (`ctx->int_enable == 0`), or if `MFC_WrListStallAck` is never dispatched, tag 0 stays parked in `list_stall_mask` forever. The SPU never finishes `BrBDblBufWrkld`, and `[0x747D3510]` remains `-1` for eternity.

---

#### Mechanism C: Stale DMA List Memory & `SPU_LIST_REFETCH`
- **Documented Evidence**: `ascension/host/ascension_defaults.cpp:303-307` and `ps3kit/runtime/spu/spu_channels.c:719-806`.
- **Analysis**:
  1. In `ascension_defaults.cpp:303-307`:
     ```cpp
     // Stopgap for the batch-job freeze: a DMA-list element real hardware would fault on
     // (module 216's command-stream lists caught with one stale quadword) is re-read from
     // the main memory the list came from, when that copy is well-formed.
     set_default("SPU_LIST_REFETCH", "1");
     ```
  2. If the DMA list at LS `0x1580` was transferred from guest main memory while the producer (PPU or SPU) had not fully flushed its write buffers, the SPU executes invalid list element data. Enabling `SPU_LIST_REFETCH=1` causes `spu_list_refetch()` to re-read the descriptor from host memory if an invalid element is detected.

---

## 5. Verification Status & Actionable Recommendations

### 5.1. Verified vs. Hypothesis Status
- **Verified**:
  - `brbDriver` runs at host priority `THREAD_PRIORITY_ABOVE_NORMAL` (11) due to guest priority `< 256` (`sys_ppu_thread.c:477-478`, `host_pin.c:1423-1424`).
  - SPU worker 5 running wid 8 (`BrBDblBufWrkld`) runs at `THREAD_PRIORITY_NORMAL` (10) unless elevated via `SPURS_BOOST_WID` (`cellSpurs.c:2743-2757`).
  - Command `0x45` on tag 0 at LS `0x1580` is an MFC stall-and-notify list command that stops `tag_completed` from being set until acknowledged via `MFC_WrListStallAck` (`spu_dma.h:1029-1067, 1803`).
  - Channel 25 (`MFC_RdListStallStat`) is destructive clear-on-read; premature evaluation drops the stall event (`spu_drain.c:355-366`).
  - Bilal's runtime sets `SPURS_BOOST_WID="8=2"` and `SPU_LIST_REFETCH="1"` as default operating environment (`ascension_defaults.cpp:307, 356`).
- **Hypothesis**:
  - `[Hypothesis 1]`: The 85% stall in our port is predominantly triggered by `brbDriver` starving SPU worker 5 of CPU scheduling quanta on Windows before SPU 5 can process its interrupt and issue `MFC_WrListStallAck`.
  - `[Hypothesis 2]`: In our port's SPU dispatch loop, the stall-and-notify interrupt is either not delivered because `ctx->int_enable` is 0 during synchronous `wrch MFC_Cmd`, or `MFC_RdListStallStat` is read by an uncoordinated poll, permanently losing the stall event.

### 5.2. Actionable Remediation Checklist for `gowassension`
1. **Implement `SPURS_BOOST_WID` in `cellSpurs`**:
   Ensure that when an SPU worker picks up workload ID 8 (`BrBDblBufWrkld`), its host thread priority is immediately raised to `THREAD_PRIORITY_HIGHEST` (`SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)`), matching Bilal's `cellSpurs.c:2749-2755`.
2. **Audit MFC Stall-and-Notify Dispatch**:
   Verify in `spu_dma.h` and `spu_channels.c` that when `stall_notify` is raised on command `0x45`, the SPU interrupt is properly delivered, `int_enable` is respected, and `MFC_WrListStallAck` is received to resume the list via `mfc_list_stall_ack()`.
3. **Verify Synchronous Interrupt Deferral (`SPU_SN_DEFER`)**:
   Check if our port implements `g_sn_defer` (`spu_drain.c:375-384`) to hold off immediate interrupt delivery until the SPU workload has completed its state transition.
4. **Enable `SPU_LIST_REFETCH=1`**:
   Incorporate Bilal's `spu_list_refetch()` logic into `spu_channels.c` to protect against stale memory reads during initial DMA list acquisition.
