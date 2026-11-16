import os, sys, time, subprocess, json

RUN_LOCK = "build/run.lock"
EXE = "build/gow_ascension.exe"
TARGET_ELF = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"
LOG_DIR = "scratch/part2"

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

acquire_lock()

try:
    results = []
    env = os.environ.copy()
    env["PS3_VFS_ROOT"] = "E:"
    env["SPU_ATOM_EA"] = "70A9E900"
    env["SPU_ATOM_EA_MAX"] = "0"
    env["SPU_ATOM_FULL"] = "2"
    env["SPU_COMMIT_EA"] = "70A9E900"
    env["SPU_PUTLLC_WHY"] = "1"
    env["SPURS_PM_CLAIMLOG"] = "1"

    for run_idx in range(1, 11):
        stdout_path = os.path.join(LOG_DIR, f"part2_run{run_idx:02d}_stdout.log")
        stderr_path = os.path.join(LOG_DIR, f"part2_run{run_idx:02d}_stderr.log")
        print(f"\n==========================================")
        print(f"[PART 2] Starting Run {run_idx:02d}/10 at {time.strftime('%H:%M:%S')}")
        print(f"==========================================")

        with open(stdout_path, "wb") as f_out, open(stderr_path, "wb") as f_err:
            proc = subprocess.Popen([EXE, TARGET_ELF], stdout=f_out, stderr=f_err, env=env)
            t0 = time.time()
            try:
                proc.wait(timeout=62)
            except subprocess.TimeoutExpired:
                print(f"[RUN {run_idx:02d}] 62s elapsed, terminating gracefully...")
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()

        dur = time.time() - t0
        print(f"[RUN {run_idx:02d}] Finished in {dur:.1f}s")

        # Parse run outcome from stderr
        exit_method = "UNKNOWN"
        val_3510 = "N/A"
        main_lr = "N/A"
        w5_claimed = 0
        w5_completed = 0
        w5_last_counter = 0
        w5_pm_entries = 0
        target_r5 = 0
        max_lane = 0

        with open(stderr_path, "r", errors="ignore") as f:
            lines = f.readlines()
            for l in lines:
                if "EXIT_METHOD:" in l: exit_method = l.split(":")[-1].strip()
                if "VAL_3510:" in l: val_3510 = l.split(":")[-1].strip()
                if "MAIN_LR:" in l: main_lr = l.split(":")[-1].strip()
                if "WID5_JOBS_CLAIMED:" in l:
                    try: w5_claimed = int(l.split(":")[-1].strip())
                    except: pass
                if "WID5_JOBS_COMPLETED:" in l:
                    try: w5_completed = int(l.split(":")[-1].strip())
                    except: pass
                if "WID5_LAST_COUNTER:" in l:
                    try: w5_last_counter = int(l.split(":")[-1].strip())
                    except: pass
                if "WID5_PM_ENTRIES:" in l:
                    try: w5_pm_entries = int(l.split(":")[-1].strip())
                    except: pass
                if "target_r5=" in l:
                    # e.g. lim_cnt=0, tbl_cnt=275 => target_r5=275 | max_lane=274
                    try:
                        part = l.split("target_r5=")[1]
                        target_r5 = int(part.split()[0].replace("|",""))
                        max_lane = int(l.split("max_lane=")[1].split()[0])
                    except: pass

        row = {
            "run": run_idx,
            "exit_method": exit_method,
            "main_lr": main_lr,
            "w5_claimed": w5_claimed,
            "w5_completed": w5_completed,
            "w5_last_counter": w5_last_counter,
            "w5_pm_entries": w5_pm_entries,
            "target_r5": target_r5,
            "max_lane": max_lane,
            "stderr": stderr_path
        }
        results.append(row)
        print(f"[RUN {run_idx:02d} RESULT] exit={exit_method} main_lr={main_lr} w5_claimed={w5_claimed} w5_completed={w5_completed} last_cnt={w5_last_counter} target={target_r5} max_lane={max_lane}")

    with open(os.path.join(LOG_DIR, "part2_summary.json"), "w") as f:
        json.dump(results, f, indent=2)

finally:
    release_lock()
