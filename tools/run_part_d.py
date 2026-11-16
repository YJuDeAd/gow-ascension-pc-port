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
    metrics = {
        "log": os.path.basename(log_path),
        "advanced": False,
        "main_left_61b9fc": False,
        "last_main_lr": "N/A",
        "val_3510": "N/A",
        "w8_pm_entries": 0,
        "mfc_stall_seen": False,
        "mfc_resume_seen": False,
        "stall_followed_by_resume": False,
        "live_draws": 0,
        "presents": 0,
    }
    if not os.path.exists(log_path):
        return metrics

    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # 1. 0x747D3510
    m_3510 = re.findall(r'\[0x747D3510\]=0x([0-9A-Fa-f]+)', content)
    m_trans = re.findall(r'\[TRANSITION-0x747D3510\].*?new=0x([0-9A-Fa-f]+)', content)
    if m_trans:
        metrics["val_3510"] = f"0x{m_trans[-1]}"
    elif m_3510:
        metrics["val_3510"] = f"0x{m_3510[-1]}"

    # 2. Main lr
    m_stages = re.findall(r'\[STAGE\].*?main_lr=(0x[0-9A-Fa-f]+)', content)
    m_boot = re.findall(r'\[BOOT-DIAG\s+@\s+\d+s\]\s+lr=(0x[0-9A-Fa-f]+)', content)
    m_trans_mlr = re.findall(r'\[TRANSITION-0x747D3510\].*?main_lr=(0x[0-9A-Fa-f]+)', content)
    if m_stages:
        metrics["last_main_lr"] = m_stages[-1]
    elif m_boot:
        metrics["last_main_lr"] = m_boot[-1]
    elif m_trans_mlr:
        metrics["last_main_lr"] = m_trans_mlr[-1]
    
    if metrics["last_main_lr"] != "N/A":
        metrics["main_left_61b9fc"] = (metrics["last_main_lr"].upper() != "0X0061B9FC" and metrics["last_main_lr"].upper() != "0X00000000")

    # 3. wid 8 pm_entries
    m_w8 = re.findall(r'Workload\s+.*wid=8.*?pm_entries=(\d+)', content)
    if m_w8:
        metrics["w8_pm_entries"] = int(m_w8[-1])
    else:
        m_diag_w8 = re.findall(r'w8_pm=(\d+)', content)
        if m_diag_w8:
            metrics["w8_pm_entries"] = int(m_diag_w8[-1])

    # 4. MFC list stall and resume
    stall_idx = content.find("[mfc-list] STALL")
    resume_idx = content.find("[mfc-list] RESUME")
    if stall_idx != -1:
        metrics["mfc_stall_seen"] = True
    if resume_idx != -1:
        metrics["mfc_resume_seen"] = True
    if stall_idx != -1 and resume_idx != -1 and resume_idx > stall_idx:
        metrics["stall_followed_by_resume"] = True

    # 5. Live draw and presents
    m_draws = re.findall(r'\[live-draw\] frame (\d+)', content)
    if m_draws:
        metrics["live_draws"] = int(m_draws[-1])
    m_pres = re.findall(r'total_presents=(\d+)', content)
    if m_pres:
        metrics["presents"] = int(m_pres[-1])

    # 6. Advanced
    # Advanced = main leaves 0x0061B9FC before 60 s AND [0x747D3510] == 0
    if metrics["main_left_61b9fc"] and metrics["val_3510"].upper() in ("0X00000000", "0X0"):
        metrics["advanced"] = True

    return metrics

def ensure_iso_mounted():
    psarc = r"E:\PS3_GAME\USRDIR\GAME.PSARC"
    if not os.path.exists(psarc):
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

def run_experiment(args):
    ensure_iso_mounted()
    root = os.path.abspath(".")
    lock_file = os.path.join(root, "build", "run.lock")
    exe = os.path.join(root, "build", "gow_ascension.exe")
    elf = os.path.join(root, "vfs", "PS3_GAME", "USRDIR", "EBOOT.ELF")
    outdir = os.path.join(root, args.outdir)
    os.makedirs(outdir, exist_ok=True)

    extra_env = {}
    if args.env:
        for item in args.env:
            if "=" in item:
                k, v = item.split("=", 1)
                extra_env[k.strip()] = v.strip()

    print("=" * 60)
    print(f"EXPERIMENT: {args.prefix}")
    print(f"Runs: {args.runs}, Duration: {args.duration}s per run")
    print(f"Environment Overrides: {extra_env}")
    print("=" * 60)

    results = []

    for r in range(1, args.runs + 1):
        run_id = f"{r:02d}"
        err_log = os.path.join(outdir, f"{args.prefix}_run{run_id}_stderr.log")
        out_log = os.path.join(outdir, f"{args.prefix}_run{run_id}_stdout.log")

        print(f"\n--- Run {r}/{args.runs} ({args.prefix}) ---")

        # Acquire lock
        if os.path.exists(lock_file):
            try: os.remove(lock_file)
            except: pass
        with open(lock_file, "w") as f:
            f.write(f"PID={os.getpid()}, Run={r}, Prefix={args.prefix}, Start={time.time()}\n")

        # Cold clean
        clean_fios_cache(root)

        if os.path.exists(err_log): os.remove(err_log)
        if os.path.exists(out_log): os.remove(out_log)

        run_env = os.environ.copy()
        run_env.update(extra_env)

        f_err = open(err_log, "w", encoding="utf-8")
        f_out = open(out_log, "w", encoding="utf-8")

        cmd = [exe, elf, "--renderer=d3d12"]
        p = subprocess.Popen(cmd, cwd=root, env=run_env, stdout=f_out, stderr=f_err)

        start_t = time.time()
        while time.time() - start_t < args.duration:
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

        # Release lock
        if os.path.exists(lock_file):
            try: os.remove(lock_file)
            except: pass

        m = parse_run_log(err_log)
        m["run"] = r
        results.append(m)

        print(f"Run {r}: Advanced={m['advanced']} | Main Left={m['main_left_61b9fc']} (lr={m['last_main_lr']}) | [0x747D3510]={m['val_3510']} | w8_pm={m['w8_pm_entries']} | STALL->RESUME={m['stall_followed_by_resume']}")

    # Print summary table
    print("\n" + "=" * 80)
    print(f"RESULTS SUMMARY: {args.prefix}")
    print("=" * 80)
    print("| Run | Advanced | Main Left 0x0061B9FC | Last Main lr | [0x747D3510] | wid 8 pm | STALL -> RESUME | LiveDraw |")
    print("|:---:|:--------:|:--------------------:|:------------:|:------------:|:--------:|:---------------:|:--------:|")
    for res in results:
        adv_str = "**YES**" if res["advanced"] else "NO"
        left_str = "YES" if res["main_left_61b9fc"] else "NO"
        sr_str = "YES" if res["stall_followed_by_resume"] else "NO"
        print(f"| {res['run']:02d} | {adv_str} | {left_str} | `{res['last_main_lr']}` | `{res['val_3510']}` | {res['w8_pm_entries']} | {sr_str} | {res['live_draws']} |")

    adv_count = sum(1 for res in results if res["advanced"])
    print(f"\nAdvanced: {adv_count} / {args.runs} ({adv_count / args.runs * 100:.1f}%)")

    # Save JSON
    json_path = os.path.join(outdir, f"{args.prefix}_results.json")
    with open(json_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"Saved results to {json_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument("--prefix", type=str, default="test")
    parser.add_argument("--outdir", type=str, default="scratch/part_d")
    parser.add_argument("--env", action="append")
    args = parser.parse_args()
    run_experiment(args)
