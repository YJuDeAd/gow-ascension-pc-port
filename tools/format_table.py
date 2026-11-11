#!/usr/bin/env python3
import json

with open("build/phase0e_results.json", "r", encoding="utf-8") as f:
    data = json.load(f)

print("| Run # | Log File | main lr @ 10s, 20s, 30s, 40s, 50s, 60s | [0x747D3510] @ 60s | brbDriver lr | wid 8 pm_entries | True Presents | Groups Exec | Advanced | Cumulative Advanced / Total |")
print("|:---:|:---|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|")

passing_so_far = 0
for d in data:
    adv = d["advanced"]
    if adv:
        passing_so_far += 1
    cum = f"{passing_so_far} / {d['run_idx']}"
    sec_map = d["main_lr_by_sec"]
    lrs = ", ".join([f"{sec_map.get(str(s), sec_map.get(s, '-'))}" for s in [10, 20, 30, 40, 50, 60]])
    print(f"| {d['run_idx']:02d} | `{d['stderr_path']}` | {lrs} | `{d['val_3510_60']}` | `{d['brb_lr_60']}` | {d['wid8_pm_60']} | {d['presents_60']} | {d['groups_exec_max']} | **{'YES' if adv else 'NO'}** | {cum} |")
