import os
import sys
import time
import subprocess
import json
import re
import shutil

RUN_LOCK = "build/run.lock"
OUT_DIR = "scratch/part4"
os.makedirs(OUT_DIR, exist_ok=True)

# Locations
RELEASE_ROOT = r"C:\Users\ROBIN\Downloads\GodOfWarAscension-0.1.33-windows"
RELEASE_LAUNCHER = os.path.join(RELEASE_ROOT, "God of War Ascension Launcher.exe")
RELEASE_EXE = os.path.join(RELEASE_ROOT, "ascension-recomp", "ascension", "build", "win", "AscensionRecomp.exe")
RELEASE_CWD = os.path.join(RELEASE_ROOT, "ascension-recomp", "ascension")
RELEASE_EBOOT = os.path.join(RELEASE_ROOT, "ascension-recomp", "work", "eboot", "EBOOT.ELF")
RELEASE_LAST_RUN_LOG = os.path.join(RELEASE_ROOT, "ascension-recomp", "ascension", "build", "logs", "last_run.log")

SOURCE_ROOT = r"D:\gow-bilal-build"
SOURCE_EXE = os.path.join(SOURCE_ROOT, "ascension", "build", "win", "AscensionRecomp.exe")

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
            fd = os.open(RUN_LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            print(f"[LOCK] Acquired {RUN_LOCK}")
            return
        except FileExistsError:
            # Check stale lock
            try:
                with open(RUN_LOCK, "r") as f:
                    content = f.read().strip()
                if not content or not content.isdigit() or not is_pid_alive(int(content)):
                    print(f"[LOCK] Removing stale lock file: {content}")
                    os.remove(RUN_LOCK)
                    continue
            except Exception:
                pass
            print(f"[LOCK] Waiting for {RUN_LOCK}...")
            time.sleep(1)

def release_lock():
    try:
        if os.path.exists(RUN_LOCK):
            os.remove(RUN_LOCK)
            print(f"[LOCK] Released {RUN_LOCK}")
    except OSError:
        pass

def kill_ascension_procs():
    subprocess.run(["taskkill", "/F", "/IM", "AscensionRecomp.exe"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "God of War Ascension Launcher.exe"], capture_output=True)

# Compute captured environment from launcher
sys.path.insert(0, os.path.join(RELEASE_ROOT, "ascension-recomp", "ascension", "launcher"))
from app import config, game

settings = config.load_settings()
captured_env = game.game_environment(settings)
captured_env['PS3_VFS_ROOT'] = r"E:\\"

def run_direct_proc(name, cmd, cwd, env, duration=90):
    print(f"\n==========================================")
    print(f"STARTING RUN: {name}")
    print(f"Cmd: {cmd}")
    print(f"Cwd: {cwd}")
    print(f"Duration: {duration}s")
    print(f"==========================================")
    
    kill_ascension_procs()
    time.sleep(1)

    log_file = os.path.join(OUT_DIR, f"{name}.log")
    dbg_file = os.path.join(cwd, "dbg.txt")
    dbg_out = os.path.join(cwd, "dbg.txt.out")

    for p in [dbg_file, dbg_out]:
        if os.path.exists(p):
            try: os.remove(p)
            except OSError: pass

    t_start = time.time()
    with open(log_file, "wb") as out_fp:
        proc = subprocess.Popen(
            cmd,
            cwd=cwd,
            env=env,
            stdout=out_fp,
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        )
        
        next_sample = 10
        while time.time() - t_start < duration:
            elapsed = time.time() - t_start
            if elapsed >= next_sample:
                try:
                    with open(dbg_file, "w") as f:
                        f.write("threads\n")
                except Exception:
                    pass
                next_sample += 10
            if proc.poll() is not None:
                print(f"[WARN] Process {name} exited prematurely at {elapsed:.2f}s with code {proc.returncode}")
                break
            time.sleep(0.5)

        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    kill_ascension_procs()
    t_end = time.time()
    real_duration = t_end - t_start
    print(f"FINISHED RUN: {name} in {real_duration:.2f}s")

    if os.path.exists(dbg_out):
        try:
            shutil.copy2(dbg_out, os.path.join(OUT_DIR, f"{name}_dbg.txt.out"))
        except Exception:
            pass

    return log_file, real_duration

def run_launcher_proc(name, duration=90):
    print(f"\n==========================================")
    print(f"STARTING RUN: {name} (via Launcher)")
    print(f"Cmd: {RELEASE_LAUNCHER} --play")
    print(f"Duration: {duration}s")
    print(f"==========================================")

    kill_ascension_procs()
    time.sleep(1)

    # Archive previous last_run.log
    if os.path.exists(RELEASE_LAST_RUN_LOG):
        try: os.remove(RELEASE_LAST_RUN_LOG)
        except OSError: pass

    t_start = time.time()
    launcher_proc = subprocess.Popen(
        [RELEASE_LAUNCHER, "--play"],
        cwd=RELEASE_ROOT,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    )

    while time.time() - t_start < duration:
        time.sleep(1)

    kill_ascension_procs()
    if launcher_proc.poll() is None:
        launcher_proc.kill()
        launcher_proc.wait()

    t_end = time.time()
    real_duration = t_end - t_start
    print(f"FINISHED RUN: {name} in {real_duration:.2f}s")

    log_file = os.path.join(OUT_DIR, f"{name}.log")
    if os.path.exists(RELEASE_LAST_RUN_LOG):
        shutil.copy2(RELEASE_LAST_RUN_LOG, log_file)
    else:
        with open(log_file, "w") as f:
            f.write("last_run.log not found after launcher run\n")

    return log_file, real_duration

def analyze_log(log_path):
    if not os.path.exists(log_path):
        return {}
    
    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = [line.rstrip() for line in f]

    first_80 = lines[:80]
    first_draw_time = None
    flips = 0
    presents = 0
    pso_count = 0
    pso_time = 0.0
    lrs = []
    
    for line in lines:
        if any(term in line for term in ["frame 1 ", "[VPSUBMIT] draw", "[live-draw] frame 1", "flip 1"]):
            if first_draw_time is None:
                first_draw_time = "detected"
        
        m_flip = re.search(r'flip (\d+)', line)
        if m_flip:
            flips = max(flips, int(m_flip.group(1)))
        m_flip2 = re.search(r'guest flips: (\d+)', line)
        if m_flip2:
            flips = max(flips, int(m_flip2.group(1)))
            
        m_pres = re.search(r'presents=(\d+)', line)
        if m_pres:
            presents = max(presents, int(m_pres.group(1)))

        if "[pso-async]" in line:
            pso_count += 1
            m_ms = re.search(r'\((\d+)\s*ms', line)
            if m_ms:
                pso_time += float(m_ms.group(1))
        elif "[pso]" in line and "made" in line:
            pso_count += 1

        m_lr = re.search(r'lr=(0x[0-9A-Fa-f]+)', line)
        if m_lr:
            lrs.append(m_lr.group(1).upper())

    return {
        "line_count": len(lines),
        "first_80": first_80,
        "first_draw": first_draw_time or "not_logged",
        "flips": flips,
        "presents": presents,
        "pso_count": pso_count,
        "pso_time_ms": pso_time,
        "sample_lrs": list(dict.fromkeys(lrs))[:10]
    }

def main():
    acquire_lock()
    try:
        manifest = []

        # 1. Release via Launcher (once, 90s)
        log1, d1 = run_launcher_proc("run1_release_launcher", duration=90)
        manifest.append(("run1_release_launcher", log1, d1, "Release (via Launcher)"))

        # 2. Release directly (3 cold x 90s)
        cmd_rel = [RELEASE_EXE, RELEASE_EBOOT]
        for i in range(1, 4):
            rname = f"run2_{i}_release_direct"
            log_i, di = run_direct_proc(rname, cmd_rel, RELEASE_CWD, captured_env, duration=90)
            manifest.append((rname, log_i, di, f"Release Direct #{i}"))

        # 3. Source build directly with same cmd/env/disc (3 cold x 90s)
        cmd_src = [SOURCE_EXE, RELEASE_EBOOT]
        for i in range(1, 4):
            rname = f"run3_{i}_source_direct"
            log_i, di = run_direct_proc(rname, cmd_src, RELEASE_CWD, captured_env, duration=90)
            manifest.append((rname, log_i, di, f"Source Direct #{i}"))

        # 4. Source build once with launcher's env/settings applied (once, 90s)
        rname = "run4_source_launcher_env"
        log4, d4 = run_direct_proc(rname, cmd_src, RELEASE_CWD, captured_env, duration=90)
        manifest.append((rname, log4, d4, "Source with Launcher Env"))

        # Process and summarize results
        results = []
        for rname, log_path, duration, desc in manifest:
            info = analyze_log(log_path)
            results.append({
                "run_id": rname,
                "description": desc,
                "duration": round(duration, 2),
                "log_file": log_path,
                "line_count": info.get("line_count", 0),
                "first_draw": info.get("first_draw", "N/A"),
                "flips": info.get("flips", 0),
                "fps": round(info.get("flips", 0) / max(duration, 1), 2),
                "presents": info.get("presents", 0),
                "pso_count": info.get("pso_count", 0),
                "pso_time_ms": info.get("pso_time_ms", 0),
                "sample_lrs": info.get("sample_lrs", []),
                "first_80_preview": info.get("first_80", [])[:5]
            })

        summary_path = os.path.join(OUT_DIR, "part4_summary.json")
        with open(summary_path, "w", encoding="utf-8") as f:
            json.dump(results, f, indent=2)
        print(f"\n[DONE] Saved summary to {summary_path}")

        # Compute divergence between Release Direct and Source Direct
        rel_log = os.path.join(OUT_DIR, "run2_1_release_direct.log")
        src_log = os.path.join(OUT_DIR, "run3_1_source_direct.log")
        if os.path.exists(rel_log) and os.path.exists(src_log):
            r_lines = [l.strip() for l in open(rel_log, encoding='utf-8', errors='ignore') if l.strip()]
            s_lines = [l.strip() for l in open(src_log, encoding='utf-8', errors='ignore') if l.strip()]
            divergence = None
            for idx, (rl, sl) in enumerate(zip(r_lines, s_lines)):
                if rl != sl:
                    divergence = (idx + 1, rl, sl)
                    break
            div_path = os.path.join(OUT_DIR, "divergence.json")
            with open(div_path, "w", encoding="utf-8") as f:
                json.dump({
                    "divergence_line": divergence[0] if divergence else None,
                    "release_line": divergence[1] if divergence else None,
                    "source_line": divergence[2] if divergence else None
                }, f, indent=2)
            if divergence:
                print(f"[DIVERGENCE] First divergence at line {divergence[0]}:")
                print(f"  Release: {divergence[1]}")
                print(f"  Source : {divergence[2]}")

    finally:
        release_lock()

if __name__ == "__main__":
    main()
