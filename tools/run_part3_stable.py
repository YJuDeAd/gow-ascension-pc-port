import os
import sys
import time
import subprocess
import hashlib
import re
import shutil

EXE = "build/gow_ascension.exe"
TARGET_ELF = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"
RUNTIME_LIB = "D:/gow-bilal-asia/ascension/build/win/ps3recomp_sdk/libps3recomp_runtime.a"
LOCK_FILE = "build/run.lock"

def get_sha256(path):
    if not os.path.exists(path):
        return "MISSING"
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest().upper()

def is_pid_alive(pid):
    try:
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}"], capture_output=True, text=True).stdout
        return str(pid) in out
    except Exception:
        return False

def acquire_lock():
    while True:
        try:
            fd = os.open(LOCK_FILE, os.O_CREAT | os.O_EXCL | os.O_RDWR)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            break
        except FileExistsError:
            try:
                with open(LOCK_FILE, "r") as f:
                    content = f.read().strip()
                if not content or not content.isdigit() or not is_pid_alive(int(content)):
                    if os.path.exists(LOCK_FILE):
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
    subprocess.run(["taskkill", "/F", "/IM", "gow_ascension.exe"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "AscensionRecomp.exe"], capture_output=True)

def clean_fios_cache():
    cache_paths = [
        "vfs/dev_hdd0/game/BCAS25016/cache.idx",
        "D:/gow-bilal-asia/ascension/build/win/gamedata/dev_hdd0/game/BCAS25016/cache.idx"
    ]
    for p in cache_paths:
        if os.path.exists(p):
            try:
                os.remove(p)
            except Exception:
                pass

def get_disk_info():
    t_c, u_c, f_c = shutil.disk_usage("C:")
    t_d, u_d, f_d = shutil.disk_usage("D:")
    return {
        "C_free_gb": round(f_c / (1024**3), 2),
        "C_used_gb": round(u_c / (1024**3), 2),
        "D_free_gb": round(f_d / (1024**3), 2),
        "D_used_gb": round(u_d / (1024**3), 2)
    }

def parse_log(stdout_log):
    audit_dict = {}
    boot_dict = {}
    telem_lines = []
    first_draw_time = "N/A"
    crashed = False
    last_lr = "UNKNOWN"
    last_state = "UNKNOWN"
    cur_sec = None

    if os.path.exists(stdout_log):
        with open(stdout_log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                # [TASK4-THREAD-AUDIT @ <sec>s] ... Thread tid=1 [main]: state=RUNNING entry=0x00010000 lr=<lr>
                m_audit_sec = re.search(r'\[TASK4-THREAD-AUDIT @ (\d+)s\]', line)
                if m_audit_sec:
                    cur_sec = int(m_audit_sec.group(1))
                m_audit_main = re.search(r'Thread tid=1 \[main\]: state=(\w+).*?lr=(0x[0-9A-Fa-f]+)', line)
                if m_audit_main:
                    state = m_audit_main.group(1)
                    lr = m_audit_main.group(2).upper()
                    last_lr = lr
                    last_state = state
                    if cur_sec is not None:
                        audit_dict[cur_sec] = (state, lr)

                m_boot = re.search(r'\[BOOT-DIAG @ (\d+)s\] lr=(0x[0-9A-Fa-f]+)', line)
                if m_boot:
                    boot_dict[int(m_boot.group(1))] = m_boot.group(2).upper()

                if "[TELEMETRY 5s]" in line:
                    telem_lines.append(line.strip())
                    m_p = re.search(r'wall=(\d+\.\d+)s .* presents=([0-9\.]+)/s .* total_presents=(\d+)', line)
                    if m_p:
                        wall = float(m_p.group(1))
                        tot_p = int(m_p.group(3))
                        if first_draw_time == "N/A" and tot_p > 0:
                            first_draw_time = f"<{wall:.0f}s"

                if "FATAL" in line or "EXCEPTION" in line or "Segmentation fault" in line:
                    crashed = True

    # Main lr at 10, 20, 30, 40, 50, 60, 70, 80, 90
    lr_history = []
    for s in [10, 20, 30, 40, 50, 60, 70, 80, 90]:
        if s in audit_dict:
            lr_history.append(audit_dict[s][1])
        elif s in boot_dict:
            lr_history.append(boot_dict[s])
        else:
            lr_history.append("N/A")

    # Metric: main leaves 0x00467B1C and stays alive through duration
    # If at 60s/90s main lr is NOT 0x00467B1C and not crashed
    # In particular, check if it reached 0x00467B1C earlier and left it, or never stalled at 0x00467B1C at the end
    stalled_at_467B1C = any(lr.lower() == "0x00467b1c" for lr in lr_history[-3:])
    passed = (not stalled_at_467B1C) and (not crashed) and (last_lr != "UNKNOWN")

    # FPS from telemetry
    fps_history = {}
    total_presents = 0
    for tl in telem_lines:
        m = re.search(r'wall=(\d+)\.0s .* presents=([0-9\.]+)/s .* total_presents=(\d+)', tl)
        if m:
            w = int(m.group(1))
            fps = float(m.group(2))
            tot = int(m.group(3))
            fps_history[w] = fps
            total_presents = tot

    return {
        "passed": passed,
        "crashed": crashed,
        "last_lr": last_lr,
        "last_state": last_state,
        "lr_history": lr_history,
        "first_draw_time": first_draw_time,
        "fps_30": fps_history.get(30, 0.0),
        "fps_60": fps_history.get(60, 0.0),
        "fps_90": fps_history.get(90, 0.0),
        "total_presents": total_presents,
        "telem_lines": telem_lines
    }

def run_single(run_idx, total_runs, duration, log_dir, runtime_sha, exe_sha, prefix="Part3_Stable"):
    stdout_log = os.path.join(log_dir, f"{prefix}_run{run_idx:02d}.log")
    
    # Check if a completed log already exists
    if os.path.exists(stdout_log):
        with open(stdout_log, "r", encoding="utf-8", errors="replace") as f_check:
            content = f_check.read()
            if f"wall={duration}.0s" in content or (duration >= 60 and f"wall={duration-5}.0s" in content) or (duration == 90 and "wall=85.0s" in content):
                parsed = parse_log(stdout_log)
                lr_str = " -> ".join([x for x in parsed["lr_history"] if x != "N/A"])
                if not lr_str: lr_str = parsed["last_lr"]
                print(f"[{prefix} | Run {run_idx:02d}/{total_runs:02d} (CACHED)] elapsed={duration:.1f}s | passed={parsed['passed']} | last_lr={parsed['last_lr']} | FPS[30/60/90]={parsed['fps_30']:.1f}/{parsed['fps_60']:.1f}/{parsed['fps_90']:.1f} | tot_pres={parsed['total_presents']}")
                print(f"   LR history: {lr_str}")
                sys.stdout.flush()
                return {
                    "run": run_idx, "elapsed": float(duration), "passed": parsed["passed"],
                    "crashed": parsed["crashed"], "last_lr": parsed["last_lr"], "last_state": parsed["last_state"],
                    "first_draw_time": parsed["first_draw_time"], "fps_30": parsed["fps_30"],
                    "fps_60": parsed["fps_60"], "fps_90": parsed["fps_90"], "total_presents": parsed["total_presents"],
                    "lr_history": parsed["lr_history"], "exe_sha": exe_sha, "runtime_sha": runtime_sha,
                    "log": stdout_log, "telem_lines": parsed["telem_lines"]
                }

    acquire_lock()
    try:
        kill_procs()
        clean_fios_cache()

        env = os.environ.copy()
        env["PS3_PATCHES"] = "Disable MLAA"
        env["GOWA_NO_MLAA"] = "1"
        env["PS3_VFS_ROOT"] = "E:\\"
        env["PS3_MODS"] = "C:/Users/ROBIN/AppData/Roaming/ascension-launcher/generated;D:/gow-bilal-asia/ascension/mods-builtin/skip-intro"
        env["PS3_SKIP_MOVIES"] = "1"
        env["LD_PSO_PRECOMPILE"] = "1"
        env["OCCL"] = "2"
        env["PS3_HOST_PIN"] = "1"

        start_time = time.time()
        with open(stdout_log, "w", encoding="utf-8") as f_out:
            proc = subprocess.Popen([EXE, TARGET_ELF], cwd=".", stdout=f_out, stderr=subprocess.STDOUT, env=env)
            try:
                proc.wait(timeout=duration)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        elapsed = time.time() - start_time

        parsed = parse_log(stdout_log)
        lr_str = " -> ".join([x for x in parsed["lr_history"] if x != "N/A"])
        if not lr_str:
            lr_str = parsed["last_lr"]

        print(f"[{prefix} | Run {run_idx:02d}/{total_runs:02d}] elapsed={elapsed:.1f}s | passed={parsed['passed']} | last_lr={parsed['last_lr']} | FPS[30/60/90]={parsed['fps_30']:.1f}/{parsed['fps_60']:.1f}/{parsed['fps_90']:.1f} | tot_pres={parsed['total_presents']}")
        print(f"   LR history: {lr_str}")
        sys.stdout.flush()

        return {
            "run": run_idx,
            "elapsed": elapsed,
            "passed": parsed["passed"],
            "crashed": parsed["crashed"],
            "last_lr": parsed["last_lr"],
            "last_state": parsed["last_state"],
            "first_draw_time": parsed["first_draw_time"],
            "fps_30": parsed["fps_30"],
            "fps_60": parsed["fps_60"],
            "fps_90": parsed["fps_90"],
            "total_presents": parsed["total_presents"],
            "lr_history": parsed["lr_history"],
            "exe_sha": exe_sha,
            "runtime_sha": runtime_sha,
            "log": stdout_log,
            "telem_lines": parsed["telem_lines"]
        }
    finally:
        release_lock()

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=20)
    parser.add_argument("--duration", type=int, default=90)
    parser.add_argument("--log-dir", default="scratch/part3_stable_runs")
    parser.add_argument("--prefix", default="Part3_Stable")
    args = parser.parse_args()

    os.makedirs(args.log_dir, exist_ok=True)

    runtime_sha = get_sha256(RUNTIME_LIB)
    exe_sha = get_sha256(EXE)
    disk_before = get_disk_info()

    print("================================================================================")
    print(f"PART 3 BENCHMARK: {args.prefix} ({args.runs} cold x {args.duration}s)")
    print(f"Executable:  {EXE} ({exe_sha})")
    print(f"Runtime Lib: {RUNTIME_LIB} ({runtime_sha})")
    print(f"Disk C: free={disk_before['C_free_gb']} GB, used={disk_before['C_used_gb']} GB")
    print("================================================================================")
    sys.stdout.flush()

    results = []
    pass_count = 0
    for r in range(1, args.runs + 1):
        res = run_single(r, args.runs, args.duration, args.log_dir, runtime_sha, exe_sha, args.prefix)
        results.append(res)
        if res["passed"]:
            pass_count += 1

    print("\n================================================================================")
    print(f"RESULTS SUMMARY: {pass_count}/{args.runs} PASSED")
    print("================================================================================")
    print(f"| Run | Elapsed | Pass | Last LR | First Draw | FPS 30s | FPS 60s | FPS 90s | Presents | LR Progression (10s..90s) |")
    print(f"|---|---|---|---|---|---|---|---|---|---|")
    for res in results:
        lrs_str = " -> ".join([x for x in res["lr_history"] if x != "N/A"][-3:])
        print(f"| {res['run']:02d} | {res['elapsed']:.1f}s | {'PASS' if res['passed'] else 'FAIL'} | `{res['last_lr']}` | {res['first_draw_time']} | {res['fps_30']:.1f} | {res['fps_60']:.1f} | {res['fps_90']:.1f} | {res['total_presents']} | {lrs_str} |")

if __name__ == "__main__":
    main()

