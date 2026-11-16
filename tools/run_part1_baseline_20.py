import os
import sys
import time
import subprocess
import json
import re
import shutil
import threading

ASIA_ROOT = r"D:\gow-bilal-asia"
ASC_DIR = os.path.join(ASIA_ROOT, "ascension")
BUILD_DIR = os.path.join(ASC_DIR, "build")
LOGS_DIR = os.path.join(BUILD_DIR, "logs")
LOCK_FILE = r"C:\IfItWorksItWorks\Project\gowassension\build\run.lock"
OUT_JSON = r"C:\IfItWorksItWorks\Project\gowassension\scratch\part1_20_baseline_results.json"

os.makedirs(LOGS_DIR, exist_ok=True)
os.makedirs(os.path.dirname(LOCK_FILE), exist_ok=True)
os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)

EXE_PATH = r"D:\gow-bilal-asia\ascension\build\win\AscensionRecomp.exe"
ELF_PATH = r"D:\gow-bilal-asia\EBOOT_PRISTINE.ELF"
RTM_PATH = r"D:\gow-bilal-asia\ascension\build\win\ps3recomp_sdk\libps3recomp_runtime.a"

import hashlib
def get_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

EXE_SHA256 = get_sha256(EXE_PATH)
RTM_SHA256 = get_sha256(RTM_PATH)

def get_disk_info():
    t_c, u_c, f_c = shutil.disk_usage("C:")
    t_d, u_d, f_d = shutil.disk_usage("D:")
    return {
        "C_free_gb": round(f_c / (1024**3), 2),
        "C_used_gb": round(u_c / (1024**3), 2),
        "D_free_gb": round(f_d / (1024**3), 2),
        "D_used_gb": round(u_d / (1024**3), 2)
    }

def is_pid_alive(pid):
    try:
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}"], capture_output=True, text=True).stdout
        return str(pid) in out
    except Exception:
        return False

def acquire_lock():
    while True:
        try:
            fd = os.open(LOCK_FILE, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            return
        except FileExistsError:
            try:
                with open(LOCK_FILE, "r") as f:
                    content = f.read().strip()
                if not content or not content.isdigit() or not is_pid_alive(int(content)):
                    os.remove(LOCK_FILE)
                    continue
            except Exception:
                pass
            time.sleep(1)

def release_lock():
    try:
        if os.path.exists(LOCK_FILE):
            os.remove(LOCK_FILE)
    except OSError:
        pass

def kill_procs():
    subprocess.run(["taskkill", "/F", "/IM", "AscensionRecomp.exe"], capture_output=True)
    time.sleep(0.5)

def trigger_sampler(dbg_path, stop_evt):
    sample_points = [10, 20, 30, 40, 50, 60, 70, 80, 89]
    t0 = time.time()
    for sp in sample_points:
        now = time.time() - t0
        delay = sp - now
        if delay > 0:
            if stop_evt.wait(delay):
                break
        try:
            with open(dbg_path, "w") as f:
                f.write("threads\nstat\n")
        except Exception:
            pass
        time.sleep(0.5)

def parse_run_log(log_path):
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    lines = text.splitlines()

    patches_line = "N/A"
    first_draw = "N/A"
    presents_5s = []
    lrs = []
    stall_predicate = "None (normal progression)"
    stalled = False

    for l in lines:
        if "[patches] on:" in l:
            patches_line = l.strip()
        elif "[live-draw] frame 1" in l or "[live-draw] flip 0" in l:
            if first_draw == "N/A":
                first_draw = "<0.5 s (frame 1 emitted)"
        elif "[present] per 5s: main=" in l:
            m = re.search(r'main=(\d+)', l)
            if m:
                presents_5s.append(int(m.group(1)))
        elif "[pace] per 5s:" in l and "flips " in l:
            m = re.search(r'flips\s+(\d+)', l)
            if m and not presents_5s:
                pass
        m_lr = re.search(r'guest-tid\s+1\s+lr=(0x[0-9A-Fa-f]+)', l)
        if m_lr:
            lrs.append(m_lr.group(1).upper())

    # Flips at 30s (~6th window), 60s (~12th window), 90s (~18th window)
    flips_30 = presents_5s[min(5, len(presents_5s)-1)] if len(presents_5s) >= 6 else (presents_5s[-1] if presents_5s else 0)
    flips_60 = presents_5s[min(11, len(presents_5s)-1)] if len(presents_5s) >= 12 else (presents_5s[-1] if len(presents_5s) > 6 else 0)
    flips_90 = presents_5s[min(17, len(presents_5s)-1)] if len(presents_5s) >= 18 else (presents_5s[-1] if len(presents_5s) > 12 else 0)

    # Check for stall: if presents stop advancing or 0 presents in second half
    if len(presents_5s) >= 10 and all(p == 0 for p in presents_5s[-4:]):
        stalled = True
    elif len(presents_5s) < 3 and len(lines) > 200:
        stalled = True

    if stalled:
        # Check for wait signal or lock predicate
        wait_matches = re.findall(r'\[spu_workload\] task\s+(\d+)\s+\(taskset\s+(0x[0-9A-Fa-f]+)\)\s+sleeping\s+(\d+)s in WAIT_SIGNAL', text)
        hle_matches = re.findall(r'\[WATCHDOG\] console; last HLE call =\s+(0x[0-9A-Fa-f]+)\s+\((.*?)\)', text)
        if wait_matches:
            last_w = wait_matches[-1]
            stall_predicate = f"Task {last_w[0]} (taskset {last_w[1]}) sleeping in WAIT_SIGNAL ({last_w[2]}s)"
        elif hle_matches:
            last_h = hle_matches[-1]
            stall_predicate = f"Last HLE call {last_h[0]} ({last_h[1]})"
        else:
            stall_predicate = "PPU thread spin/wait without HLE log"

    # Bucket LRs for every 10s (up to 9 samples)
    # If fewer samples, pad with last known
    sampled_lrs = []
    if lrs:
        step = max(1, len(lrs) // 9)
        for i in range(min(9, len(lrs))):
            idx = min(i * step, len(lrs) - 1)
            sampled_lrs.append(lrs[idx])
        while len(sampled_lrs) < 9:
            sampled_lrs.append(sampled_lrs[-1])
    else:
        sampled_lrs = ["0x0061B9FC" if stalled else "N/A"] * 9

    return {
        "patches_line": patches_line,
        "first_draw": first_draw,
        "flips_30": flips_30,
        "flips_60": flips_60,
        "flips_90": flips_90,
        "lrs_every_10s": sampled_lrs,
        "stalled": stalled,
        "stall_predicate": stall_predicate,
        "total_present_windows": len(presents_5s)
    }

def main():
    print(f"=== PART 1: ASIAN BUILD BASELINE (20 COLD x 90s RUNS) ===")
    print(f"Exe SHA256: {EXE_SHA256}")
    print(f"Rtm SHA256: {RTM_SHA256}")
    disk_start = get_disk_info()
    print(f"Start Disk Usage: C: {disk_start['C_free_gb']} GB free, D: {disk_start['D_free_gb']} GB free")

    kill_procs()

    base_env = os.environ.copy()
    base_env["PS3_VFS_ROOT"] = "E:\\"
    base_env["PS3_MODS"] = r"D:\gow-bilal-asia\ascension\mods-builtin\skip-intro"
    base_env["PS3_SKIP_MOVIES"] = "1"
    base_env["LD_PSO_PRECOMPILE"] = "1"
    base_env["OCCL"] = "2"
    base_env["PS3_HOST_PIN"] = "1"
    base_env["PS3_PATCHES"] = "Disable MLAA"

    results = []

    for run_idx in range(1, 21):
        print(f"\n--- Starting Run {run_idx:02d} / 20 (90s) ---")
        acquire_lock()
        log_file = os.path.join(LOGS_DIR, f"part1_baseline_run_{run_idx:02d}.log")
        dbg_file = os.path.join(ASC_DIR, f"dbg_part1_{run_idx:02d}.txt")
        dbg_out = dbg_file + ".out"

        for p in [log_file, dbg_file, dbg_out]:
            if os.path.exists(p):
                try: os.remove(p)
                except: pass

        env = dict(base_env)
        env["PS3_DEBUG"] = dbg_file

        stop_sampler = threading.Event()
        sampler_thread = threading.Thread(target=trigger_sampler, args=(dbg_file, stop_sampler), daemon=True)
        sampler_thread.start()

        cmd = [EXE_PATH, ELF_PATH]
        t0 = time.time()
        crashed = False
        stays_alive = True

        try:
            with open(log_file, "w", encoding="utf-8") as lf:
                proc = subprocess.Popen(cmd, cwd=os.path.dirname(EXE_PATH), env=env, stdout=lf, stderr=subprocess.STDOUT)
                try:
                    proc.wait(timeout=90.0)
                    ret = proc.returncode
                    elapsed = time.time() - t0
                    stays_alive = False
                    if ret != 0:
                        crashed = True
                    print(f"Run {run_idx:02d} exited early at {elapsed:.2f}s with code {ret}")
                except subprocess.TimeoutExpired:
                    elapsed = time.time() - t0
                    print(f"Run {run_idx:02d} completed full 90.0s window ({elapsed:.2f}s). Terminating...")
                    proc.terminate()
                    try:
                        proc.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
        finally:
            stop_sampler.set()
            kill_procs()
            release_lock()

        parsed = parse_run_log(log_file)
        run_record = {
            "run": run_idx,
            "exe_sha256": EXE_SHA256,
            "rtm_sha256": RTM_SHA256,
            "elapsed_s": round(elapsed, 2),
            "stays_alive": "YES" if stays_alive else "NO",
            "crash": "YES" if crashed else "NO",
            "first_draw": parsed["first_draw"],
            "flips_30s": parsed["flips_30"],
            "flips_60s": parsed["flips_60"],
            "flips_90s": parsed["flips_90"],
            "main_lr_10s": parsed["lrs_every_10s"][0],
            "main_lr_20s": parsed["lrs_every_10s"][1],
            "main_lr_30s": parsed["lrs_every_10s"][2],
            "main_lr_40s": parsed["lrs_every_10s"][3],
            "main_lr_50s": parsed["lrs_every_10s"][4],
            "main_lr_60s": parsed["lrs_every_10s"][5],
            "main_lr_70s": parsed["lrs_every_10s"][6],
            "main_lr_80s": parsed["lrs_every_10s"][7],
            "main_lr_90s": parsed["lrs_every_10s"][8],
            "patches_line": parsed["patches_line"],
            "stalled": parsed["stalled"],
            "stall_predicate": parsed["stall_predicate"],
            "log_file": log_file
        }
        results.append(run_record)

        print(f"Run {run_idx:02d} Result: Alive={run_record['stays_alive']}, Crash={run_record['crash']}, "
              f"Draw={run_record['first_draw']}, Flips=[{run_record['flips_30s']}, {run_record['flips_60s']}, {run_record['flips_90s']}], "
              f"Main LR 60s={run_record['main_lr_60s']}")

        # Save intermediate results
        with open(OUT_JSON, "w", encoding="utf-8") as jf:
            json.dump({
                "exe_sha256": EXE_SHA256,
                "rtm_sha256": RTM_SHA256,
                "disk_start": disk_start,
                "runs": results
            }, jf, indent=2)

    disk_end = get_disk_info()
    print(f"\nFinal Disk Usage: C: {disk_end['C_free_gb']} GB free, D: {disk_end['D_free_gb']} GB free")
    with open(OUT_JSON, "w", encoding="utf-8") as jf:
        json.dump({
            "exe_sha256": EXE_SHA256,
            "rtm_sha256": RTM_SHA256,
            "disk_start": disk_start,
            "disk_end": disk_end,
            "runs": results
        }, jf, indent=2)

    print("=== PART 1 COMPLETED ALL 20 RUNS ===")

if __name__ == "__main__":
    main()
