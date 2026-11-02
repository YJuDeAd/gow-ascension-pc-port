import json

with open('build/logs/parity_10/summary.json', 'r') as f:
    pre = json.load(f)
with open('build/logs/parity_10_post/summary.json', 'r') as f:
    post = json.load(f)

print("=== MILESTONE DISTRIBUTION COMPARISON ===")
def dist(dataset):
    counts = {}
    for r in dataset:
        m = r['milestone'].split('(')[0].strip()
        counts[m] = counts.get(m, 0) + 1
    return counts

pre_dist = dist(pre)
post_dist = dist(post)
all_keys = set(pre_dist.keys()) | set(post_dist.keys())

print(f"{'Milestone':<35} | {'Pre (0198d37)':<15} | {'Post (relift+switches)':<22}")
print("-" * 78)
for k in sorted(all_keys):
    print(f"{k:<35} | {pre_dist.get(k, 0):>2}/10 ({pre_dist.get(k,0)*10}%)   | {post_dist.get(k, 0):>2}/10 ({post_dist.get(k,0)*10}%)")

print("\n=== RUN-BY-RUN DETAILS ===")
print(f"{'Run':<4} | {'Pre Milestone':<32} | {'Pre G/P':<12} | {'Post Milestone':<32} | {'Post G/P':<12}")
print("-" * 105)
for r1, r2 in zip(pre, post):
    m1 = r1['milestone'].split('(')[0].strip()
    gp1 = f"{r1['max_groups']}/{r1['max_presents']}"
    m2 = r2['milestone'].split('(')[0].strip()
    gp2 = f"{r2['max_groups']}/{r2['max_presents']}"
    print(f"{r1['run']:<4} | {m1:<32} | {gp1:<12} | {m2:<32} | {gp2:<12}")

print("\n=== KEY METRICS ===")
for name, data in [("Pre (0198d37)", pre), ("Post (relift+switches)", post)]:
    nospace = sum(1 for r in data if r['nospace'])
    audio = sum(1 for r in data if r['audio_port_start'])
    vdec = sum(r['vdec_calls'] for r in data)
    sail = sum(r['sail_calls'] for r in data)
    crash = sum(1 for r in data if r['crash'])
    avg_gp = sum(r['max_groups'] for r in data) / len(data)
    avg_pres = sum(r['max_presents'] for r in data) / len(data)
    print(f"{name}:")
    print(f"  NOSPACE_EXIT: {nospace}/10")
    print(f"  cellAudioPortStart: {audio}/10")
    print(f"  cellVdec calls: {vdec}")
    print(f"  cellSail calls: {sail}")
    print(f"  Crashes: {crash}/10")
    print(f"  Mean max groups: {avg_gp:.1f}, Mean max presents: {avg_pres:.1f}")
