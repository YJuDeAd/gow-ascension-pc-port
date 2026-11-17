import os
import glob
import re

def parse_stable_log(path):
    with open(path, 'r', encoding='latin1') as f:
        content = f.read()

    # Main thread LR history at 10, 20, 30, ..., 90 s
    lrs = {}
    for m in re.finditer(r'\[TASK4-THREAD-AUDIT @ (\d+)s\].*?Thread tid=1 \[main\]: state=\w+.*?lr=(0x[0-9A-Fa-f]+)', content, re.DOTALL):
        sec = int(m.group(1))
        lr = m.group(2).upper()
        lrs[sec] = lr

    # If audit wasn't formatted like that, check boot-diag
    if not lrs:
        for m in re.finditer(r'\[BOOT-DIAG @ (\d+)s\] lr=(0x[0-9A-Fa-f]+)', content):
            sec = int(m.group(1))
            lr = m.group(2).upper()
            lrs[sec] = lr

    # Telemetry presents and flips
    telem_lines = re.findall(r'\[TELEMETRY 5s\].*', content)
    flips_per_sec = []
    pres_per_sec = []
    tot_presents = 0
    tot_flips = 0
    first_draw = "N/A"

    for tl in telem_lines:
        m = re.search(r'wall=(\d+\.\d+)s .* presents=([0-9\.]+)/s .*?flip_words=([0-9\.]+)/s .*?total_presents=(\d+)', tl)
        if not m:
            m = re.search(r'wall=(\d+\.\d+)s .* presents=([0-9\.]+)/s .* total_presents=(\d+)', tl)
        if m:
            wall = float(m.group(1))
            p = float(m.group(2))
            fl = float(m.group(3)) if len(m.groups()) >= 4 else 0.0
            tp = int(m.group(4)) if len(m.groups()) >= 4 else int(m.group(3))
            pres_per_sec.append(p)
            flips_per_sec.append(fl)
            tot_presents = tp
            if first_draw == "N/A" and tp > 0:
                first_draw = f"<{wall:.0f}s"

    # GCM put / get
    gcm_put_get = re.findall(r'\[gcm.*?put=0x([0-9A-Fa-f]+).*?get=0x([0-9A-Fa-f]+)', content, re.IGNORECASE)
    if not gcm_put_get:
        gcm_put_get = re.findall(r'put=(0x[0-9A-Fa-f]+).*?get=(0x[0-9A-Fa-f]+)', content, re.IGNORECASE)

    # Next wait
    last_lr = lrs.get(max(lrs.keys())) if lrs else "UNKNOWN"

    return {
        'path': path,
        'lrs': lrs,
        'last_lr': last_lr,
        'first_draw': first_draw,
        'tot_presents': tot_presents,
        'tot_flips': tot_flips,
        'avg_pres': sum(pres_per_sec)/len(pres_per_sec) if pres_per_sec else 0.0,
        'avg_flips': sum(flips_per_sec)/len(flips_per_sec) if flips_per_sec else 0.0,
        'gcm_put_get': gcm_put_get[-1] if gcm_put_get else ("N/A", "N/A"),
        'raw_telem_sample': telem_lines[:3]
    }

def main():
    stable_files = sorted(glob.glob('scratch/part3_stable_runs/Part3_Stable_run*.log'))
    fivemin_files = sorted(glob.glob('scratch/part3_stable_runs/Part3_5Min_run*.log'))

    print("==========================================================================================")
    print(" PART 4 VERIFICATION: 20 COLD x 90s RUNS")
    print("==========================================================================================")
    print(f"| Run | Last LR | First Draw | Presents/s | Flips/s | Tot Presents | Tot Flips | GCM (put/get) | LR @ 10s -> 30s -> 60s -> 90s |")
    print(f"|---|---|---|---|---|---|---|---|---|")

    counts = {}
    for idx, f in enumerate(stable_files, 1):
        r = parse_stable_log(f)
        counts[r['last_lr']] = counts.get(r['last_lr'], 0) + 1
        lr_seq = f"{r['lrs'].get(10, 'N/A')} -> {r['lrs'].get(30, 'N/A')} -> {r['lrs'].get(60, 'N/A')} -> {r['lrs'].get(90, 'N/A')}"
        gcm_str = f"{r['gcm_put_get'][0]}/{r['gcm_put_get'][1]}" if r['gcm_put_get'] != ("N/A", "N/A") else "N/A"
        print(f"| {idx:02d} | `{r['last_lr']}` | {r['first_draw']} | {r['avg_pres']:.1f} | {r['avg_flips']:.1f} | {r['tot_presents']} | {r['tot_flips']} | {gcm_str} | {lr_seq} |")

    print(f"\nFinal Distribution (20 runs): {counts}")

    print("\n==========================================================================================")
    print(" PART 4 VERIFICATION: 3 x 300s (5-MINUTE) RUNS")
    print("==========================================================================================")
    print(f"| Run | Last LR | First Draw | Presents/s | Flips/s | Tot Presents | Tot Flips | GCM (put/get) | LR @ 60s -> 120s -> 180s -> 300s |")
    print(f"|---|---|---|---|---|---|---|---|---|")
    for idx, f in enumerate(fivemin_files, 1):
        r = parse_stable_log(f)
        lr_seq = f"{r['lrs'].get(60, 'N/A')} -> {r['lrs'].get(120, 'N/A')} -> {r['lrs'].get(180, 'N/A')} -> {r['lrs'].get(300, 'N/A')}"
        gcm_str = f"{r['gcm_put_get'][0]}/{r['gcm_put_get'][1]}" if r['gcm_put_get'] != ("N/A", "N/A") else "N/A"
        print(f"| {idx:02d} | `{r['last_lr']}` | {r['first_draw']} | {r['avg_pres']:.1f} | {r['avg_flips']:.1f} | {r['tot_presents']} | {r['tot_flips']} | {gcm_str} | {lr_seq} |")

if __name__ == '__main__':
    main()
