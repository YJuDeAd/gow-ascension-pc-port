import os
import sys
import time
import subprocess
import hashlib
import json
import re

def get_file_sha256(path):
    if not os.path.exists(path):
        return "MISSING"
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

def acquire_lock(lock_file="build/run.lock"):
    while True:
        try:
            fd = os.open(lock_file, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            return
        except FileExistsError:
            time.sleep(1)

def release_lock(lock_file="build/run.lock"):
    try:
        os.remove(lock_file)
    except OSError:
        pass

def clean_fios_cache():
    cache_path = os.path.join("vfs", "dev_hdd0", "game", "BCAS25016", "cache.idx")
    if os.path.exists(cache_path):
        try:
            os.remove(cache_path)
        except OSError:
            pass

def run_single(cell_name, run_idx, total_runs=20, timeout_s=60):
    runtime_lib = "ps3recomp/build/libps3recomp_runtime.a"
    exe_path = "build/gow_ascension.exe"
    elf_path = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"

    lib_sha = get_file_sha256(runtime_lib)
    exe_sha = get_file_sha256(exe_path)

    log_dir = os.path.join("scratch", "part2", cell_name)
    os.makedirs(log_dir, exist_ok=True)
    stderr_path = os.path.join(log_dir, f"run_{run_idx:02d}_stderr.log")
    stdout_path = os.path.join(log_dir, f"run_{run_idx:02d}_stdout.log")

    clean_fios_cache()

    env = os.environ.copy()
    env["PS3_VFS_ROOT"] = "E:"

    acquire_lock()
    t_start = time.time()
    main_left = False
    leave_t = None
    last_lr = "N/A"

    with open(stderr_path, "wb") as f_err, open(stdout_path, "wb") as f_out:
        p = subprocess.Popen([exe_path, elf_path], stdout=f_out, stderr=f_err, env=env)
        try:
            while time.time() - t_start < timeout_s:
                if p.poll() is not None:
                    break
                time.sleep(1.0)
                # Check stderr for main_lr updates
                if os.path.exists(stderr_path):
                    with open(stderr_path, "r", errors="ignore") as f:
                        content = f.read()
                        # Find all occurrences of lr
                        m_boot = re.findall(r'\[BOOT-DIAG\s+@\s+\d+s\]\s+lr=(0x[0-9A-Fa-f]+)', content)
                        m_tid1 = re.findall(r'Thread tid=1 \[main\]:.*?lr=(0x[0-9A-Fa-f]+)', content)
                        m_brb = re.findall(r'\[MAIN-BRB-WAIT\]', content)
                        
                        current_lr = None
                        if m_tid1:
                            current_lr = m_tid1[-1].upper()
                        elif m_boot:
                            current_lr = m_boot[-1].upper()
                        
                        if current_lr:
                            last_lr = current_lr
                            if current_lr != "0X0061B9FC" and current_lr != "0X00000000":
                                if not main_left:
                                    main_left = True
                                    leave_t = time.time() - t_start

            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
        finally:
            release_lock()

    # Final check of complete log
    with open(stderr_path, "r", errors="ignore") as f:
        full_content = f.read()
        m_boot = re.findall(r'\[BOOT-DIAG\s+@\s+\d+s\]\s+lr=(0x[0-9A-Fa-f]+)', full_content)
        m_tid1 = re.findall(r'Thread tid=1 \[main\]:.*?lr=(0x[0-9A-Fa-f]+)', full_content)
        m_stages = re.findall(r'\[STAGE\].*?main_lr=(0x[0-9A-Fa-f]+)', full_content)
        
        all_lrs = []
        for lr in m_boot + m_tid1 + m_stages:
            lr_u = lr.upper()
            if lr_u != "0X00000000":
                all_lrs.append(lr_u)
        
        if all_lrs:
            last_lr = all_lrs[-1]
            for lr_u in all_lrs:
                if lr_u != "0X0061B9FC":
                    main_left = True
                    break

    dur = time.time() - t_start
    result = {
        "cell": cell_name,
        "run": run_idx,
        "lib_sha": lib_sha,
        "exe_sha": exe_sha,
        "main_left_61b9fc": main_left,
        "leave_time_s": round(leave_t, 2) if leave_t else None,
        "last_lr": last_lr,
        "duration_s": round(dur, 1),
        "log_path": stderr_path
    }
    return result

def main():
    if len(sys.argv) < 2:
        print("Usage: run_cell_benchmark.py <cell_name> [num_runs=20] [timeout_s=60]")
        sys.exit(1)
    
    cell_name = sys.argv[1]
    num_runs = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    timeout_s = int(sys.argv[3]) if len(sys.argv) > 3 else 60

    print(f"=== BENCHMARK START: {cell_name} ({num_runs} runs x {timeout_s}s) ===")
    results = []
    summary_path = os.path.join("scratch", "part2", f"{cell_name}_results.json")

    for i in range(1, num_runs + 1):
        r = run_single(cell_name, i, num_runs, timeout_s)
        results.append(r)
        status = "PASSED" if r["main_left_61b9fc"] else "STUCK_61B9FC"
        t_info = f"at {r['leave_time_s']}s" if r['leave_time_s'] else "never"
        print(f"| {r['cell']} | Run {r['run']:02d} | {r['lib_sha'][:12]}... | {r['exe_sha'][:12]}... | {status:12s} | Last LR: {r['last_lr']:10s} | Left: {t_info} | Dur: {r['duration_s']}s |")
        sys.stdout.flush()

        with open(summary_path, "w") as f_json:
            json.dump(results, f_json, indent=2)

    passed_count = sum(1 for r in results if r["main_left_61b9fc"])
    print(f"=== SUMMARY {cell_name}: {passed_count}/{num_runs} passed (left 0x0061B9FC) ===")

if __name__ == "__main__":
    main()
