import os
import sys
import time
import re
import json
import subprocess

WORKTREE_DIR = r"C:\IfItWorksItWorks\Project\gow-port-latest"
LOCK_FILE = os.path.join(WORKTREE_DIR, "build", "run.lock")
EXE_PATH = os.path.join(WORKTREE_DIR, "build", "gow_ascension.exe")
EBOOT_PATH = os.path.join(WORKTREE_DIR, "vfs", "PS3_GAME", "USRDIR", "EBOOT.ELF")
LOG_DIR = os.path.join(WORKTREE_DIR, "logs", "phase1d_ab")

SPURS_MAXCONT_VAL = "3=5,13=3,15=5,16=5,17=5"

def clean_fios_cache():
    for root_rel in ["vfs/cache/BCAS25016/fios", "vfs/dev_hdd0/game/BCAS25016"]:
        root_dir = os.path.join(WORKTREE_DIR, root_rel)
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
            except Exception:
                pass

def parse_log(err_path):
    if not os.path.exists(err_path):
        return None

    with open(err_path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    main_lr_by_sec = {}
    audit_blocks = re.findall(r"===\s*\[TASK4-THREAD-AUDIT\s*@\s*(\d+)s\]\s*===(.*?)(?====|\Z)", content, re.DOTALL)
    for sec_str, block in audit_blocks:
        sec = int(sec_str)
        m_lr = re.search(r"Thread\s+tid=1\s+\[main\]:[^\n]*?lr=0x([0-9A-Fa-f]+)", block)
        if m_lr:
            main_lr_by_sec[sec] = m_lr.group(1).upper()

    lr_10s = [main_lr_by_sec.get(s, "------") for s in [10, 20, 30, 40, 50, 60]]
    lr_history_str = "/".join(lr_10s)
    lr_60s = lr_10s[-1]

    brb_driver_lr = "N/A"
    val_3510 = "N/A"
    m_thread9 = re.findall(r"\[THREAD9-brbDriver\][^\n]*?lr=0x([0-9A-Fa-f]+)", content)
    if m_thread9:
        brb_driver_lr = "0x" + m_thread9[-1].upper()

    m_val = re.findall(r"\[0x747D3510\]=0x([0-9A-Fa-f]+)", content)
    if m_val:
        val_3510 = "0x" + m_val[-1].upper()
    else:
        m_r9 = re.findall(r"\[r9\+0x40\]=0x([0-9A-Fa-f]+)", content)
        if m_r9:
            val_3510 = "0x" + m_r9[-1].upper()

    wid8_pm_entries = 0
    m_w8 = re.findall(r"Workload\s+'BrBDblBufWrkld'\s*\(wid=8[^\)]*\):[^\n]*?pm_entries=(\d+)", content)
    if m_w8:
        wid8_pm_entries = int(m_w8[-1])

    total_presents = 0
    m_pres = re.findall(r"\[TELEMETRY 5s\][^\n]*?total_presents=(\d+)", content)
    if m_pres:
        total_presents = int(m_pres[-1])

    max_groups_exec = 0
    m_grp = re.findall(r"\[live-draw\][^\n]*?groups\[[^\n]*?exec=(\d+)", content)
    if m_grp:
        for g in m_grp:
            val = int(g)
            if val > max_groups_exec:
                max_groups_exec = val

    advanced = (lr_60s != "0061B9FC" and lr_60s != "------" and (val_3510 == "0x00000000" or val_3510 == "0x0"))

    return {
        "main_lr_history": lr_history_str,
        "lr_60s": lr_60s,
        "lr_10s": lr_10s,
        "val_3510": val_3510,
        "brb_driver_lr": brb_driver_lr,
        "wid8_pm_entries": wid8_pm_entries,
        "total_presents": total_presents,
        "groups_exec": max_groups_exec,
        "advanced": advanced
    }

def run_single(name, duration_sec=60):
    os.makedirs(LOG_DIR, exist_ok=True)
    out_log = os.path.join(LOG_DIR, f"{name}_out.log")
    err_log = os.path.join(LOG_DIR, f"{name}_err.log")

    if os.path.exists(out_log): os.remove(out_log)
    if os.path.exists(err_log): os.remove(err_log)

    while os.path.exists(LOCK_FILE):
        print(f"Waiting for {LOCK_FILE}...")
        time.sleep(1)

    with open(LOCK_FILE, "w") as f:
        f.write(f"pid={os.getpid()} name={name}\n")

    try:
        clean_fios_cache()

        env = os.environ.copy()
        env["SPURS_MAXCONT"] = SPURS_MAXCONT_VAL

        print(f"\n>>> [{name}] Launching gow_ascension.exe (SPURS_MAXCONT='{SPURS_MAXCONT_VAL}') for {duration_sec}s...")
        sys.stdout.flush()

        with open(out_log, "wb") as f_out, open(err_log, "wb") as f_err:
            proc = subprocess.Popen(
                [EXE_PATH, EBOOT_PATH],
                cwd=WORKTREE_DIR,
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
                print(f"[{name}] Finished {duration_sec}s run.")

        data = parse_log(err_log)
        data["name"] = name
        data["log_file"] = os.path.basename(err_log)
        print(f"[{name}] Result: main_lr={data['main_lr_history']} [0x747D3510]={data['val_3510']} brb_lr={data['brb_driver_lr']} wid8_pm={data['wid8_pm_entries']} presents={data['total_presents']} groups={data['groups_exec']} advanced={data['advanced']}")
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
    print("PHASE 1d BENCHMARK: 10 Cold x 60s runs on latest ps3recomp (v0.19.0-3-gde4f0903)")
    print(f"Exe SHA256 target: {EXE_PATH}")
    print(f"Pass metric: main lr != 0x0061B9FC at 60s AND [0x747D3510] == 0")
    print("================================================================================")

    results = []
    for i in range(1, 11):
        name = f"phase1d_port_run{i:02d}"
        res = run_single(name, 60)
        results.append(res)
        time.sleep(2)

    summary_file = os.path.join(LOG_DIR, "summary.json")
    with open(summary_file, "w") as f:
        json.dump(results, f, indent=2)

    adv_count = sum(1 for r in results if r["advanced"])
    print("\n================================================================================")
    print(f"PHASE 1d SUMMARY: Advanced: {adv_count}/10")
    print(f"Summary JSON written to: {summary_file}")
    print("================================================================================")

if __name__ == "__main__":
    main()
