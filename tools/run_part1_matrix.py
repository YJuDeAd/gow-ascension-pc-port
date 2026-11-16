import os
import sys
import time
import subprocess
import json
import hashlib
import re
from scipy.stats import fisher_exact

LOCK_FILE = "build/run.lock"
EXE = "build/gow_ascension.exe"
TARGET_ELF = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"
RUNTIME_LIB = "ps3recomp/build/libps3recomp_runtime.a"
LOG_DIR = "scratch/part1_runs"
RESULTS_JSON = "scratch/part1_matrix_results.json"

os.makedirs(LOG_DIR, exist_ok=True)
os.makedirs(os.path.dirname(RESULTS_JSON), exist_ok=True)

def get_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

def acquire_lock():
    while True:
        try:
            fd = os.open(LOCK_FILE, os.O_CREAT | os.O_EXCL | os.O_RDWR)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            break
        except FileExistsError:
            time.sleep(1)

def release_lock():
    try:
        os.remove(LOCK_FILE)
    except OSError:
        pass

def kill_procs():
    subprocess.run(["taskkill", "/F", "/IM", "gow_ascension.exe"], capture_output=True)

def run_single(cell_name, run_idx, extra_env, runtime_sha, exe_sha):
    acquire_lock()
    try:
        kill_procs()
        env = os.environ.copy()
        env["PS3_VFS_ROOT"] = "E:"
        env.update(extra_env)

        stdout_log = os.path.join(LOG_DIR, f"{cell_name}_run{run_idx:02d}_stdout.log")
        stderr_log = os.path.join(LOG_DIR, f"{cell_name}_run{run_idx:02d}_stderr.log")

        start_time = time.time()
        with open(stdout_log, "wb") as f_out, open(stderr_log, "wb") as f_err:
            proc = subprocess.Popen([EXE, TARGET_ELF], stdout=f_out, stderr=f_err, env=env)
            try:
                proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        elapsed = time.time() - start_time

        # Analyze logs
        main_lrs = []
        brb_lrs = []
        val_3510s = []
        patch_line = ""

        with open(stdout_log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "[patches]" in line:
                    patch_line = line.strip()

        with open(stderr_log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "Thread tid=1 [main]" in line or "[BOOT-DIAG" in line:
                    m = re.search(r'lr=(0x[0-9A-Fa-f]+)', line)
                    if m:
                        main_lrs.append(m.group(1).upper())
                if "[brbDriver]" in line or "Thread tid=" in line and "brbDriver" in line:
                    m_lr = re.search(r'lr=(0x[0-9A-Fa-f]+)', line)
                    if m_lr:
                        brb_lrs.append(m_lr.group(1).upper())
                if "[0x747D3510]=" in line:
                    m_val = re.search(r'\[0x747D3510\]=(0x[0-9A-Fa-f]+)', line)
                    if m_val:
                        val_3510s.append(m_val.group(1).upper())

        final_main_lr = main_lrs[-1] if main_lrs else "UNKNOWN"
        # Metric: main leaves 0x0061B9FC before 60 s
        passed = (final_main_lr != "0x0061B9FC" and final_main_lr != "UNKNOWN")

        last_brb_lr = brb_lrs[-1] if brb_lrs else "N/A"
        last_val_3510 = val_3510s[-1] if val_3510s else "N/A"

        result = {
            "cell": cell_name,
            "run": run_idx,
            "runtime_sha256": runtime_sha,
            "exe_sha256": exe_sha,
            "elapsed_s": round(elapsed, 2),
            "patch_status": patch_line,
            "final_main_lr": final_main_lr,
            "passed": passed,
            "brb_lr": last_brb_lr,
            "val_3510": last_val_3510,
            "stdout_log": stdout_log,
            "stderr_log": stderr_log
        }

        print(f"[{cell_name} | Run {run_idx:02d}/20] elapsed={elapsed:.1f}s | main_lr={final_main_lr} | passed={passed} | brb_lr={last_brb_lr} | [0x747D3510]={last_val_3510} | exe={exe_sha[:8]}.. | rtm={runtime_sha[:8]}..")
        sys.stdout.flush()
        return result
    finally:
        release_lock()

def main():
    runtime_sha = get_sha256(RUNTIME_LIB)
    exe_sha = get_sha256(EXE)
    print(f"=========================================================================================")
    print(f"PART 1: MLAA-Off vs MLAA-On Matrix Benchmark (20 cold x 60s per cell)")
    print(f"Runtime SHA256: {runtime_sha}")
    print(f"Exe SHA256:     {exe_sha}")
    print(f"=========================================================================================\n")
    sys.stdout.flush()

    results = {"CellA_MLAA_ON": [], "CellB_MLAA_OFF": []}

    # Cell A: Disable MLAA ON (default)
    print("--- Starting Cell A: Disable MLAA ON (default) ---")
    for r in range(1, 21):
        res = run_single("CellA_MLAA_ON", r, {}, runtime_sha, exe_sha)
        results["CellA_MLAA_ON"].append(res)
        with open(RESULTS_JSON, "w") as f:
            json.dump(results, f, indent=2)

    # Cell B: Disable MLAA OFF (GOWA_NO_MLAA=0)
    print("\n--- Starting Cell B: Disable MLAA OFF (GOWA_NO_MLAA=0) ---")
    for r in range(1, 21):
        res = run_single("CellB_MLAA_OFF", r, {"GOWA_NO_MLAA": "0"}, runtime_sha, exe_sha)
        results["CellB_MLAA_OFF"].append(res)
        with open(RESULTS_JSON, "w") as f:
            json.dump(results, f, indent=2)

    # Compute statistics
    pass_a = sum(1 for r in results["CellA_MLAA_ON"] if r["passed"])
    fail_a = 20 - pass_a
    pass_b = sum(1 for r in results["CellB_MLAA_OFF"] if r["passed"])
    fail_b = 20 - pass_b

    # 2x2 contingency table:
    # [[pass_a, fail_a], [pass_b, fail_b]]
    table = [[pass_a, fail_a], [pass_b, fail_b]]
    oddsr, p_value = fisher_exact(table)

    print("\n=========================================================================================")
    print(f"BENCHMARK SUMMARY:")
    print(f"Cell A (Disable MLAA ON, default): {pass_a}/20 passed ({pass_a*5}%)")
    print(f"Cell B (Disable MLAA OFF, stock):  {pass_b}/20 passed ({pass_b*5}%)")
    print(f"Contingency Table: {table}")
    print(f"Two-sided Fisher Exact p-value:    {p_value:.6f}")
    print("=========================================================================================")

if __name__ == "__main__":
    main()
