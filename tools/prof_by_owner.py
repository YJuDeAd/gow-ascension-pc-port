"""Where the SPU threads' time goes, by owner, from a run.ps1 -Profile result.

usage: python tools/prof_by_owner.py build/logs/<run>-prof.txt [--top 25]

Takes each profiled thread's exclusive samples (tools/profile_threads.py
output, resolved), keeps the SPU worker threads (spurs_pm_worker in the stack)
and the SPU task threads (spu_async_thread), and prints, per kind, the mean
share of a thread for every owner: a lifted image or job module, a runtime
function (rt:), another executable function, or a DLL. "runtime total" is the
sum of the rt: rows: the SPU model's own cost, apart from the lifted code.
"""
import argparse
import collections
import re


def owner(name):
    if 'rva=' not in name:
        return 'dll:' + name.split('+')[0]
    fn = name.split()[1].split('+')[0]
    m = re.search(r'\[(.*)\]', name)
    mod = m.group(1) if m else ''
    if 'spu_recomp' in mod:
        if fn.startswith('jobmod_'):
            return 'lifted:' + fn.split('_spu_func')[0]
        return 'lifted:' + fn.split('_spu_func')[0][:40]
    if 'runtime' in mod:
        return 'rt:' + fn
    return 'exe:' + fn + ' [' + mod.split(':')[-1] + ']'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prof')
    ap.add_argument('--top', type=int, default=25)
    a = ap.parse_args()
    t = open(a.prof, encoding='latin1').read()
    agg = collections.Counter()
    threads = collections.Counter()
    for sec in t.split('prof tid')[1:]:
        kind = 'worker' if 'spurs_pm_worker' in sec else 'task' if 'spu_async_thread' in sec else None
        if not kind:
            continue
        threads[kind] += 1
        ex = sec.split('--- inclusive ---')[0]
        for m in re.finditer(r'^\s+([\d.]+)%\s+(\S.*)$', ex, re.M):
            agg[(kind, owner(m.group(2)))] += float(m.group(1))
    for kind in ('worker', 'task'):
        if not threads[kind]:
            continue
        items = sorted(((v / threads[kind], k[1]) for k, v in agg.items() if k[0] == kind), reverse=True)
        rt = sum(v for v, k in items if k.startswith('rt:'))
        print(f"== SPU {kind} threads ({threads[kind]}), mean % of a thread; runtime total {rt:.1f}%")
        for v, k in items[:a.top]:
            print(f"  {v:5.1f}  {k}")


if __name__ == '__main__':
    main()
