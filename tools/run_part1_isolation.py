import os
import sys
import time
import subprocess
import json
import hashlib
import re

LOCK_FILE = "build/run.lock"
EXE = "build/gow_ascension.exe"
TARGET_ELF = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"
RUNTIME_LIB = "ps3recomp/build/ps3recomp_runtime.lib"

def get_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
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

def parse_log(stdout_log):
    main_lrs_in_audits = []
    status_3510 = None
    patches_line = ""
    stalled_predicates = []
    last_presents = 0

    if os.path.exists(stdout_log):
        with open(stdout_log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "[patches] Disable MLAA:" in line:
                    patches_line = line.strip()
                m_audit = re.search(r'Thread tid=1 \[main\]:.*lr=(0x[0-9A-Fa-f]+)', line)
                if m_audit:
                    main_lrs_in_audits.append(m_audit.group(1).upper())
                m_stat = re.search(r'\[STATUS-3510\] \[0x747D3510\]=(0x[0-9A-Fa-f]+|\S+)', line)
                if m_stat:
                    status_3510 = m_stat.group(1)
                m_t = re.search(r'total_presents=(\d+)', line)
                if m_t:
                    last_presents = int(m_t.group(1))
                if "[MAIN-BRB-WAIT]" in line:
                    stalled_predicates.append(line.strip())

    if main_lrs_in_audits:
        final_main_lr = main_lrs_in_audits[-1]
    else:
        final_main_lr = "UNKNOWN"

    # Predicate: Main leaves 0x0061B9FC before 60s
    # Stalled condition is: final_main_lr == 0x0061B9FC and status_3510 == -1
    is_stalled = (final_main_lr.lower() == "0x0061b9fc") or (status_3510 in ["0xFFFFFFFF", "-1"])
    passed = (not is_stalled) and (final_main_lr != "UNKNOWN")

    return {
        "final_main_lr": final_main_lr,
        "main_lrs_sampled": main_lrs_in_audits,
        "status_3510": status_3510,
        "patches_line": patches_line,
        "stalled_predicates": stalled_predicates[-2:] if stalled_predicates else [],
        "last_presents": last_presents,
        "passed": passed
    }

def run_single(cell_name, run_idx, total_runs, duration, log_dir, runtime_sha, exe_sha):
    acquire_lock()
    try:
        kill_procs()
        clean_fios_cache()

        env = os.environ.copy()
        env["PS3_VFS_ROOT"] = "E:\\"
        env["PS3_PATCHES"] = "Disable MLAA"
        env["GOWA_NO_MLAA"] = "1"
        env["PS3_MODS"] = "C:/Users/ROBIN/AppData/Roaming/ascension-launcher/generated;D:/gow-bilal-asia/ascension/mods-builtin/skip-intro"
        env["PS3_SKIP_MOVIES"] = "1"
        env["LD_PSO_PRECOMPILE"] = "1"
        env["OCCL"] = "2"
        env["PS3_HOST_PIN"] = "1"

        stdout_log = os.path.join(log_dir, f"{cell_name}_run{run_idx:02d}.log")

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
        kill_procs()

        parsed = parse_log(stdout_log)

        result = {
            "cell": cell_name,
            "run": run_idx,
            "runtime_sha256": runtime_sha,
            "exe_sha256": exe_sha,
            "elapsed_s": round(elapsed, 2),
            "final_main_lr": parsed["final_main_lr"],
            "main_lrs_sampled": parsed["main_lrs_sampled"],
            "status_3510": parsed["status_3510"],
            "passed": parsed["passed"],
            "patches_line": parsed["patches_line"],
            "stalled_predicates": parsed["stalled_predicates"],
            "total_presents": parsed["last_presents"],
            "stdout_log": stdout_log
        }

        print(f"[{cell_name} | Run {run_idx:02d}/{total_runs:02d}] elapsed={elapsed:.1f}s | main_lr={parsed['final_main_lr']} | status_3510={parsed['status_3510']} | passed={parsed['passed']} | presents={parsed['last_presents']} | exe={exe_sha[:8]}.. | rtm={runtime_sha[:8]}..")
        sys.stdout.flush()
        return result
    finally:
        release_lock()

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--cell", required=True, help="Cell identifier (e.g. CellA, CellB, CellC)")
    parser.add_argument("--runs", type=int, default=20, help="Number of runs")
    parser.add_argument("--duration", type=int, default=60, help="Duration per run in seconds")
    parser.add_argument("--log-dir", default="scratch/part1_runs", help="Directory to save logs")
    parser.add_argument("--out-json", default="scratch/part1_results.json", help="Path to output json")
    args = parser.parse_args()

    os.makedirs(args.log_dir, exist_ok=True)
    os.makedirs(os.path.dirname(args.out_json) or ".", exist_ok=True)
    os.makedirs("build", exist_ok=True)

    runtime_sha = get_sha256(RUNTIME_LIB)
    exe_sha = get_sha256(EXE)

    print("=================================================================")
    print(f" PART 1 BENCHMARK: {args.cell}")
    print(f" Metric: main leaves 0x0061B9FC before {args.duration} s, MLAA OFF")
    print(f" Target Exe: {EXE} (SHA256: {exe_sha})")
    print(f" Runtime Lib: {RUNTIME_LIB} (SHA256: {runtime_sha})")
    print(f" Total runs: {args.runs} cold x {args.duration} s")
    print("=================================================================")
    sys.stdout.flush()

    results = []
    if os.path.exists(args.out_json):
        try:
            with open(args.out_json, "r") as f:
                data = json.load(f)
                if isinstance(data, dict) and data.get("cell") == args.cell:
                    results = data.get("runs", [])
        except Exception:
            results = []

    existing_runs = {r["run"] for r in results}
    pass_count = sum(1 for r in results if r["passed"])

    for r in range(1, args.runs + 1):
        if r in existing_runs:
            print(f"[{args.cell} | Run {r:02d}/{args.runs:02d}] Already completed, skipping.")
            continue
        res = run_single(args.cell, r, args.runs, args.duration, args.log_dir, runtime_sha, exe_sha)
        results.append(res)
        if res["passed"]:
            pass_count += 1
        with open(args.out_json, "w") as f:
            json.dump({"cell": args.cell, "runs": results, "pass_count": pass_count, "total_runs": len(results)}, f, indent=2)

    print("=================================================================")
    print(f" BENCHMARK COMPLETE: {args.cell}")
    print(f" Pass rate: {pass_count}/{len(results)} ({pass_count/len(results)*100:.1f}%)")
    print("=================================================================")

if __name__ == "__main__":
    main()
