import json
import os
import sys

def load_summary(path):
    if not os.path.exists(path):
        return None
    with open(path, 'r') as f:
        return json.load(f)

s_base = load_summary('build/logs/parity_10/summary.json')
s_post = load_summary('build/logs/parity_10_post/summary.json')
s_fixed = load_summary('build/logs/parity_10_fixed/summary.json')

datasets = []
if s_base: datasets.append(("Baseline (0198d37)", s_base))
if s_post: datasets.append(("Broken Post-Relift (439b7b4)", s_post))
if s_fixed: datasets.append(("Fixed Post-Relift (fixed)", s_fixed))

print("=== MILESTONE DISTRIBUTION COMPARISON ===")
def dist(dataset):
    counts = {}
    for r in dataset:
        m = r['milestone'].split('(')[0].strip()
        counts[m] = counts.get(m, 0) + 1
    return counts

all_keys = set()
for _, data in datasets:
    all_keys.update(dist(data).keys())

header = f"{'Milestone':<35}"
for name, _ in datasets:
    header += f" | {name:<26}"
print(header)
print("-" * len(header))

for k in sorted(all_keys):
    row = f"{k:<35}"
    for _, data in datasets:
        d = dist(data)
        cnt = d.get(k, 0)
        pct = (cnt * 100) // len(data)
        row += f" | {cnt:>2}/{len(data)} ({pct:>2}%)                 "[:29]
    print(row)

print("\n=== RUN-BY-RUN DETAILS ===")
num_runs = max(len(d) for _, d in datasets)
header = f"{'Run':<4}"
for name, _ in datasets:
    header += f" | {name[:12]+' Milest':<20} | {name[:12]+' G/P':<12}"
print(header)
print("-" * len(header))

for i in range(num_runs):
    row = f"{i+1:<4}"
    for _, data in datasets:
        if i < len(data):
            r = data[i]
            m = r['milestone'].split('(')[0].strip()[:18]
            gp = f"{r['max_groups']}/{r['max_presents']}"
            row += f" | {m:<20} | {gp:<12}"
        else:
            row += f" | {'N/A':<20} | {'N/A':<12}"
    print(row)

print("\n=== KEY METRICS ===")
for name, data in datasets:
    nospace = sum(1 for r in data if r['nospace'])
    audio = sum(1 for r in data if r['audio_port_start'])
    vdec = sum(r['vdec_calls'] for r in data)
    sail = sum(r['sail_calls'] for r in data)
    crash = sum(1 for r in data if r['crash'])
    zero_gp = sum(1 for r in data if r['max_groups'] == 0)
    avg_gp = sum(r['max_groups'] for r in data) / len(data)
    avg_pres = sum(r['max_presents'] for r in data) / len(data)
    print(f"{name}:")
    print(f"  NOSPACE_EXIT: {nospace}/{len(data)}")
    print(f"  cellAudioPortStart: {audio}/{len(data)}")
    print(f"  cellVdec calls: {vdec}")
    print(f"  cellSail calls: {sail}")
    print(f"  Crashes: {crash}/{len(data)}")
    print(f"  0-group runs: {zero_gp}/{len(data)}")
    print(f"  Mean max groups: {avg_gp:.1f}, Mean max presents: {avg_pres:.1f}")
