import os
import sys
import time
import subprocess
import hashlib
import re
import shutil

EXE = "build/gow_ascension.exe"
TARGET_ELF = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"
RUNTIME_LIB = "ps3recomp/build/ps3recomp_runtime.lib"
LOCK_FILE = "build/run.lock"
LOG_DIR = "scratch/part3_relift_runs"

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
    main_lrs_in_audits = []
    status_3510 = None
    patches_line = ""
    stalled_predicates = []
    total_presents = 0
    presents_rate_history = {}
    first_draw_time = "N/A"
    crashed = False

    if os.path.exists(stdout_log):
        with open(stdout_log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "[patches] Disable MLAA:" in line or "[patches] on:" in line:
                    patches_line = line.strip()
                m_audit = re.search(r'Thread tid=1 \[main\]:.*lr=(0x[0-9A-Fa-f]+)', line)
                if m_audit:
                    main_lrs_in_audits.append(m_audit.group(1).upper())
                m_boot = re.search(r'\[BOOT-DIAG @ \d+s\] lr=(0x[0-9A-Fa-f]+)', line)
                if m_boot:
                    main_lrs_in_audits.append(m_boot.group(1).upper())
                m_stat = re.search(r'\[STATUS-3510\] \[0x747D3510\]=(0x[0-9A-Fa-f]+|\S+)', line)
                if m_stat:
                    status_3510 = m_stat.group(1)
                
                # [TELEMETRY 5s] wall=30.0s | ... | presents=59.4/s ... | total_presents=1782
                m_telem = re.search(r'\[TELEMETRY 5s\] wall=(\d+)\.0s .* presents=([0-9\.]+)/s .* total_presents=(\d+)', line)
                if m_telem:
                    wall_s = int(m_telem.group(1))
                    pres_rate = float(m_telem.group(2))
                    tot_pres = int(m_telem.group(3))
                    presents_rate_history[wall_s] = pres_rate
                    total_presents = tot_pres
                    if first_draw_time == "N/A" and tot_pres > 0:
                        first_draw_time = f"<{wall_s}s"
                
                # Check for first frame present in live draw or debug
                if first_draw_time == "N/A" and ("first present" in line or "live-draw frame 1" in line or "flip 0" in line):
                    first_draw_time = "<0.5s"

                if "[MAIN-BRB-WAIT]" in line:
                    stalled_predicates.append(line.strip())
                if "FATAL" in line or "EXCEPTION" in line or "Segmentation fault" in line:
                    crashed = True

    if first_draw_time == "N/A" and total_presents > 0:
        first_draw_time = "<1.0s"

    final_main_lr = main_lrs_in_audits[-1] if main_lrs_in_audits else "UNKNOWN"
    is_stalled = (final_main_lr.lower() == "0x0061b9fc")
    passed = (not is_stalled) and (final_main_lr != "UNKNOWN") and not crashed

    # Sample main lr every 10s (10, 20, 30, 40, 50, 60, 70, 80, 90)
    lrs_every_10s = []
    if main_lrs_in_audits:
        step = max(1, len(main_lrs_in_audits) // 9)
        for i in range(min(9, len(main_lrs_in_audits))):
            idx = min(i * step, len(main_lrs_in_audits) - 1)
            lrs_every_10s.append(main_lrs_in_audits[idx])
        while len(lrs_every_10s) < 9:
            lrs_every_10s.append(lrs_every_10s[-1])
    else:
        lrs_every_10s = [final_main_lr] * 9

    rate_30 = presents_rate_history.get(30, presents_rate_history.get(25, 0.0))
    rate_60 = presents_rate_history.get(60, presents_rate_history.get(55, 0.0))
    rate_90 = presents_rate_history.get(90, presents_rate_history.get(85, 0.0))

    return {
        "final_main_lr": final_main_lr,
        "main_lrs_sampled": main_lrs_in_audits,
        "lrs_every_10s": lrs_every_10s,
        "status_3510": status_3510 if status_3510 else "UNKNOWN",
        "patches_line": patches_line,
        "stalled_predicates": stalled_predicates[-2:] if stalled_predicates else [],
        "total_presents": total_presents,
        "first_draw_time": first_draw_time,
        "fps_30": rate_30,
        "fps_60": rate_60,
        "fps_90": rate_90,
        "crashed": crashed,
        "passed": passed
    }

def run_single(run_idx, total_runs, duration, log_dir, runtime_sha, exe_sha):
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

        stdout_log = os.path.join(log_dir, f"Part3_Relift_run{run_idx:02d}.log")
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
        lr_history_str = " -> ".join(parsed["lrs_every_10s"][:4]) + " ... -> " + parsed["lrs_every_10s"][-1]

        print(f"[Part3 Re-lift | Run {run_idx:02d}/{total_runs:02d}] elapsed={elapsed:.1f}s | main_lr={parsed['final_main_lr']} | status_3510={parsed['status_3510']} | passed={parsed['passed']} | 1st_draw={parsed['first_draw_time']} | FPS[30/60/90]={parsed['fps_30']:.1f}/{parsed['fps_60']:.1f}/{parsed['fps_90']:.1f} | tot_pres={parsed['total_presents']}")
        print(f"   Sampled LRs: {lr_history_str}")
        sys.stdout.flush()

        return {
            "run": run_idx,
            "elapsed": elapsed,
            "passed": parsed["passed"],
            "crashed": parsed["crashed"],
            "final_main_lr": parsed["final_main_lr"],
            "status_3510": parsed["status_3510"],
            "first_draw_time": parsed["first_draw_time"],
            "fps_30": parsed["fps_30"],
            "fps_60": parsed["fps_60"],
            "fps_90": parsed["fps_90"],
            "total_presents": parsed["total_presents"],
            "lrs_every_10s": parsed["lrs_every_10s"],
            "patches_line": parsed["patches_line"],
            "exe_sha": exe_sha,
            "runtime_sha": runtime_sha,
            "log": stdout_log
        }
    finally:
        release_lock()

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=20)
    parser.add_argument("--duration", type=int, default=90)
    parser.add_argument("--log-dir", default="scratch/part3_relift_runs")
    args = parser.parse_args()

    os.makedirs(args.log_dir, exist_ok=True)

    runtime_sha = get_sha256(RUNTIME_LIB)
    exe_sha = get_sha256(EXE)
    disk_before = get_disk_info()

    print("================================================================================")
    print("PART 3: Adopt Upstream Lifter Pipeline Native Benchmark")
    print(f"Exe: {EXE} (SHA256: {exe_sha})")
    print(f"Runtime: {RUNTIME_LIB} (SHA256: {runtime_sha})")
    print(f"Disk Before: C: {disk_before['C_free_gb']} GB free | D: {disk_before['D_free_gb']} GB free")
    print(f"Runs: {args.runs} cold x {args.duration}s | Native Merged SPU Re-lifted Pipeline")
    print("================================================================================")
    sys.stdout.flush()

    results = []
    pass_count = 0
    for r in range(1, args.runs + 1):
        res = run_single(r, args.runs, args.duration, args.log_dir, runtime_sha, exe_sha)
        results.append(res)
        if res["passed"]:
            pass_count += 1

    disk_after = get_disk_info()
    pass_rate = (pass_count / args.runs) * 100.0

    print("\n================================================================================")
    print(f"PART 3 BENCHMARK RESULTS: {pass_count}/{args.runs} passed ({pass_rate:.1f}%)")
    print(f"Disk After: C: {disk_after['C_free_gb']} GB free (delta {disk_after['C_free_gb'] - disk_before['C_free_gb']:.2f} GB)")
    print(f"Exe SHA256: {exe_sha}")
    print(f"Runtime SHA256: {runtime_sha}")
    print("================================================================================")

    import json
    with open("scratch/part3_benchmark_results.json", "w") as f:
        json.dump({
            "exe_sha256": exe_sha,
            "runtime_sha256": runtime_sha,
            "runs": args.runs,
            "duration": args.duration,
            "pass_count": pass_count,
            "pass_rate": pass_rate,
            "disk_before": disk_before,
            "disk_after": disk_after,
            "results": results
        }, f, indent=2)

if __name__ == "__main__":
    main()
