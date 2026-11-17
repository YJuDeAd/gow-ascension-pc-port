import os
import sys
import time
import subprocess
import hashlib
import re

EXE = "build/gow_ascension.exe"
TARGET_ELF = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"
RUNTIME_LIB = "ps3recomp/build/ps3recomp_runtime.lib"
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

def parse_log(stdout_log):
    main_lrs_in_audits = []
    status_3510 = None
    patches_line = ""
    stalled_predicates = []
    last_presents = 0
    interp_confirmed = False

    if os.path.exists(stdout_log):
        with open(stdout_log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "[patches] Disable MLAA:" in line or "[patches] on:" in line:
                    patches_line = line.strip()
                m_audit = re.search(r'Thread tid=1 \[main\]:.*lr=(0x[0-9A-Fa-f]+)', line)
                if m_audit:
                    main_lrs_in_audits.append(m_audit.group(1).upper())
                m_stat = re.search(r'\[STATUS-3510\] \[0x747D3510\]=(0x[0-9A-Fa-f]+|\S+)', line)
                if m_stat:
                    status_3510 = m_stat.group(1)
                m_t = re.search(r'presents: total=(\d+)', line)
                if m_t:
                    last_presents = int(m_t.group(1))
                if "[MAIN-BRB-WAIT]" in line:
                    stalled_predicates.append(line.strip())
                if "[spu] SPU_NOLIFT:" in line or "[spu-smc] microstep img=2" in line or "[wws-jobentry]" in line:
                    interp_confirmed = True

    if main_lrs_in_audits:
        final_main_lr = main_lrs_in_audits[-1]
    else:
        final_main_lr = "UNKNOWN"

    is_stalled = (final_main_lr.lower() == "0x0061b9fc") or (status_3510 in ["0xFFFFFFFF", "-1"])
    passed = (not is_stalled) and (final_main_lr != "UNKNOWN")

    return {
        "final_main_lr": final_main_lr,
        "main_lrs_sampled": main_lrs_in_audits,
        "status_3510": status_3510 if status_3510 else "UNKNOWN",
        "patches_line": patches_line,
        "stalled_predicates": stalled_predicates[-2:] if stalled_predicates else [],
        "last_presents": last_presents,
        "passed": passed,
        "interp_confirmed": interp_confirmed
    }

def run_single(run_idx, total_runs, duration, log_dir, runtime_sha, exe_sha):
    acquire_lock()
    try:
        kill_procs()
        clean_fios_cache()

        env = os.environ.copy()
        env["SPU_NOLIFT"] = "2:0"
        env["SPU_INTERP_UNLIFTED"] = "1"
        env["PS3_PATCHES"] = "Disable MLAA"
        env["GOWA_NO_MLAA"] = "1"
        env["PS3_VFS_ROOT"] = "E:\\"
        env["PS3_MODS"] = "C:/Users/ROBIN/AppData/Roaming/ascension-launcher/generated;D:/gow-bilal-asia/ascension/mods-builtin/skip-intro"
        env["PS3_SKIP_MOVIES"] = "1"
        env["LD_PSO_PRECOMPILE"] = "1"
        env["OCCL"] = "2"
        env["PS3_HOST_PIN"] = "1"

        stdout_log = os.path.join(log_dir, f"Part2_Interp_run{run_idx:02d}.log")
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
        lr_history_str = " -> ".join(parsed["main_lrs_sampled"]) if parsed["main_lrs_sampled"] else parsed["final_main_lr"]

        print(f"[Part2 Interp | Run {run_idx:02d}/{total_runs:02d}] elapsed={elapsed:.1f}s | main_lr={parsed['final_main_lr']} | status_3510={parsed['status_3510']} | passed={parsed['passed']} | interp={parsed['interp_confirmed']} | presents={parsed['last_presents']}")
        print(f"   LRs: {lr_history_str}")
        sys.stdout.flush()

        return {
            "run": run_idx,
            "elapsed": elapsed,
            "passed": parsed["passed"],
            "final_main_lr": parsed["final_main_lr"],
            "status_3510": parsed["status_3510"],
            "interp": parsed["interp_confirmed"],
            "presents": parsed["last_presents"],
            "main_lrs": parsed["main_lrs_sampled"],
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
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument("--log-dir", default="scratch/part2_interp_runs")
    args = parser.parse_args()

    os.makedirs(args.log_dir, exist_ok=True)

    runtime_sha = get_sha256(RUNTIME_LIB)
    exe_sha = get_sha256(EXE)

    print("================================================================================")
    print("PART 2: Bug or Race? SPU Interpreter wws_job_pm (image 2)")
    print(f"Exe: {EXE} (SHA256: {exe_sha})")
    print(f"Runtime: {RUNTIME_LIB} (SHA256: {runtime_sha})")
    print(f"Runs: {args.runs} cold x {args.duration}s | Metric: main leaves 0x0061B9FC before 60s")
    print("================================================================================")
    sys.stdout.flush()

    results = []
    pass_count = 0
    for r in range(1, args.runs + 1):
        res = run_single(r, args.runs, args.duration, args.log_dir, runtime_sha, exe_sha)
        results.append(res)
        if res["passed"]:
            pass_count += 1

    print("\n================================================================================")
    print(f"PART 2 SUMMARY: {pass_count}/{args.runs} passed ({pass_count/args.runs*100:.1f}%)")
    print("================================================================================")

    csv_path = os.path.join(args.log_dir, "part2_summary.csv")
    with open(csv_path, "w", encoding="utf-8") as f:
        f.write("run,elapsed,passed,final_main_lr,status_3510,interp,presents,exe_sha,runtime_sha,log\n")
        for res in results:
            f.write(f"{res['run']},{res['elapsed']:.1f},{res['passed']},{res['final_main_lr']},{res['status_3510']},{res['interp']},{res['presents']},{res['exe_sha']},{res['runtime_sha']},{res['log']}\n")
    print(f"Wrote summary to {csv_path}")

if __name__ == "__main__":
    main()
