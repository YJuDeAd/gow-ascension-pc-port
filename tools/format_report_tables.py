import json, glob, re

with open("logs/honest_20_runs/summary.json", "r") as f:
    runs = json.load(f)

print("### Task 2: 20 Cold Runs x 90s (Defaults Only, GOW_FAKE_DONE=0, GOW_7B1C_BYPASS=0)")
print()
print("| Run | Log File | Stage @ 90s | Reached 7B1C? | Exit Q3 Counters (L0..L6) | Lanes @ 30s | Yields (L0..L5) | Presents | Groups | Max Draws | First Draw |")
print("|:---:|:---|:---|:---:|:---|:---|:---|:---:|:---:|:---:|:---:|")

for i, r in enumerate(runs, 1):
    log_name = f"run_{i:02d}_err.log"
    stage = r.get("stage_name", "")
    t_reach = r.get("time_reached_7b1c")
    if r.get("reached_7b1c"):
        reached = f"Yes (t={t_reach}s)"
    elif "0X00467B1C" in stage or "0X0046700C" in stage:
        reached = "Yes (in wait)"
    else:
        reached = "No"
    q3 = r.get("q3_exit", "").replace("target=", "tgt=").replace("min_L=", "min=")
    lanes = r.get("lanes_30s", "")
    yields = ",".join(str(y) for y in r.get("yields_per_lane", []))
    pres = r.get("presents", 0)
    grp = r.get("groups_exec", 0)
    drw = r.get("max_live_draws", 0)
    fds = r.get("first_draw_s")
    f_drw = f"{fds}s" if fds else "None"
    print(f"| {i:02d} | `{log_name}` | `{stage}` | {reached} | `{q3}` | `{lanes}` | `{yields}` | {pres} | {grp} | {drw} | {f_drw} |")

print()
print("### pm_entries Breakdown for Workloads 8 to 18 Across 20 Runs")
print()
print("| Run | Log File | w8 | w9 | w10 | w11 | w12 | w13 | w14 | w15 | w16 | w17 | w18 |")
print("|:---:|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|")

for i, r in enumerate(runs, 1):
    log_name = f"run_{i:02d}_err.log"
    pm = r.get("pm_entries_8_18", {})
    cols = [str(pm.get(str(w), pm.get(w, 0))) for w in range(8, 19)]
    print(f"| {i:02d} | `{log_name}` | " + " | ".join(cols) + " |")
