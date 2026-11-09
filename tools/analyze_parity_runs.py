import os
import re
import json
import sys

def read_file_safely(path):
    if not os.path.exists(path):
        return []
    with open(path, "rb") as f:
        raw = f.read()
    if raw.startswith(b"\xff\xfe") or raw.startswith(b"\xfe\xff"):
        try:
            return raw.decode("utf-16", errors="replace").splitlines(keepends=True)
        except Exception:
            pass
    try:
        return raw.decode("utf-8").splitlines(keepends=True)
    except UnicodeDecodeError:
        try:
            return raw.decode("utf-16", errors="replace").splitlines(keepends=True)
        except Exception:
            return raw.decode("latin-1", errors="replace").splitlines(keepends=True)

def parse_run_log(run_dir, run_num):
    pad = f"{run_num:02d}"
    out_file = f"run_{pad}_stdout.log"
    err_file = f"run_{pad}_stderr.log"
    out_path = os.path.join(run_dir, out_file)
    err_path = os.path.join(run_dir, err_file)

    if not os.path.exists(err_path):
        return None

    err_lines = read_file_safely(err_path)
    out_lines = read_file_safely(out_path)
    combined = err_lines + out_lines

    # NOSPACE_EXIT
    nospace = any("NOSPACE_EXIT" in l or "ContentErrorDialog: NOSPACE" in l for l in combined)

    # API calls
    vdec_calls = sum(1 for l in combined if "cellVdec" in l)
    sail_calls = sum(1 for l in combined if "cellSail" in l)
    audio_port_start = any("PortStart" in l or "cellAudioPortStart" in l for l in combined)

    # Crash
    crash = any("FATAL" in l or "Access Violation" in l or "Crash handler" in l or "Segmentation fault" in l or "[CRASH]" in l for l in combined)

    max_frame = 0
    max_groups = 0
    max_presents = 0
    wid8_pm_entries = 0
    wid8_jobs_done = 0
    wid10_pm_entries = 0
    wid10_jobs_done = 0

    re_livedraw = re.compile(r"\[live-draw\] frame (\d+).*?groups\[seen=\d+\s+exec=(\d+)")
    re_telem = re.compile(r"\[TELEMETRY 5s\] wall=([0-9.]+)s \| dl_head=\d+.*?presents=([0-9.]+)/s \[buf0=(\d+), buf1=(\d+)\](?: \| total_presents=(\d+))?")
    re_pres_qpc = re.compile(r"\[present-qpc\] preserved (\d+) successful presents")
    re_stage = re.compile(r"\[STAGE\]\s+t=(\d+)\s+main_lr=(0x[0-9A-Fa-f]+)\s+main_last_hle=([^\s]+)\s+live_draws=(\d+)\s+presents=(\d+)")
    re_boot_diag = re.compile(r"\[BOOT-DIAG @ (\d+)s\] lr=(0x[0-9A-Fa-f]+)")
    re_thread_audit = re.compile(r"=== \[TASK4-THREAD-AUDIT @ (\d+)s\] ===")
    re_thread_main = re.compile(r"Thread tid=1 \[main\]:.*?lr=(0x[0-9A-Fa-f]+)")
    re_wid8 = re.compile(r"Workload 'BrBDblBufWrkld' \(wid=8.*?\):.*?pm_entries=(\d+)")
    re_wid_audit = re.compile(r"wid=(\d+).*?pm_entries=(\d+).*?jobs_completed=(\d+)")
    re_main_poll = re.compile(r"\[MAIN-POLL-00466E28\] mgr_ea=0x([0-9A-Fa-f]+) desc_base=0x([0-9A-Fa-f]+) hdr=0x([0-9A-Fa-f]+) target_val=(\d+) min_val=(\d+)")
    re_desc_row = re.compile(r"\+0x70:\s+([0-9A-Fa-f]{4})([0-9A-Fa-f]{4})\s+([0-9A-Fa-f]{4})([0-9A-Fa-f]{4})\s+([0-9A-Fa-f]{4})([0-9A-Fa-f]{4})\s+([0-9A-Fa-f]{4})([0-9A-Fa-f]{4})")

    main_lr = None
    time_series_gp = {}
    time_series_lr = {}
    cur_audit_sec = None

    q3_target = None
    q3_counters = None
    in_desc_base = False

    for line in err_lines:
        m_ld = re_livedraw.search(line)
        if m_ld:
            frame = int(m_ld.group(1))
            groups = int(m_ld.group(2))
            if frame > max_frame:
                max_frame = frame
            if groups > max_groups:
                max_groups = groups

        m_tel = re_telem.search(line)
        if m_tel:
            w_sec = int(round(float(m_tel.group(1))))
            if m_tel.group(4):
                pres = int(m_tel.group(4))
            else:
                pres = int(m_tel.group(2)) + int(m_tel.group(3))
            if pres > max_presents:
                max_presents = pres
            time_series_gp[w_sec] = (max_groups, pres)

        m_qpc = re_pres_qpc.search(line)
        if m_qpc:
            qpc_pres = int(m_qpc.group(1))
            if qpc_pres > max_presents:
                max_presents = qpc_pres

        m_stg = re_stage.search(line)
        if m_stg:
            s_sec = int(m_stg.group(1))
            s_lr = m_stg.group(2).upper()
            s_pres = int(m_stg.group(5))
            if s_pres > max_presents:
                max_presents = s_pres
            time_series_gp[s_sec] = (max_groups, s_pres)
            time_series_lr[s_sec] = s_lr
            main_lr = s_lr

        m_bd = re_boot_diag.search(line)
        if m_bd:
            b_sec = int(m_bd.group(1))
            b_lr = m_bd.group(2).upper()
            time_series_lr[b_sec] = b_lr
            main_lr = b_lr

        m_ta = re_thread_audit.search(line)
        if m_ta:
            cur_audit_sec = int(m_ta.group(1))

        if cur_audit_sec is not None:
            m_tm = re_thread_main.search(line)
            if m_tm:
                tm_lr = m_tm.group(1).upper()
                time_series_lr[cur_audit_sec] = tm_lr
                main_lr = tm_lr
                cur_audit_sec = None

        m_w8 = re_wid8.search(line)
        if m_w8:
            w8_cnt = int(m_w8.group(1))
            if w8_cnt > wid8_pm_entries:
                wid8_pm_entries = w8_cnt

        m_wa = re_wid_audit.search(line)
        if m_wa:
            wid = int(m_wa.group(1))
            pm = int(m_wa.group(2))
            jobs = int(m_wa.group(3))
            if wid == 8:
                if pm > wid8_pm_entries: wid8_pm_entries = pm
                if jobs > wid8_jobs_done: wid8_jobs_done = jobs
            elif wid == 10:
                if pm > wid10_pm_entries: wid10_pm_entries = pm
                if jobs > wid10_jobs_done: wid10_jobs_done = jobs

        m_poll = re_main_poll.search(line)
        if m_poll:
            q3_target = int(m_poll.group(4))

        if "[DESC-BASE @" in line:
            in_desc_base = True
        elif in_desc_base and ("[" in line and "+0x" not in line):
            in_desc_base = False

        if in_desc_base:
            m_drow = re_desc_row.search(line)
            if m_drow:
                # +0x70 is bset[3] (halfword 0=limit, halfword 1=claim, halfwords 2..7=L0..L5)
                # groups: 1=lim, 2=clm, 3=L0, 4=L1, 5=L2, 6=L3, 7=L4, 8=L5
                hw = [int(m_drow.group(k), 16) for k in range(1, 9)]
                q3_counters = hw

    def get_at_sec(ts_dict, target_sec, default_val):
        if not ts_dict:
            return default_val
        closest = None
        for s in sorted(ts_dict.keys()):
            if s <= target_sec:
                closest = ts_dict[s]
            else:
                break
        return closest if closest is not None else default_val

    lr_history = {
        10: get_at_sec(time_series_lr, 10, main_lr if main_lr else "-"),
        20: get_at_sec(time_series_lr, 20, main_lr if main_lr else "-"),
        30: get_at_sec(time_series_lr, 30, main_lr if main_lr else "-"),
        40: get_at_sec(time_series_lr, 40, main_lr if main_lr else "-"),
        50: get_at_sec(time_series_lr, 50, main_lr if main_lr else "-"),
        60: get_at_sec(time_series_lr, 60, main_lr if main_lr else "-"),
    }

    # Determine checkpoint reached
    if any("Santa Monica" in l or "SMS_LOGO" in l for l in combined) or max_frame >= 800:
        checkpoint = f"Santa Monica Studio Logo (frame {max_frame})"
    elif any("Sony Computer Entertainment" in l or "SCEA" in l for l in combined) or max_frame >= 200:
        checkpoint = f"Sony / Legal Splash (frame {max_frame})"
    elif audio_port_start or max_frame > 0 or max_presents > 0:
        checkpoint = f"Intro FMV / Audio Playing (frame {max_frame})"
    else:
        checkpoint = f"Early Init (frame {max_frame})"

    if not q3_counters:
        q3_counters = [0, 0, 0, 0, 0, 0, 0, 0]
    if q3_target is None:
        q3_target = 1

    return {
        "run": pad,
        "out_file": out_file,
        "err_file": err_file,
        "nospace": nospace,
        "vdec_calls": vdec_calls,
        "sail_calls": sail_calls,
        "audio_port_start": audio_port_start,
        "crash": crash,
        "checkpoint": checkpoint,
        "max_frame": max_frame,
        "max_groups": max_groups,
        "max_presents": max_presents,
        "wid8_pm_entries": wid8_pm_entries,
        "wid8_jobs_done": wid8_jobs_done,
        "wid10_pm_entries": wid10_pm_entries,
        "wid10_jobs_done": wid10_jobs_done,
        "q3_counters": q3_counters,
        "q3_target": q3_target,
        "main_lr": main_lr,
        "lr_history": lr_history
    }

def print_report(run_dir):
    print(f"\n==================== Parity Runs Analysis Report ({run_dir}) ====================")
    runs = []
    for i in range(1, 11):
        r = parse_run_log(run_dir, i)
        if r:
            runs.append(r)

    if not runs:
        print(f"No run logs found in {run_dir}!")
        return

    # Master Task 2 Table
    print(f"\n| Run | Log File | Main LR (10s..60s) | WID 8 (pm/job) | WID 10 (pm/job) | Queue-3 (+2..+E / tgt) | Groups | Presents | Checkpoint Reached |")
    print(f"|---|---|---|---|---|---|---|---|---|")
    for r in runs:
        lr_hist = f"{r['lr_history'][10]}->{r['lr_history'][20]}->{r['lr_history'][30]}->{r['lr_history'][40]}->{r['lr_history'][50]}->{r['lr_history'][60]}"
        w8 = f"{r['wid8_pm_entries']}/{r['wid8_jobs_done']}"
        w10 = f"{r['wid10_pm_entries']}/{r['wid10_jobs_done']}"
        # q3_counters: hw 1..7 are +2..+0xE (claim, L0..L5)
        q3_str = f"[{','.join(str(x) for x in r['q3_counters'][1:])} / {r['q3_target']}]"
        print(f"| Run {r['run']} | `{r['err_file']}` | `{lr_hist}` | `{w8}` | `{w10}` | `{q3_str}` | {r['max_groups']} | {r['max_presents']} | {r['checkpoint']} |")

    # Save summary json
    summary_path = os.path.join(run_dir, "summary.json")
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(runs, f, indent=2)
    print(f"\nDetailed summary JSON saved to: {summary_path}\n")

if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else "build/logs/task2_unhooked"
    print_report(d)
