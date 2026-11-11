import os
import sys
import time
import re
import json
import subprocess

LOCK_FILE = os.path.join("build", "run.lock")
EXE_PATH = os.path.join("build", "gow_ascension.exe")
EBOOT_PATH = os.path.join("vfs", "PS3_GAME", "USRDIR", "EBOOT.ELF")
LOG_DIR = os.path.join("logs", "phase0b_ab")

CONFIG_A = "3=1,9=5,10=5,11=5,12=5,13=3,15=5,16=5,17=5,18=5"
CONFIG_B = "3=5,13=3,15=5,16=5,17=5"

def clean_fios_cache():
    for root_dir in ["vfs/cache/BCAS25016/fios", "vfs/dev_hdd0/game/BCAS25016"]:
        idx_path = os.path.join(root_dir, "cache.idx")
        if os.path.exists(idx_path):
            try:
                sz = os.path.getsize(idx_path)
                if sz > 0:
                    with open(idx_path, "rb") as f:
                        data = f.read()
                    if all(b == 0 for b in data):
                        os.remove(idx_path)
                        print(f"[CLEAN] Removed zero-filled {idx_path}")
            except Exception as e:
                pass

def parse_log(err_path):
    if not os.path.exists(err_path):
        return None

    with open(err_path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    # 1. Main LR every 10s (10s, 20s, 30s, 40s, 50s, 60s)
    # Pattern:
    # === [TASK4-THREAD-AUDIT @ (\d+)s] ===
    # Thread tid=1 [main]: ... lr=0x([0-9A-Fa-f]+)
    main_lr_by_sec = {}
    audit_blocks = re.findall(r"===\s*\[TASK4-THREAD-AUDIT\s*@\s*(\d+)s\]\s*===(.*?)(?====|\Z)", content, re.DOTALL)
    for sec_str, block in audit_blocks:
        sec = int(sec_str)
        m_lr = re.search(r"Thread\s+tid=1\s+\[main\]:[^\n]*?lr=0x([0-9A-Fa-f]+)", block)
        if m_lr:
            main_lr_by_sec[sec] = m_lr.group(1).upper()

    lr_10s = [main_lr_by_sec.get(s, "------") for s in [10, 20, 30, 40, 50, 60]]
    lr_history_str = "/".join(lr_10s)

    # 2. [0x747D3510] value and brbDriver lr at 60s (or latest)
    # [THREAD9-brbDriver] (tid=9): lr=0x00468A40 r9=... [r9+0x40]=... [0x747D3510]=0x00000000 (-1)
    # or [THREAD9-brbDriver]: lr=0x00468A40 r9=0x747D34D0 [r9+0x40]=0xFFFFFFFF (-1)
    brb_driver_lr = "N/A"
    val_3510 = "N/A"
    m_thread9 = re.findall(r"\[THREAD9-brbDriver\][^\n]*?lr=0x([0-9A-Fa-f]+)", content)
    if m_thread9:
        brb_driver_lr = "0x" + m_thread9[-1].upper()

    m_val = re.findall(r"\[0x747D3510\]=0x([0-9A-Fa-f]+)", content)
    if m_val:
        val_3510 = "0x" + m_val[-1].upper()
    else:
        # Fallback to [r9+0x40] if logged
        m_r9 = re.findall(r"\[r9\+0x40\]=0x([0-9A-Fa-f]+)", content)
        if m_r9:
            val_3510 = "0x" + m_r9[-1].upper()

    # 3. wid 8 pm_entries
    # Workload 'BrBDblBufWrkld' (wid=8, data_ea=...): readyCount=..., pm_entries=11, state=...
    wid8_pm_entries = 0
    m_w8 = re.findall(r"Workload\s+'BrBDblBufWrkld'\s*\(wid=8[^\)]*\):[^\n]*?pm_entries=(\d+)", content)
    if m_w8:
        wid8_pm_entries = int(m_w8[-1])

    # 4. true presents (from [TELEMETRY 5s] total_presents=...)
    total_presents = 0
    m_pres = re.findall(r"\[TELEMETRY 5s\][^\n]*?total_presents=(\d+)", content)
    if m_pres:
        total_presents = int(m_pres[-1])

    # 5. groups exec from [live-draw]
    # [live-draw] frame ... packets[...] groups[seen=... exec=(\d+) ...
    max_groups_exec = 0
    m_grp = re.findall(r"\[live-draw\][^\n]*?groups\[[^\n]*?exec=(\d+)", content)
    if m_grp:
        for g in m_grp:
            val = int(g)
            if val > max_groups_exec:
                max_groups_exec = val

    return {
        "main_lr_history": lr_history_str,
        "lr_10s": lr_10s,
        "val_3510": val_3510,
        "brb_driver_lr": brb_driver_lr,
        "wid8_pm_entries": wid8_pm_entries,
        "total_presents": total_presents,
        "groups_exec": max_groups_exec,
    }

def run_single(name, spu_cont, duration_sec=60):
    os.makedirs(LOG_DIR, exist_ok=True)
    out_log = os.path.join(LOG_DIR, f"{name}_out.log")
    err_log = os.path.join(LOG_DIR, f"{name}_err.log")

    if os.path.exists(out_log): os.remove(out_log)
    if os.path.exists(err_log): os.remove(err_log)

    # Acquire run.lock
    while os.path.exists(LOCK_FILE):
        print(f"Waiting for {LOCK_FILE}...")
        time.sleep(1)

    with open(LOCK_FILE, "w") as f:
        f.write(f"pid={os.getpid()} name={name}\n")

    try:
        clean_fios_cache()

        env = os.environ.copy()
        env["SPURS_MAXCONT"] = spu_cont

        print(f"\n>>> [{name}] Launching gow_ascension.exe (SPURS_MAXCONT='{spu_cont}') for {duration_sec}s...")
        sys.stdout.flush()

        with open(out_log, "wb") as f_out, open(err_log, "wb") as f_err:
            proc = subprocess.Popen(
                [EXE_PATH, EBOOT_PATH],
                stdout=f_out,
                stderr=f_err,
                env=env
            )

            t0 = time.time()
            while time.time() - t0 < duration_sec:
                if proc.poll() is not None:
                    print(f"[{name}] Exited early at {time.time()-t0:.1f}s with code {proc.returncode}")
                    break
                time.sleep(1.0)

            if proc.poll() is None:
                proc.kill()
                try:
                    proc.wait(timeout=5)
                except Exception:
                    pass
                print(f"[{name}] Finished 60s run.")

        data = parse_log(err_log)
        data["name"] = name
        data["log_file"] = err_log
        data["spurs_maxcont"] = spu_cont
        print(f"[{name}] Result: main_lr={data['main_lr_history']} [0x747D3510]={data['val_3510']} brb_lr={data['brb_driver_lr']} wid8_pm={data['wid8_pm_entries']} presents={data['total_presents']} groups={data['groups_exec']}")
        sys.stdout.flush()
        return data

    finally:
        if os.path.exists(LOCK_FILE):
            os.remove(LOCK_FILE)

def main():
    if not os.path.exists(EXE_PATH):
        print(f"ERROR: {EXE_PATH} not found!")
        sys.exit(1)

    print("================================================================================")
    print("PHASE 0b: 10 Cold x 60s Config A (ours) vs 10 Cold x 60s Config B (reference)")
    print("================================================================================")

    all_results = []

    # Config A: 10 runs
    for i in range(1, 11):
        name = f"configA_ours_run{i:02d}"
        res = run_single(name, CONFIG_A, 60)
        all_results.append(res)
        time.sleep(2)

    # Config B: 10 runs
    for i in range(1, 11):
        name = f"configB_ref_run{i:02d}"
        res = run_single(name, CONFIG_B, 60)
        all_results.append(res)
        time.sleep(2)

    summary_file = os.path.join(LOG_DIR, "summary.json")
    with open(summary_file, "w") as f:
        json.dump(all_results, f, indent=2)

    print("\n================================================================================")
    print("ALL 20 RUNS COMPLETE. SUMMARY JSON WRITTEN TO", summary_file)
    print("================================================================================")

if __name__ == "__main__":
    main()
