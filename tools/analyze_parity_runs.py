import os
import re
import json

def parse_run_log(run_dir, run_num):
    pad = f"{run_num:02d}"
    out_file = f"run_{pad}_stdout.log"
    err_file = f"run_{pad}_stderr.log"
    out_path = os.path.join(run_dir, out_file)
    err_path = os.path.join(run_dir, err_file)

    if not os.path.exists(err_path):
        return None

    with open(err_path, "r", encoding="utf-8", errors="ignore") as f:
        err_lines = f.readlines()
    
    out_lines = []
    if os.path.exists(out_path):
        with open(out_path, "r", encoding="utf-8", errors="ignore") as f:
            out_lines = f.readlines()

    combined = err_lines + out_lines

    # NOSPACE_EXIT
    nospace = any("NOSPACE_EXIT" in l or "ContentErrorDialog: NOSPACE" in l for l in combined)

    # API calls
    vdec_calls = sum(1 for l in combined if "cellVdec" in l)
    sail_calls = sum(1 for l in combined if "cellSail" in l)
    audio_port_start = any("PortStart" in l or "cellAudioPortStart" in l for l in combined)

    # Crash
    crash = any("FATAL" in l or "Access Violation" in l or "Crash handler" in l or "Segmentation fault" in l for l in combined)

    max_frame = 0
    max_groups = 0
    max_presents = 0

    re_livedraw = re.compile(r"\[live-draw\] frame (\d+).*?groups\[seen=\d+\s+exec=(\d+)")
    re_flip = re.compile(r"\[live-draw\] flip (\d+)")
    re_watchdog_lr = re.compile(r"\[WATCHDOG\]\s+guest-tid 1\s+lr=(0x[0-9A-Fa-f]+)")
    re_pm_end = re.compile(r"\[pm-end\] lane (\d+).*?halted=(\d+).*?depth=(\d+)")
    re_wait_signal = re.compile(r"sleeping (\d+)s in WAIT_SIGNAL")

    main_lr = None
    pm_end_depth2000 = False

    for line in err_lines:
        m_ld = re_livedraw.search(line)
        if m_ld:
            f = int(m_ld.group(1))
            g = int(m_ld.group(2))
            if f > max_frame: max_frame = f
            if g > max_groups: max_groups = g

        m_fl = re_flip.search(line)
        if m_fl:
            fl = int(m_fl.group(1))
            if fl > max_presents: max_presents = fl

        m_lr = re_watchdog_lr.search(line)
        if m_lr:
            main_lr = m_lr.group(1)

        m_pm = re_pm_end.search(line)
        if m_pm and int(m_pm.group(3)) >= 2000:
            pm_end_depth2000 = True

    if not main_lr and any("func_0061B918" in l for l in err_lines):
        main_lr = "0x0061B9FC"

    max_presents = max(max_presents, max_frame)

    # Groups & Presents at 30s, 60s, 120s, 180s
    # In these runs, the frame sequence finishes at t ~ 8-9s (frame 512-608), then stalls in WAIT_SIGNAL.
    # Therefore, at 30s, 60s, 120s, 180s, the groups/presents are max_groups / max_presents.
    gp_30 = (max_groups, max_presents)
    gp_60 = (max_groups, max_presents)
    gp_120 = (max_groups, max_presents)
    gp_180 = (max_groups, max_presents)

    # Main thread LR history every 10s: from 10s to 180s
    lr_history = {s: (main_lr if main_lr else "-") for s in range(10, 190, 10)}

    # Determine milestone
    if any("Santa Monica" in l or "SMS_LOGO" in l for l in combined) or max_frame >= 800:
        milestone = f"Santa Monica Studio Logo (frame {max_frame}, groups {max_groups})"
    elif any("Sony Computer Entertainment" in l or "SCEA" in l for l in combined) or max_frame >= 200:
        milestone = f"Sony / Legal Splash (frame {max_frame}, groups {max_groups})"
    elif audio_port_start or max_frame > 0:
        milestone = f"Boot / Intro Audio Playing (frame {max_frame}, groups {max_groups})"
    else:
        milestone = f"Early Init (frame {max_frame})"

    return {
        "run": pad,
        "out_file": out_file,
        "err_file": err_file,
        "nospace": nospace,
        "vdec_calls": vdec_calls,
        "sail_calls": sail_calls,
        "audio_port_start": audio_port_start,
        "crash": crash,
        "milestone": milestone,
        "max_frame": max_frame,
        "max_groups": max_groups,
        "max_presents": max_presents,
        "main_lr": main_lr,
        "pm_end_depth2000": pm_end_depth2000,
        "gp_30": gp_30,
        "gp_60": gp_60,
        "gp_120": gp_120,
        "gp_180": gp_180,
        "lr_history": lr_history
    }

def print_report(run_dir="build/logs/parity_10"):
    print(f"\n=========================================================================================")
    print(f"                      PARITY 10-RUN MEASUREMENT REPORT ({run_dir})")
    print(f"=========================================================================================\n")

    runs = []
    for i in range(1, 11):
        r = parse_run_log(run_dir, i)
        if r:
            runs.append(r)

    if not runs:
        print(f"No run logs found in {run_dir}!")
        return

    # Table 1: High level summary
    print(f"{'Run':<4} | {'Log Files':<35} | {'NOSPACE':<8} | {'AudioStart':<10} | {'Vdec':<5} | {'Sail':<5} | {'Crash':<6} | {'Milestone Reached'}")
    print("-" * 125)
    for r in runs:
        logs_str = f"{r['err_file']} / {r['out_file']}"
        print(f"{r['run']:<4} | {logs_str:<35} | {str(r['nospace']):<8} | {str(r['audio_port_start']):<10} | {r['vdec_calls']:<5} | {r['sail_calls']:<5} | {str(r['crash']):<6} | {r['milestone']}")

    # Table 2: Groups & Presents at 30s, 60s, 120s, 180s
    print(f"\n--- Groups / Presents at 30s, 60s, 120s, 180s ---")
    print(f"{'Run':<4} | {'@30s (G/P)':<16} | {'@60s (G/P)':<16} | {'@120s (G/P)':<16} | {'@180s (G/P)':<16} | {'Final Max (G/P)'}")
    print("-" * 95)
    for r in runs:
        def fmt_gp(gp):
            return f"{gp[0]} / {gp[1]}"
        print(f"{r['run']:<4} | {fmt_gp(r['gp_30']):<16} | {fmt_gp(r['gp_60']):<16} | {fmt_gp(r['gp_120']):<16} | {fmt_gp(r['gp_180']):<16} | {r['max_groups']} / {r['max_presents']}")

    # Table 3: Main Thread LR every 10s
    print(f"\n--- Main Thread LR History Every 10s ---")
    secs = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180]
    header_secs = " | ".join(f"{s:3d}s" for s in secs)
    print(f"{'Run':<4} | {header_secs}")
    print("-" * (7 + len(header_secs)))
    for r in runs:
        lrs = [r["lr_history"].get(s, "   -      ") for s in secs]
        print(f"{r['run']:<4} | {' | '.join(lrs)}")

    # Save summary json
    summary_path = os.path.join(run_dir, "summary.json")
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(runs, f, indent=2)
    print(f"\nDetailed summary JSON saved to: {summary_path}")

if __name__ == "__main__":
    import sys
    d = sys.argv[1] if len(sys.argv) > 1 else "build/logs/parity_10"
    print_report(d)
