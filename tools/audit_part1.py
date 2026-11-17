import os
import glob
import re

logs = sorted(glob.glob('scratch/part3_relift_runs/Part3_Relift_run*.log'))
print(f"Total log files found: {len(logs)}")

table_rows = []

for log_path in logs:
    run_name = os.path.basename(log_path)
    with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    # Match audits
    # Pattern: [TASK4-THREAD-AUDIT @ <sec>s] ... Thread tid=1 [main]: state=RUNNING entry=0x00010000 lr=<lr>
    audit_matches = re.findall(r'\[TASK4-THREAD-AUDIT @ (\d+)s\][\s\S]*?Thread tid=1 \[main\]:.*?lr=(0x[0-9A-Fa-f]+)', content)
    audit_dict = {int(s): lr for s, lr in audit_matches}

    # Match boot-diags
    boot_matches = re.findall(r'\[BOOT-DIAG @ (\d+)s\] lr=(0x[0-9A-Fa-f]+)', content)
    boot_dict = {int(s): lr for s, lr in boot_matches}

    # LRs at 10, 20, 30, 40, 50, 60, 70, 80, 90s
    lrs = []
    for s in [10, 20, 30, 40, 50, 60, 70, 80, 90]:
        if s in audit_dict:
            lrs.append(audit_dict[s])
        elif s in boot_dict:
            lrs.append(boot_dict[s])
        else:
            lrs.append("N/A")

    # Match telemetry lines
    telem_lines = re.findall(r'\[TELEMETRY 5s\] wall=.*', content)

    # First draw time
    # Check first telemetry where total_presents > 0 or presents > 0
    first_draw = "N/A"
    for tl in telem_lines:
        m = re.search(r'wall=(\d+\.\d+)s .* total_presents=(\d+)', tl)
        if m and int(m.group(2)) > 0:
            first_draw = f"<{m.group(1)}s"
            break

    # pm-end lines
    pm_ends = re.findall(r'\[pm-end\].*', content)

    # wid 8 pm_entries
    wid8_matches = re.findall(r'Workload \'BrBDblBufWrkld\' \(wid=8.*?\):.*?pm_entries=(\d+)', content)
    wid8_val = wid8_matches[-1] if wid8_matches else "N/A"

    # Total presents from last telemetry
    tot_pres = 0
    if telem_lines:
        m_last = re.search(r'total_presents=(\d+)', telem_lines[-1])
        if m_last:
            tot_pres = int(m_last.group(1))

    # Sample 3 raw telemetry lines: wall=5.0s, wall=15.0s, wall=30.0s (or last)
    sample_telem = []
    for target_w in ["wall=5.0s", "wall=15.0s", "wall=30.0s"]:
        for tl in telem_lines:
            if target_w in tl:
                sample_telem.append(tl)
                break
    if len(sample_telem) < 3 and telem_lines:
        sample_telem = telem_lines[:3]

    table_rows.append({
        "run": run_name,
        "lrs": lrs,
        "first_draw": first_draw,
        "telem_sample": sample_telem,
        "pm_end_count": len(pm_ends),
        "wid8_pm_entries": wid8_val,
        "total_presents": tot_pres
    })

for row in table_rows:
    print(f"{row['run']}:")
    print(f"  LRs [10-90s]: {', '.join(row['lrs'])}")
    print(f"  First Draw: {row['first_draw']} | Total Pres: {row['total_presents']} | wid8 pm_entries: {row['wid8_pm_entries']} | [pm-end] count: {row['pm_end_count']}")
    print("  Sample Telemetry (3 raw lines):")
    for st in row['telem_sample']:
        print(f"    {st}")
    print()
