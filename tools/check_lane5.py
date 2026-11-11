#!/usr/bin/env python3
for run_id in ["08", "09"]:
    print(f"=== Lane 5 history in Run {run_id} ===")
    with open(f"build/logs/phase0e_run_{run_id}.stderr.log", "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "Lane 5:" in line or "wid 8" in line or "wid=8" in line:
                print(line.strip())
