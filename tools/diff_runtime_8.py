import os
import difflib
import hashlib

files = [
    ('spu_channels.c', 'scratch/our_runtime_backup/spu_channels.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_channels.c'),
    ('spu_coherency.c', 'scratch/our_runtime_backup/spu_coherency.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_coherency.c'),
    ('spu_drain.c', 'scratch/our_runtime_backup/spu_drain.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_drain.c'),
    ('spu_dma.h', 'scratch/our_runtime_backup/spu_dma.h', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_dma.h'),
    ('spu_workload.c', 'scratch/our_runtime_backup/spu_workload.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_workload.c'),
    ('spurs_policy.c', 'scratch/our_runtime_backup/spurs_policy.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spurs_policy.c'),
    ('cellSpurs.c', 'scratch/our_runtime_backup/cellSpurs.c', 'D:/gow-bilal-asia/ps3kit/libs/spurs/cellSpurs.c'),
    ('cellGcmSys.c', 'scratch/our_runtime_backup/cellGcmSys.c', 'D:/gow-bilal-asia/ps3kit/libs/video/cellGcmSys.c'),
]

os.makedirs('scratch/part3_diffs', exist_ok=True)

print(f"{'Filename':16} | {'Our Size':>8} | {'Their Size':>10} | {'Our SHA':>8} | {'Their SHA':>9} | {'Hunks':>5} | {'Diff +':>6} | {'Diff -':>6}")
print("-" * 85)

for name, our_p, their_p in files:
    with open(our_p, 'rb') as f1, open(their_p, 'rb') as f2:
        b1 = f1.read()
        b2 = f2.read()
    sha1 = hashlib.sha256(b1).hexdigest()[:8]
    sha2 = hashlib.sha256(b2).hexdigest()[:8]
    l1 = b1.decode('utf-8', errors='ignore').splitlines(keepends=True)
    l2 = b2.decode('utf-8', errors='ignore').splitlines(keepends=True)
    diff = list(difflib.unified_diff(l1, l2, fromfile='our/' + name, tofile='their/' + name))
    hunks = [line for line in diff if line.startswith('@@')]
    added = sum(1 for line in diff if line.startswith('+') and not line.startswith('+++'))
    removed = sum(1 for line in diff if line.startswith('-') and not line.startswith('---'))
    print(f"{name:16} | {len(b1):8} | {len(b2):10} | {sha1:8} | {sha2:9} | {len(hunks):5} | {added:6} | {removed:6}")

    # write diff
    diff_file = os.path.join('scratch/part3_diffs', f"{name}.diff")
    with open(diff_file, 'w', encoding='utf-8') as df:
        df.writelines(diff)
