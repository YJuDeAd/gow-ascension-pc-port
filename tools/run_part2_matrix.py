import os
import sys
import time
import subprocess
import json
import hashlib
import re
import shutil
import threading

LOCK_FILE = "build/run.lock"
EXE = "D:/gow-bilal-asia/ascension/build/win/AscensionRecomp.exe"
TARGET_ELF = "D:/gow-bilal-asia/EBOOT_PRISTINE.ELF"
RUNTIME_LIB = "D:/gow-bilal-asia/ascension/build/win/ps3recomp_sdk/libps3recomp_runtime.a"
LOG_DIR = "scratch/part2_runs"
RESULTS_JSON = "scratch/part2_matrix_results.json"

os.makedirs(LOG_DIR, exist_ok=True)
os.makedirs(os.path.dirname(RESULTS_JSON), exist_ok=True)

def get_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

def is_pid_alive(pid):
    try:
        import psutil
        return psutil.pid_exists(pid)
    except Exception:
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}"], capture_output=True, text=True).stdout
        return str(pid) in out

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
    subprocess.run(["taskkill", "/F", "/IM", "gow_ascension.exe"], capture_output=True)

def run_single(cell_name, run_idx, runtime_sha, exe_sha):
    acquire_lock()
    try:
        kill_procs()
        env = os.environ.copy()
        env["PS3_VFS_ROOT"] = "E:\\"
        env["PS3_PATCHES"] = "Disable MLAA"
        env["PS3_MODS"] = "C:/Users/ROBIN/AppData/Roaming/ascension-launcher/generated;D:/gow-bilal-asia/ascension/mods-builtin/skip-intro"
        env["PS3_SKIP_MOVIES"] = "1"
        env["LD_PSO_PRECOMPILE"] = "1"
        env["OCCL"] = "2"
        env["PS3_HOST_PIN"] = "1"

        dbg_file = f"D:/gow-bilal-asia/ascension/dbg_{cell_name}_{run_idx:02d}.txt"
        dbg_out = dbg_file + ".out"
        if os.path.exists(dbg_file): os.remove(dbg_file)
        if os.path.exists(dbg_out): os.remove(dbg_out)
        env["PS3_DEBUG"] = dbg_file

        stdout_log = os.path.join(LOG_DIR, f"{cell_name}_run{run_idx:02d}.log")

        def sampler_worker():
            sample_times = [10, 20, 30, 40, 50, 58]
            t0 = time.time()
            for st in sample_times:
                now = time.time() - t0
                if st > now:
                    time.sleep(st - now)
                try:
                    with open(dbg_file, "w") as df:
                        df.write("threads\nstat\n")
                except Exception:
                    pass

        sampler_th = threading.Thread(target=sampler_worker, daemon=True)
        sampler_th.start()

        start_time = time.time()
        with open(stdout_log, "w", encoding="utf-8") as f_out:
            proc = subprocess.Popen([EXE, TARGET_ELF], cwd="D:/gow-bilal-asia/ascension/build/win", stdout=f_out, stderr=subprocess.STDOUT, env=env)
            try:
                proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        elapsed = time.time() - start_time
        kill_procs()

        # Parse logs
        presents = []
        lrs = []
        with open(stdout_log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                m_p = re.search(r'\[present\] per 5s: main=(\d+)', line)
                if m_p:
                    presents.append(int(m_p.group(1)))
                m_lr = re.search(r'guest-tid\s+1\s+lr=(0x[0-9A-Fa-f]+)', line)
                if m_lr:
                    lrs.append(m_lr.group(1).upper())

        if os.path.exists(dbg_out):
            with open(dbg_out, "r", encoding="utf-8", errors="replace") as f:
                for line in f:
                    m_lr = re.search(r'guest-tid\s+1\s+lr=(0x[0-9A-Fa-f]+)', line)
                    if m_lr:
                        lrs.append(m_lr.group(1).upper())
                    m_fn = re.search(r'func_([0-9A-Fa-f]+)', line)
                    if m_fn and "tid  7472" in line: # main or game thread
                        pass

        # If presents > 0 and frames presented cleanly, main has left 0x0061B9FC
        final_main_lr = lrs[-1] if lrs else ("LEFT_PAST_BRB" if len(presents) > 0 and presents[-1] > 0 else "0x0061B9FC")
        passed = (len(presents) > 0 and presents[-1] > 0) or (final_main_lr.lower() != "0x0061b9fc")

        last_presents = presents[-1] if presents else 0

        result = {
            "cell": cell_name,
            "run": run_idx,
            "runtime_sha256": runtime_sha,
            "exe_sha256": exe_sha,
            "elapsed_s": round(elapsed, 2),
            "final_main_lr": final_main_lr,
            "presents_5s": presents,
            "passed": passed,
            "stdout_log": stdout_log
        }

        print(f"[{cell_name} | Run {run_idx:02d}/20] elapsed={elapsed:.1f}s | main_lr={final_main_lr} | passed={passed} | flips_5s={last_presents} | exe={exe_sha[:8]}.. | rtm={runtime_sha[:8]}..")
        sys.stdout.flush()
        return result
    finally:
        release_lock()

def main():
    runtime_sha = get_sha256(RUNTIME_LIB)
    exe_sha = get_sha256(EXE)
    print("=================================================================")
    print(" PART 2 BENCHMARK: (their runtime, their lifted code) with MLAA OFF")
    print("=================================================================")
    print(f" Runtime Lib SHA256: {runtime_sha}")
    print(f" Executable SHA256:  {exe_sha}")
    print("=================================================================")
    sys.stdout.flush()

    results = []
    if os.path.exists(RESULTS_JSON):
        try:
            with open(RESULTS_JSON, "r") as f:
                results = json.load(f)
        except Exception:
            results = []

    cell_name = "their_rtm_their_lift"
    existing_runs = {r["run"] for r in results if r["cell"] == cell_name}

    for i in range(1, 21):
        if i in existing_runs:
            print(f"[{cell_name} | Run {i:02d}/20] Already completed, skipping.")
            continue
        r = run_single(cell_name, i, runtime_sha, exe_sha)
        results.append(r)
        with open(RESULTS_JSON, "w") as f:
            json.dump(results, f, indent=2)

    cell_results = [r for r in results if r["cell"] == cell_name]
    passed_count = sum(1 for r in cell_results if r["passed"])
    print("=================================================================")
    print(f" Finished: {passed_count}/{len(cell_results)} passed main leaves 0x0061B9FC before 60 s")
    print("=================================================================")

if __name__ == "__main__":
    main()
