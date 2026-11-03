"""One frame's SPU work laid out in time, from a frame_timeline.py run log.

usage: python tools/frame_layout.py build/logs/<run>.log [--frame -20] [--min-us 500]

Same variables as tools/frame_timeline.py (PPU_WWATCH=99AA80 for the frame starts,
SPURS_WID_TIMELOG, SPU_TASK_TIMELOG, GCM_REPORTLOG). Prints, for frame --frame
(negative counts from the end), every policy-module run of at least --min-us
(start-end in ms from the frame's start, workload, SPU), each task's busy spans
(exit of WAIT_SIGNAL to the next enter) and the GPU reports, in start order.
"""
import argparse
import re


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--frame", type=int, default=-20)
    ap.add_argument("--min-us", type=int, default=500)
    a = ap.parse_args()
    starts, runs, task, reps = [], [], [], []
    for l in open(a.log, encoding="utf-8", errors="ignore"):
        m = re.search(r"\[ww\] t=(\d+)us 0x0*99AAA0 <-", l)
        if m: starts.append(int(m.group(1))); continue
        m = re.match(r"\[wid-time\] t=(\d+)us d=(\d+)us wid=(\d+) spu=(\d+)", l)
        if m: runs.append(tuple(map(int, m.groups()))); continue
        m = re.search(r"\[task-time\] t=(\d+)us (enter|exit) task=(\d+)", l)
        if m: task.append((int(m.group(1)), m.group(2), int(m.group(3)))); continue
        m = re.search(r"\[GET_REPORT\] t=(\d+)us type=(\d+) off=0x(\w+)", l)
        if m: reps.append((int(m.group(1)), int(m.group(2)), m.group(3)))
    f0, f1 = starts[a.frame - 1], starts[a.frame]
    ms = lambda t: (t - f0) / 1000.0
    rows = [(t, f"  {ms(t):6.2f}-{ms(t + d):6.2f}  wid {w:2d} spu {s}") for t, d, w, s in runs
            if f0 <= t < f1 and d >= a.min_us]
    out = {}
    for t, kind, k in task:
        if kind == "exit": out[k] = t
        elif k in out:
            t0 = out.pop(k)
            if f0 <= t0 < f1: rows.append((t0, f"  {ms(t0):6.2f}-{ms(t):6.2f}  task {k}"))
    rows += [(t, f"  {ms(t):6.2f}         report type {ty} off 0x{o}") for t, ty, o in reps if f0 <= t < f1]
    print(f"frame {a.frame}: {(f1 - f0) / 1000:.1f} ms")
    for _, r in sorted(rows): print(r)


if __name__ == "__main__":
    main()
