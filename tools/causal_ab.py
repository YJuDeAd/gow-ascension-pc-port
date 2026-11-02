"""Read the [causal] windows of a run made with CAUSAL_TOGGLE_S=<s> and say how
much of the delay reached the frame.

usage: python tools/causal_ab.py build/logs/<run>.log [more logs] [--skip 4] [--added-ms 2.6]

Each window with the delay on is set against the mean of the off windows on
either side of it (same scene, same machine state), so the spread between
runs cancels. Prints the median frame time difference over those pairs, with
its spread, and, given --added-ms (what the delay adds to its link a frame),
the share that passed on.
"""
import argparse
import re
import statistics


def windows(path):
    out = []
    for l in open(path, encoding="utf-8", errors="ignore"):
        m = re.search(r"\[causal\] win=(\d+) on=(\d) frames=(\d+) fps=([\d.]+) med=([\d.]+)ms p90=([\d.]+)ms", l)
        if m:
            out.append((int(m.group(1)), int(m.group(2)), float(m.group(4)), float(m.group(5)), float(m.group(6))))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--skip", type=int, default=4, help="first windows left out (loading, intro)")
    ap.add_argument("--added-ms", type=float, default=0.0)
    a = ap.parse_args()
    d_med, d_fps, d_p90 = [], [], []
    for path in a.logs:
        w = [x for x in windows(path) if x[0] >= a.skip]
        for i in range(1, len(w) - 1):
            if w[i][1] != 1 or w[i - 1][1] != 0 or w[i + 1][1] != 0:
                continue
            off = [w[i - 1], w[i + 1]]
            d_fps.append(w[i][2] - statistics.mean(x[2] for x in off))
            d_med.append(w[i][3] - statistics.mean(x[3] for x in off))
            d_p90.append(w[i][4] - statistics.mean(x[4] for x in off))
        on = [x for x in w if x[1] == 1]
        off = [x for x in w if x[1] == 0]
        if on and off:
            print(f"{path}: {len(w)} windows; fps off {statistics.median(x[2] for x in off):.1f}"
                  f" on {statistics.median(x[2] for x in on):.1f}; frame median off"
                  f" {statistics.median(x[3] for x in off):.2f} on {statistics.median(x[3] for x in on):.2f} ms")
    if not d_med:
        print("no on window with off windows on both sides")
        return
    q = lambda v: statistics.quantiles(v, n=4) if len(v) >= 4 else [min(v), statistics.median(v), max(v)]
    m = statistics.median(d_med)
    print(f"{len(d_med)} pairs: frame median {m:+.2f} ms (quartiles {q(d_med)[0]:+.2f} / {q(d_med)[-1]:+.2f}),"
          f" p90 {statistics.median(d_p90):+.2f} ms, fps {statistics.median(d_fps):+.2f}")
    if a.added_ms > 0:
        print(f"passed on: {m / a.added_ms * 100:.0f} % of {a.added_ms} ms")


if __name__ == "__main__":
    main()
