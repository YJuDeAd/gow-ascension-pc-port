import glob
import os
import re

def analyze_logs(pattern):
    files = sorted(glob.glob(pattern))
    print(f"Analyzing {len(files)} files matching '{pattern}'...")
    results = []
    for p in files:
        with open(p, 'r', encoding='utf-8', errors='ignore') as f:
            text = f.read()

        # Did main thread leave 0x0061B9FC?
        # Check if 0x0033A394 appears in BOOT-DIAG or thread audit
        left_barrier = ('0x0033A394' in text or '0x00467B1C' in text)
        
        # Check if stalled at 0x0061B9FC
        stalled_at_barrier = False
        m_lrs = re.findall(r'Thread tid=1 \[main\]:.*?lr=(0x[0-9A-Fa-f]+)', text)
        if m_lrs and m_lrs[-1].upper() == '0x0061B9FC':
            stalled_at_barrier = True

        pm_ends = re.findall(r'\[pm-end\].*', text)
        depth_2000 = re.findall(r'\[spu-depth\].*host_depth=2000.*', text)
        halted_events = re.findall(r'\[pm-end\] lane (\d+).*?halted=(\d+).*?pc=(0x[0-9A-Fa-f]+).*?depth=(\d+)', text)
        
        results.append({
            'file': os.path.basename(p),
            'left_barrier': left_barrier,
            'stalled_at_barrier': stalled_at_barrier,
            'pm_ends_count': len(pm_ends),
            'depth_2000_count': len(depth_2000),
            'halted_lanes': [h[0] for h in halted_events if h[1] == '1'],
            'halted_events': halted_events
        })

    passing = [r for r in results if r['left_barrier']]
    stalling = [r for r in results if not r['left_barrier']]
    print(f"Summary: {len(passing)} passed, {len(stalling)} stalled")
    print(f"Depth=2000 events in passing runs: {sum(r['depth_2000_count'] for r in passing)}")
    print(f"Depth=2000 events in stalling runs: {sum(r['depth_2000_count'] for r in stalling)}")
    print(f"Halted lane events in passing runs: {sum(len(r['halted_lanes']) for r in passing)}")
    print(f"Halted lane events in stalling runs: {sum(len(r['halted_lanes']) for r in stalling)}")
    
    print("\nBreakdown by run:")
    for r in results:
        print(f"  {r['file']}: passed={r['left_barrier']} | d2k={r['depth_2000_count']} | halted_lanes={r['halted_lanes']}")

print("=== PART 1 RUNS (Old Unmerged Baseline: MLAA ON / OFF) ===")
analyze_logs("scratch/part1_runs/*_stderr.log")

print("\n=== MATRIX RUNS (Cell 1 Baseline) ===")
analyze_logs("scratch/matrix/cell1_*_stderr.log")
