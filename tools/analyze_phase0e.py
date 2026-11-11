#!/usr/bin/env python3
import json
import re
import os

with open("build/phase0e_results.json", "r", encoding="utf-8") as f:
    results = json.load(f)

print("=== RUN RESULTS OVERVIEW ===")
for r in results:
    idx = r["run_idx"]
    adv = "YES" if r["advanced"] else "NO"
    main_lr = r["main_lr_60"]
    v3510 = r["val_3510_60"]
    brb_lr = r["brb_lr_60"]
    w8_pm = r["wid8_pm_60"]
    pres = r["presents_60"]
    grp = r["groups_exec_max"]
    by_sec = r["main_lr_by_sec"]
    lr_10 = by_sec.get("10", by_sec.get(10, "-"))
    lr_20 = by_sec.get("20", by_sec.get(20, "-"))
    lr_30 = by_sec.get("30", by_sec.get(30, "-"))
    lr_40 = by_sec.get("40", by_sec.get(40, "-"))
    lr_50 = by_sec.get("50", by_sec.get(50, "-"))
    lr_60 = by_sec.get("60", by_sec.get(60, "-"))
    print(f"Run {idx:02d}: adv={adv} | main_lr 10s={lr_10} 20s={lr_20} 30s={lr_30} 40s={lr_40} 50s={lr_50} 60s={lr_60} | [0x747D3510]={v3510} brb_lr={brb_lr} w8_pm={w8_pm} pres={pres} grp={grp}")

print("\n=== WRITES ANALYSIS IN STALLING RUNS (Run 01-07, 10) ===")
# Look at Run 01 as representative stalling run
for r in results:
    if not r["advanced"]:
        print(f"\n--- Stalling Run {r['run_idx']:02d} ---")
        print("Transitions:")
        for t in r["transitions"]:
            print(f"  {t['raw']}")
        print("Watchea writes to 0x747D3510:")
        for w in r["watchea_writes"]:
            print(f"  {w['raw']}")
        print("WW writes to 0x747D3510 (last 5):")
        for w in r["ww_writes"][-5:]:
            print(f"  {w['raw']}")
        if r["run_idx"] in [1, 2]:
            continue
        else:
            break

print("\n=== WRITES ANALYSIS IN PASSING RUNS (Run 08, Run 09) ===")
for r in results:
    if r["advanced"]:
        print(f"\n--- Passing Run {r['run_idx']:02d} ---")
        print("Transitions:")
        for t in r["transitions"]:
            print(f"  {t['raw']}")
        print("Watchea writes to 0x747D3510:")
        for w in r["watchea_writes"]:
            print(f"  {w['raw']}")
        print("WW writes to 0x747D3510:")
        for w in r["ww_writes"]:
            if "0x747D3510" in w["raw"] or "0X747D3510" in w["raw"]:
                print(f"  {w['raw']}")

