#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import re
import json

LOCK_FILE = os.path.join("build", "run.lock")
EXE_PATH = os.path.join("build", "gow_ascension.exe")
LOGS_DIR = os.path.join("build", "logs")
CACHE_FILE = os.path.join("vfs", "dev_hdd0", "game", "BCAS25016", "cache.idx")
TARGET_ELF = os.path.join("vfs", "PS3_GAME", "USRDIR", "EBOOT.ELF")

def clean_fios_cache():
    if os.path.exists(CACHE_FILE):
        try:
            with open(CACHE_FILE, "rb") as f:
                data = f.read()
            if len(data) > 0 and all(b == 0 for b in data):
                print(f"[FIOS-CLEAN] Removing zero-filled {CACHE_FILE}")
                os.remove(CACHE_FILE)
        except Exception as e:
            print(f"[FIOS-CLEAN] Error checking {CACHE_FILE}: {e}")

def acquire_lock():
    while os.path.exists(LOCK_FILE):
        print(f"Waiting for {LOCK_FILE} to clear...")
        time.sleep(1)
    os.makedirs("build", exist_ok=True)
    with open(LOCK_FILE, "w") as f:
        f.write(str(os.getpid()))

def release_lock():
    if os.path.exists(LOCK_FILE):
        try:
            os.remove(LOCK_FILE)
        except Exception as e:
            print(f"Error removing {LOCK_FILE}: {e}")

def kill_stale_processes():
    try:
        subprocess.run(["taskkill", "/F", "/IM", "gow_ascension.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass

def run_single(run_idx, duration_sec=62):
    clean_fios_cache()
    kill_stale_processes()

    os.makedirs(LOGS_DIR, exist_ok=True)
    stderr_path = os.path.join(LOGS_DIR, f"phase0e_run_{run_idx:02d}.stderr.log")
    stdout_path = os.path.join(LOGS_DIR, f"phase0e_run_{run_idx:02d}.stdout.log")

    if os.path.exists(stderr_path):
        os.remove(stderr_path)
    if os.path.exists(stdout_path):
        os.remove(stdout_path)

    env = os.environ.copy()
    env["PPU_WWATCH"] = "0x747D3510"
    env["PPU_WWATCH_MAX"] = "0"
    env["SPU_WATCHEA"] = "0x747D3510"
    env["SPU_WATCHEA_EVERY"] = "1"
    env["SPURS_MAXCONT"] = "3=5,13=3,15=5,16=5,17=5"

    print(f"\n=======================================================")
    print(f"Starting Run {run_idx:02d} (duration {duration_sec}s)...")
    print(f"  stderr: {stderr_path}")
    print(f"=======================================================")

    with open(stdout_path, "w", encoding="utf-8", errors="replace") as out_f, \
         open(stderr_path, "w", encoding="utf-8", errors="replace") as err_f:
        p = subprocess.Popen([EXE_PATH, TARGET_ELF], env=env, stdout=out_f, stderr=err_f)
        try:
            p.wait(timeout=duration_sec)
        except subprocess.TimeoutExpired:
            print(f"Run {run_idx:02d} reached {duration_sec}s. Terminating process...")
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                p.wait(timeout=5)
            except Exception:
                pass

    time.sleep(1)
    return stderr_path, stdout_path

def parse_run(run_idx, stderr_path):
    if not os.path.exists(stderr_path):
        return None

    with open(stderr_path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    main_lr_by_sec = {}
    brb_lr_by_sec = {}
    val_3510_by_sec = {}
    wid8_pm_by_sec = {}
    total_presents_by_sec = {}

    ww_writes = []
    watchea_writes = []
    transitions = []
    wid8_events = []
    spurs_calls = []
    groups_exec_max = 0

    re_audit = re.compile(r"=== \[TASK4-THREAD-AUDIT @ (\d+)s\] ===")
    re_main_thread = re.compile(r"Thread tid=1 \[main\]:.*lr=(0x[0-9A-Fa-f]+)")
    re_brb_thread = re.compile(r"\[THREAD9-brbDriver\] \(tid=9\): lr=(0x[0-9A-Fa-f]+).*\[0x747D3510\]=(0x[0-9A-Fa-f]+)")
    re_telemetry = re.compile(r"\[TELEMETRY (\d+)s\] wall=([0-9.]+)s.*total_presents=(\d+)")
    re_wid_audit = re.compile(r"\[(\d+)\]\s+wid=(\d+)\s+\(([^)]+)\)\s+state=\d+\s+pri=\d+\s+pm_entries=(\d+)")
    re_ww = re.compile(r"\[ww\]\s+t=(\d+)us\s+(0x[0-9A-Fa-f]+)\s+<-\s+(0x[0-9A-Fa-f]+)\s+\(w\d+\)\s+guest-fn=(0x[0-9A-Fa-f]+)\s+lr=(0x[0-9A-Fa-f]+)\s+tid=(\d+)")
    re_watchea = re.compile(r"\[watchea\]\s+n=\d+\s+spu(\d+)\s+cmd=(0x[0-9A-Fa-f]+)\s+ea=(0x[0-9A-Fa-f]+)\s+size=(\d+)\s+lsa=(0x[0-9A-Fa-f]+)\s+pc=(0x[0-9A-Fa-f]+)\s+->\s+\[(0x[0-9A-Fa-f]+)\]=(0x[0-9A-Fa-f]+)")
    re_trans = re.compile(r"\[TRANSITION-0x747D3510\]\s+t=(\d+)us\s+old=(0x[0-9A-Fa-f]+)\s+new=(0x[0-9A-Fa-f]+)\s+\|\s+main_lr=(0x[0-9A-Fa-f]+)\s+brb_lr=(0x[0-9A-Fa-f]+)\s+w8_pm=(\d+)")
    re_livedraw = re.compile(r"\[live-draw\]\s+frame\s+\d+:\s+groups exec\s+(\d+)")
    re_spurs_call = re.compile(r"\[SPURS-CALL\]\s+t=([0-9.]+)s\s+tid=(\d+)\s+([A-Za-z0-9_]+)\(0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\)")

    cur_audit_sec = None
    for line in lines:
        m_aud = re_audit.search(line)
        if m_aud:
            cur_audit_sec = int(m_aud.group(1))

        if cur_audit_sec is not None:
            m_main = re_main_thread.search(line)
            if m_main:
                main_lr_by_sec[cur_audit_sec] = m_main.group(1).upper()
                cur_audit_sec = None

        m_brb = re_brb_thread.search(line)
        if m_brb:
            brb_lr = m_brb.group(1).upper()
            val_3510 = m_brb.group(2).upper()
            sec_idx = len(brb_lr_by_sec) * 5 + 5
            brb_lr_by_sec[sec_idx] = brb_lr
            val_3510_by_sec[sec_idx] = val_3510

        m_tel = re_telemetry.search(line)
        if m_tel:
            sec_val = int(round(float(m_tel.group(2))))
            total_presents_by_sec[sec_val] = int(m_tel.group(3))

        m_wid = re_wid_audit.search(line)
        if m_wid and int(m_wid.group(2)) == 8:
            sec_idx = len(wid8_pm_by_sec) * 10 + 10
            wid8_pm_by_sec[sec_idx] = int(m_wid.group(4))

        m_ww_m = re_ww.search(line)
        if m_ww_m:
            ww_writes.append({
                "t_us": int(m_ww_m.group(1)),
                "ea": m_ww_m.group(2).upper(),
                "val": m_ww_m.group(3).upper(),
                "guest_fn": m_ww_m.group(4).upper(),
                "lr": m_ww_m.group(5).upper(),
                "tid": int(m_ww_m.group(6)),
                "raw": line.strip()
            })

        m_watchea_m = re_watchea.search(line)
        if m_watchea_m:
            watchea_writes.append({
                "spu": int(m_watchea_m.group(1)),
                "cmd": m_watchea_m.group(2).upper(),
                "ea": m_watchea_m.group(3).upper(),
                "size": int(m_watchea_m.group(4)),
                "lsa": m_watchea_m.group(5).upper(),
                "pc": m_watchea_m.group(6).upper(),
                "target_ea": m_watchea_m.group(7).upper(),
                "val": m_watchea_m.group(8).upper(),
                "raw": line.strip()
            })

        m_trans_m = re_trans.search(line)
        if m_trans_m:
            transitions.append({
                "t_us": int(m_trans_m.group(1)),
                "old": m_trans_m.group(2).upper(),
                "new": m_trans_m.group(3).upper(),
                "main_lr": m_trans_m.group(4).upper(),
                "brb_lr": m_trans_m.group(5).upper(),
                "w8_pm": int(m_trans_m.group(6)),
                "raw": line.strip()
            })

        m_ld = re_livedraw.search(line)
        if m_ld:
            groups_exec_max = max(groups_exec_max, int(m_ld.group(1)))

        m_call = re_spurs_call.search(line)
        if m_call:
            spurs_calls.append(line.strip())

        if "wid 8" in line or "wid=8" in line or "BrBDblBufWrkld" in line:
            wid8_events.append(line.strip())

    # 60s values
    main_lr_60 = main_lr_by_sec.get(60, main_lr_by_sec.get(max(main_lr_by_sec.keys()) if main_lr_by_sec else 0, "UNKNOWN"))

    # 0x747D3510 at 60s
    if transitions:
        val_3510_60 = transitions[-1]["new"]
    elif val_3510_by_sec:
        max_sec = max(val_3510_by_sec.keys())
        val_3510_60 = val_3510_by_sec[max_sec]
    else:
        val_3510_60 = "UNKNOWN"

    brb_lr_60 = brb_lr_by_sec.get(max(brb_lr_by_sec.keys()) if brb_lr_by_sec else 0, "UNKNOWN")
    wid8_pm_60 = wid8_pm_by_sec.get(max(wid8_pm_by_sec.keys()) if wid8_pm_by_sec else 0, 0)
    presents_60 = total_presents_by_sec.get(max(total_presents_by_sec.keys()) if total_presents_by_sec else 0, 0)

    # Pass metric: "advanced" = main lr is NOT 0x0061B9FC at 60 s AND [0x747D3510] == 0
    is_val_zero = (val_3510_60 in ["0x00000000", "0X00000000", "0x0", "0X0", "0"])
    advanced = (main_lr_60 != "0x0061B9FC" and main_lr_60 != "0X0061B9FC" and main_lr_60 != "UNKNOWN" and is_val_zero)

    # Find who wrote the first -1
    first_neg1_writer = None
    for tr in transitions:
        if tr["new"] in ["0xFFFFFFFF", "0XFFFFFFFF"]:
            first_neg1_writer = tr
            break

    first_neg1_dma = None
    for dma in watchea_writes:
        if dma["val"] in ["0xFFFFFFFF", "0XFFFFFFFF"] and dma["cmd"] in ["0x20", "0X20"]:
            first_neg1_dma = dma
            break

    # Find who wrote the first 0
    first_zero_ww = None
    for ww in ww_writes:
        if ww["ea"] in ["0x747D3510", "0X747D3510"] and ww["val"] in ["0x0", "0X0", "0x00000000", "0X00000000"]:
            first_zero_ww = ww
            break

    first_zero_dma = None
    for dma in watchea_writes:
        if dma["target_ea"] in ["0x747D3510", "0X747D3510"] and dma["val"] in ["0x0", "0X0", "0x00000000", "0X00000000"] and dma["cmd"] in ["0x20", "0X20"]:
            first_zero_dma = dma
            break

    return {
        "run_idx": run_idx,
        "stderr_path": stderr_path,
        "main_lr_by_sec": main_lr_by_sec,
        "main_lr_60": main_lr_60,
        "val_3510_60": val_3510_60,
        "brb_lr_60": brb_lr_60,
        "wid8_pm_60": wid8_pm_60,
        "presents_60": presents_60,
        "groups_exec_max": groups_exec_max,
        "advanced": advanced,
        "ww_writes": ww_writes,
        "watchea_writes": watchea_writes,
        "transitions": transitions,
        "wid8_events": wid8_events,
        "spurs_calls": spurs_calls,
        "first_neg1_writer": first_neg1_writer,
        "first_neg1_dma": first_neg1_dma,
        "first_zero_ww": first_zero_ww,
        "first_zero_dma": first_zero_dma
    }

def main():
    acquire_lock()
    try:
        results = []
        passing_count = 0
        stalling_count = 0
        max_runs = 40

        for run_idx in range(1, max_runs + 1):
            stderr_path, stdout_path = run_single(run_idx, duration_sec=62)
            res = parse_run(run_idx, stderr_path)
            if res:
                results.append(res)
                if res["advanced"]:
                    passing_count += 1
                else:
                    stalling_count += 1

                print(f"--> Run {run_idx:02d} Result: main_lr@60s={res['main_lr_60']} [0x747D3510]={res['val_3510_60']} "
                      f"brb_lr={res['brb_lr_60']} wid8_pm={res['wid8_pm_60']} presents={res['presents_60']} "
                      f"groups={res['groups_exec_max']} advanced={'YES' if res['advanced'] else 'NO'} "
                      f"(Passing: {passing_count}, Stalling: {stalling_count}, Cumulative: {passing_count}/{len(results)})")

            # Check stopping criteria
            if run_idx >= 10:
                if passing_count >= 2 and stalling_count >= 4:
                    print(f"\nCondition met at Run {run_idx}: passing={passing_count} >= 2, stalling={stalling_count} >= 4.")
                    break
                elif run_idx == max_runs:
                    print(f"\nReached max runs {max_runs}. Stopping.")
                    break

        # Save summary JSON
        with open("build/phase0e_results.json", "w", encoding="utf-8") as f:
            json.dump([{k: v for k, v in r.items() if k not in ["wid8_events", "spurs_calls"]} for r in results], f, indent=2)

        print("\nAll runs complete! Results saved to build/phase0e_results.json.")

    finally:
        release_lock()

if __name__ == "__main__":
    main()
