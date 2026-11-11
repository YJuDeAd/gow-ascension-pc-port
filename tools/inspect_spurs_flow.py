#!/usr/bin/env python3
import re

def inspect_spurs(path, label):
    print(f"\n=======================================================")
    print(f"=== {label}: {path} ===")
    print(f"=======================================================")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    print("--- SPURS calls (AddWorkload, RequestIdleSpu, SetWorkloadPriority) ---")
    for line in lines:
        if any(k in line for k in ["cellSpursAddWorkload", "cellSpursRequestIdleSpu", "cellSpursSetWorkloadPriority", "cellSpursReadyCount"]):
            print(line.strip())

    print("\n--- Workload 8 status reports ---")
    for line in lines:
        if "wid=8" in line or "BrBDblBufWrkld" in line:
            print(line.strip())

    print("\n--- Lane audits at 5s, 10s, 15s ---")
    in_lane_audit = False
    for line in lines:
        if "=== [ALL-LANES-AUDIT" in line:
            in_lane_audit = True
            print(line.strip())
        elif in_lane_audit:
            if "==============================" in line:
                in_lane_audit = False
                print(line.strip())
            else:
                print(line.strip())

inspect_spurs("build/logs/phase0e_run_01.stderr.log", "STALLING RUN 01")
inspect_spurs("build/logs/phase0e_run_08.stderr.log", "PASSING RUN 08")
inspect_spurs("build/logs/phase0e_run_09.stderr.log", "PASSING RUN 09")
