import os
import sys
import time
import subprocess
import argparse
import re
import json

def get_cpu_info():
    info = {"model": "Unknown", "physical_cores": 0, "logical_processors": 0, "hybrid": False}
    try:
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", "Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors | ConvertTo-Json"],
            text=True
        )
        data = json.loads(out)
        if isinstance(data, list):
            data = data[0]
        info["model"] = data.get("Name", "").strip()
        info["physical_cores"] = data.get("NumberOfCores", 0)
        info["logical_processors"] = data.get("NumberOfLogicalProcessors", 0)
        if "12th Gen" in info["model"] or "13th Gen" in info["model"] or "14th Gen" in info["model"]:
            info["hybrid"] = True
    except Exception as e:
        print(f"Error querying CPU info: {e}")
    return info

def ensure_iso_mounted():
    psarc = r"E:\PS3_GAME\USRDIR\GAME.PSARC"
    if not os.path.exists(psarc):
        print(f"[ISO] {psarc} not found, checking candidates to mount...")
        iso_candidates = [
            r"D:\God of War - Ascension (Asia) (En,Zh,Ko).dec.iso",
            r"C:\IfItWorksItWorks\Project\gowassension\God of War - Ascension (Asia) (En,Zh,Ko).dec.iso"
        ]
        for iso in iso_candidates:
            if os.path.exists(iso):
                print(f"[ISO] Mounting {iso} to virtual drive...")
                subprocess.run(["powershell", "-NoProfile", "-Command", f'Mount-DiskImage -ImagePath "{iso}" -StorageType ISO'], check=False)
                break
    if os.path.exists(psarc):
        print(f"[ISO] Verified {psarc} exists.")
    else:
        print(f"[ISO] WARNING: {psarc} NOT found!")

def clean_fios_cache(root):
    cache_path = os.path.join(root, "vfs", "dev_hdd0", "game", "BCAS25016", "cache.idx")
    if os.path.exists(cache_path):
        try:
            with open(cache_path, "rb") as f:
                data = f.read()
            if len(data) > 0 and all(b == 0 for b in data):
                print(f"[FIOS-CLEAN] Removing zero-filled cache.idx ({len(data)} bytes)")
                os.remove(cache_path)
        except Exception as e:
            print(f"[FIOS-CLEAN] Error checking cache.idx: {e}")

def parse_run_log(log_path):
    res = {
        "log": os.path.basename(log_path),
        "exit_method": "STALLED",
        "val_3510": "0xDEADBEEF",
        "brb_lr": "0x00000000",
        "w8_pm": 0,
        "main_lr_60s": "0x00000000",
        "presents": 0,
        "thread_profile_15s": None,
        "thread_profile_30s": None,
        "lr_history": []
    }
    if not os.path.exists(log_path):
        return res

    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # 1. Summary Block
    m_sum = re.search(
        r'===\s+\[RUN-SUMMARY @ 60s\]\s+===\s+'
        r'EXIT_METHOD:\s+([^\r\n]+)\s+'
        r'VAL_3510:\s+([^\r\n]+)\s+'
        r'BRBDRIVER_LR:\s+([^\r\n]+)\s+'
        r'W8_PM:\s+(\d+)\s+'
        r'MAIN_LR:\s+([^\r\n]+)\s+'
        r'PRESENTS:\s+(\d+)',
        content
    )
    if m_sum:
        res["exit_method"] = m_sum.group(1).strip()
        res["val_3510"] = m_sum.group(2).strip()
        res["brb_lr"] = m_sum.group(3).strip()
        res["w8_pm"] = int(m_sum.group(4))
        res["main_lr_60s"] = m_sum.group(5).strip()
        res["presents"] = int(m_sum.group(6))
    else:
        # Fallback regexes
        m_leave = re.search(r'\[MAIN-LEAVE-0x0061B9FC\].*?method=(\w+)', content)
        if m_leave:
            res["exit_method"] = m_leave.group(1)
        elif "[MAIN-BRB-WAIT]" in content:
            res["exit_method"] = "STALLED"

        m_3510 = re.findall(r'\[TRANSITION-0x747D3510\].*?new=0x([0-9A-Fa-f]+)', content)
        if m_3510:
            res["val_3510"] = f"0x{m_3510[-1]}"
        else:
            m_3510_2 = re.findall(r'\[0x747D3510\]=0x([0-9A-Fa-f]+)', content)
            if m_3510_2:
                res["val_3510"] = f"0x{m_3510_2[-1]}"

        m_brb = re.findall(r'\[THREAD9-brbDriver\].*?lr=(0x[0-9A-Fa-f]+)', content)
        if m_brb:
            res["brb_lr"] = m_brb[-1]

        m_w8 = re.findall(r'w8_pm=(\d+)', content)
        if m_w8:
            res["w8_pm"] = int(m_w8[-1])

        m_mlr = re.findall(r'main_lr=(0x[0-9A-Fa-f]+)', content)
        if m_mlr:
            res["main_lr_60s"] = m_mlr[-1]

        m_pres = re.findall(r'\[buf0=(\d+),\s*buf1=(\d+)\]', content)
        if m_pres:
            res["presents"] = int(m_pres[-1][0]) + int(m_pres[-1][1])

    # 2. Thread profile @ 15s and 30s
    p15 = re.search(r'(===\s+\[THREAD-PROFILE @ 15s\]\s+===.*?===============================)', content, re.DOTALL)
    if p15:
        res["thread_profile_15s"] = p15.group(1).strip()
    p30 = re.search(r'(===\s+\[THREAD-PROFILE @ 30s\]\s+===.*?===============================)', content, re.DOTALL)
    if p30:
        res["thread_profile_30s"] = p30.group(1).strip()

    # 3. LR History
    m_hist = re.findall(r'\[MAIN-LR-HISTORY @ (\d+)s\]\s+lr=(0x[0-9A-Fa-f]+)', content)
    res["lr_history"] = [{"sec": int(s), "lr": lr} for s, lr in m_hist]

    return res

CELLS = {
    "cell1": {
        "name": "Cell 1 (PIN=1, BOOST=unset)",
        "prefix": "cell1_pin1_boostUnset",
        "env": {"SPURS_PIN": "1", "PS3_PIN_PPU": "1"},
        "unset": ["SPURS_BOOST_WID"]
    },
    "cell2": {
        "name": "Cell 2 (PIN=1, BOOST=8=2)",
        "prefix": "cell2_pin1_boost82",
        "env": {"SPURS_PIN": "1", "PS3_PIN_PPU": "1", "SPURS_BOOST_WID": "8=2"},
        "unset": []
    },
    "cell3": {
        "name": "Cell 3 (PIN=0, BOOST=unset)",
        "prefix": "cell3_pin0_boostUnset",
        "env": {"SPURS_PIN": "0", "PS3_PIN_PPU": "0"},
        "unset": ["SPURS_BOOST_WID"]
    },
    "cell4": {
        "name": "Cell 4 (PIN=0, BOOST=8=2)",
        "prefix": "cell4_pin0_boost82",
        "env": {"SPURS_PIN": "0", "PS3_PIN_PPU": "0", "SPURS_BOOST_WID": "8=2"},
        "unset": []
    }
}

def run_cell(cell_key, runs_per_cell=20, duration=62):
    cell = CELLS[cell_key]
    root = os.path.abspath(".")
    lock_file = os.path.join(root, "build", "run.lock")
    exe = os.path.join(root, "build", "gow_ascension.exe")
    elf = os.path.join(root, "vfs", "PS3_GAME", "USRDIR", "EBOOT.ELF")
    outdir = os.path.join(root, "scratch", "matrix")
    os.makedirs(outdir, exist_ok=True)

    print("\n" + "=" * 70)
    print(f"STARTING {cell['name']}")
    print(f"Runs: {runs_per_cell}, Duration: {duration}s per run")
    print(f"Env: {cell['env']}, Unset: {cell['unset']}")
    print("=" * 70)

    cell_results = []

    for r in range(1, runs_per_cell + 1):
        run_id = f"{r:02d}"
        prefix = cell["prefix"]
        err_log = os.path.join(outdir, f"{prefix}_run{run_id}_stderr.log")
        out_log = os.path.join(outdir, f"{prefix}_run{run_id}_stdout.log")

        # Serial lock
        if os.path.exists(lock_file):
            try: os.remove(lock_file)
            except: pass
        with open(lock_file, "w") as f:
            f.write(f"Cell={cell_key}, Run={r}, Start={time.time()}\n")

        # Cold clean
        clean_fios_cache(root)

        if os.path.exists(err_log): os.remove(err_log)
        if os.path.exists(out_log): os.remove(out_log)

        run_env = os.environ.copy()
        for u in cell["unset"]:
            run_env.pop(u, None)
        run_env.update(cell["env"])

        f_err = open(err_log, "w", encoding="utf-8")
        f_out = open(out_log, "w", encoding="utf-8")

        cmd = [exe, elf, "--renderer=d3d12"]
        t0 = time.time()
        p = subprocess.Popen(cmd, cwd=root, env=run_env, stdout=f_out, stderr=f_err)

        while time.time() - t0 < duration:
            if p.poll() is not None:
                break
            time.sleep(0.5)

        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()

        f_err.close()
        f_out.close()

        if os.path.exists(lock_file):
            try: os.remove(lock_file)
            except: pass

        # Parse metrics
        res = parse_run_log(err_log)
        res["cell"] = cell_key
        res["run"] = r
        cell_results.append(res)

        print(f"[{cell_key.upper()} Run {r:02d}/{runs_per_cell:02d}] Exit: {res['exit_method']:<14} | [3510]: {res['val_3510']} | brb_lr: {res['brb_lr']} | w8_pm: {res['w8_pm']:<2} | main_lr: {res['main_lr_60s']} | presents: {res['presents']}")
        sys.stdout.flush()

        # Update partial JSON
        partial_json = os.path.join(outdir, f"{cell_key}_results.json")
        with open(partial_json, "w", encoding="utf-8") as f:
            json.dump(cell_results, f, indent=2)

    return cell_results

def main():
    parser = argparse.ArgumentParser(description="Run 4-Cell Experiment Matrix")
    parser.add_argument("--cells", nargs="+", choices=["cell1", "cell2", "cell3", "cell4"], default=["cell1", "cell2", "cell3", "cell4"])
    parser.add_argument("--runs", type=int, default=20, help="Runs per cell (default: 20)")
    parser.add_argument("--duration", type=int, default=62, help="Duration in seconds per run (default: 62)")
    args = parser.parse_args()

    ensure_iso_mounted()
    cpu_info = get_cpu_info()
    print("=" * 70)
    print("GOW: ASCENSION 4-CELL EXPERIMENT MATRIX")
    print(f"Host CPU: {cpu_info['model']} ({cpu_info['physical_cores']} Cores, {cpu_info['logical_processors']} Threads, Hybrid: {cpu_info['hybrid']})")
    print(f"Target Cells: {args.cells}")
    print(f"Runs per Cell: {args.runs}, Duration: {args.duration}s")
    print("=" * 70)

    all_results = {}
    for c in args.cells:
        c_res = run_cell(c, runs_per_cell=args.runs, duration=args.duration)
        all_results[c] = c_res

    full_json = os.path.join("scratch", "matrix", "matrix_full_results.json")
    with open(full_json, "w", encoding="utf-8") as f:
        json.dump(all_results, f, indent=2)
    print(f"\nAll matrix runs completed. Saved to {full_json}")

if __name__ == "__main__":
    main()
