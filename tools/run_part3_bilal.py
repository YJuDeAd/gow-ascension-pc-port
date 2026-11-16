import os, sys, time, subprocess, json, threading, re

RUN_LOCK = "build/run.lock"
EXE = r"D:\gow-bilal-build\ascension\build\win\AscensionRecomp.exe"
TARGET_ELF = r"D:\gow-bilal-build\work\pristine\EBOOT.ELF"
LOG_DIR = "scratch/part3"

os.makedirs(LOG_DIR, exist_ok=True)

def acquire_lock():
    while True:
        try:
            fd = os.open(RUN_LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            print(f"[LOCK] Acquired {RUN_LOCK}")
            return
        except FileExistsError:
            print(f"[LOCK] Waiting for {RUN_LOCK}...")
            time.sleep(1)

def release_lock():
    try:
        os.remove(RUN_LOCK)
        print(f"[LOCK] Released {RUN_LOCK}")
    except OSError:
        pass

def trigger_debug_sampler(dbg_path, stop_event):
    sample_points = [10, 20, 30, 40, 50, 60]
    t0 = time.time()
    for sp in sample_points:
        now = time.time() - t0
        delay = sp - now
        if delay > 0:
            if stop_event.wait(delay):
                break
        try:
            with open(dbg_path, "w") as f:
                f.write(f"threads\n")
        except Exception:
            pass
        time.sleep(0.5)

acquire_lock()

try:
    results = []
    base_env = os.environ.copy()
    base_env["PS3_VFS_ROOT"] = "E:"
    base_env["AUDIO_MUTE"] = "1"

    print("\n" + "="*80)
    print("PART 3: DECISIVE EXPERIMENT — GOW-BILAL ON CLEAN EBOOT (10 COLD x 60s)")
    print("="*80)

    for run_idx in range(1, 11):
        stdout_path = os.path.join(LOG_DIR, f"bilal_run{run_idx:02d}_stdout.log")
        stderr_path = os.path.join(LOG_DIR, f"bilal_run{run_idx:02d}_stderr.log")
        dbg_path = os.path.abspath(os.path.join(LOG_DIR, f"bilal_run{run_idx:02d}_dbg.txt"))
        dbg_out = dbg_path + ".out"

        if os.path.exists(dbg_path): os.remove(dbg_path)
        if os.path.exists(dbg_out): os.remove(dbg_out)

        env = base_env.copy()
        env["PS3_DEBUG"] = dbg_path

        print(f"\n[PART 3] Starting Run {run_idx:02d}/10 at {time.strftime('%H:%M:%S')}...")

        stop_event = threading.Event()
        sampler_thread = threading.Thread(target=trigger_debug_sampler, args=(dbg_path, stop_event))

        with open(stdout_path, "wb") as f_out, open(stderr_path, "wb") as f_err:
            proc = subprocess.Popen([EXE, TARGET_ELF], stdout=f_out, stderr=f_err, env=env)
            t0 = time.time()
            sampler_thread.start()

            try:
                proc.wait(timeout=61)
            except subprocess.TimeoutExpired:
                print(f"[RUN {run_idx:02d}] 61s elapsed, terminating gracefully...")
                stop_event.set()
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            else:
                stop_event.set()

        sampler_thread.join()
        dur = time.time() - t0

        # Parse stderr for watchdog samples
        # [WATCHDOG]   guest-tid 1   lr=0x0033A394  r3=0x000003E8 sp=0x0FEFF850
        lr_samples = []
        presents = 0
        flips = 0
        crashed = False
        saw_0061b9fc = False
        saw_00467b1c = False

        if os.path.exists(stderr_path):
            with open(stderr_path, "r", errors="ignore") as f:
                for line in f:
                    if "EXCEPTION" in line or "CRASH" in line or "SIGSEGV" in line or "fatal error" in line:
                        crashed = True
                    if "guest-tid 1 " in line and "lr=0x" in line:
                        m = re.search(r"lr=(0x[0-9A-Fa-f]+)", line)
                        if m:
                            lr_samples.append(m.group(1).upper())
                    if "0x0061B9FC" in line:
                        saw_0061b9fc = True
                    if "0x00467B1C" in line:
                        saw_00467b1c = True
                    if "guest_flips=" in line:
                        m = re.search(r"guest_flips=(\d+)", line)
                        if m:
                            flips = max(flips, int(m.group(1)))
                    if "presents=" in line or "present=" in line:
                        m = re.search(r"present=(\d+)", line)
                        if m:
                            presents = max(presents, int(m.group(1)))

        # Also check dbg_out if needed
        # Fallback if lr_samples has entries
        lrs_10_to_60 = {}
        for idx_s, sec in enumerate([10, 20, 30, 40, 50, 60]):
            if idx_s < len(lr_samples):
                lrs_10_to_60[f"{sec}s"] = lr_samples[idx_s]
            else:
                lrs_10_to_60[f"{sec}s"] = lr_samples[-1] if lr_samples else "N/A"

        # Determine whether main left brb_StartSession (0x0061B9FC) and 0x00467B1C
        # If LR is past 0x0061B9FC (e.g. 0x0033A394 in frame loop), it left both!
        left_brb = any(int(lr, 16) != 0x0061B9FC for lr in lr_samples) if lr_samples else False
        left_w5 = any(int(lr, 16) != 0x00467B1C for lr in lr_samples) if lr_samples else False

        res = {
            "run": run_idx,
            "duration": round(dur, 2),
            "lr_10s": lrs_10_to_60.get("10s", "N/A"),
            "lr_20s": lrs_10_to_60.get("20s", "N/A"),
            "lr_30s": lrs_10_to_60.get("30s", "N/A"),
            "lr_40s": lrs_10_to_60.get("40s", "N/A"),
            "lr_50s": lrs_10_to_60.get("50s", "N/A"),
            "lr_60s": lrs_10_to_60.get("60s", "N/A"),
            "left_brb_0061B9FC": left_brb,
            "left_w5_00467B1C": left_w5,
            "presents": presents,
            "flips": flips,
            "crashed": crashed,
            "stderr_log": stderr_path,
            "stdout_log": stdout_path
        }
        results.append(res)
        print(f"[RUN {run_idx:02d}] Done: LRs=[10s:{res['lr_10s']}, 20s:{res['lr_20s']}, 30s:{res['lr_30s']}, 40s:{res['lr_40s']}, 50s:{res['lr_50s']}, 60s:{res['lr_60s']}] | Left_BRB={left_brb} | Left_W5={left_w5} | Flips={flips} | Crashed={crashed}")

    summary_file = os.path.join(LOG_DIR, "part3_summary.json")
    with open(summary_file, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\n[PART 3] Saved summary to {summary_file}")

finally:
    release_lock()
