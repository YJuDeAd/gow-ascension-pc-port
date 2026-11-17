import os
import difflib

files = [
    ('spu_channels.c', 'ps3recomp/runtime/spu/spu_channels.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_channels.c'),
    ('spu_coherency.c', 'ps3recomp/runtime/spu/spu_coherency.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_coherency.c'),
    ('spu_drain.c', 'ps3recomp/runtime/spu/spu_drain.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_drain.c'),
    ('spu_dma.h', 'ps3recomp/runtime/spu/spu_dma.h', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_dma.h'),
    ('spu_workload.c', 'ps3recomp/runtime/spu/spu_workload.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_workload.c'),
    ('spurs_policy.c', 'ps3recomp/runtime/spu/spurs_policy.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spurs_policy.c'),
    ('cellSpurs.c', 'ps3recomp/libs/spurs/cellSpurs.c', 'D:/gow-bilal-asia/ps3kit/libs/spurs/cellSpurs.c'),
    ('cellGcmSys.c', 'ps3recomp/libs/video/cellGcmSys.c', 'D:/gow-bilal-asia/ps3kit/libs/video/cellGcmSys.c'),
]

os.makedirs('scratch/part3_diffs', exist_ok=True)

for name, our_p, their_p in files:
    with open(our_p, 'r', errors='ignore') as f1, open(their_p, 'r', errors='ignore') as f2:
        l1 = f1.readlines()
        l2 = f2.readlines()
    diff = list(difflib.unified_diff(l1, l2, fromfile='our/'+name, tofile='their/'+name))
    diff_out = os.path.join('scratch/part3_diffs', f'{name}.diff')
    with open(diff_out, 'w') as df:
        df.writelines(diff)
    print(f'Wrote {diff_out}: {len(diff)} lines')
