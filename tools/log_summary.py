"""Summarise a run log: how far it got, fps, video speed, interpreter and errors.

usage: python tools/log_summary.py build/logs/<run>.log

- fps series and whether the run stalled (no frames after loading)
- the cinematic's playback speed when VDEC_TIMELOG=1 was set
- interpreted SPU code (SPU_INTERP_HIST=1) and unlifted entries per overlay
- the last PS3_HLE_HIST / LV2_SYSCALL_HIST / PS3_POLLTOP windows, if present
- counts of the error-like lines worth a look
"""
import collections
import re
import statistics
import sys


def main(path):
    lines = open(path, encoding="utf-8", errors="ignore").read().splitlines()

    fps = [float(m.group(1)) for l in lines
           for m in [re.search(r"(?:\[fps\] |\[perf\] fps=)([\d.]+)", l)] if m]
    print(f"fps ({len(fps)} x 5 s):", " ".join(f"{x:.0f}" for x in fps))
    after = [x for x in fps if x < 100]          # the loading screens run at ~180
    if fps and not after:
        print("  -> never left the loading screens: the end-of-loading stall (retry)")
    elif after:
        print(f"  after loading: min {min(after):.0f}  median {sorted(after)[len(after)//2]:.0f}  max {max(after):.0f}")

    stable = [float(m.group(1)) for l in lines
              for m in [re.search(r"\[perf\] fps=([\d.]+) spu=(\d+) hold=(\d+)", l)]
              if m and int(m.group(2)) >= 75 and int(m.group(3)) == 0]
    if stable:
        print(f"  SPU >=75%, Hold=0 ({len(stable)} windows): median {statistics.median(stable):.2f} FPS;"
              " moving scene, not a fixed-view benchmark")

    vt = [(int(m.group(1)), int(m.group(2))) for l in lines
          for m in [re.search(r"\[vdec-time\] t=(\d+)us pts=(\d+)", l)] if m]
    if len(vt) > 10:
        (t0, p0), (t1, p1) = vt[0], vt[-1]
        media, wall = (p1 - p0) / 90000, (t1 - t0) / 1e6
        print(f"video: {len(vt)} pictures, {media:.1f} s of movie in {wall:.1f} s -> speed {100*media/wall:.0f}%")

    # Per frame (between two flips): how many AUs the title fed, how many pictures it took.
    flips = [int(m.group(1)) for l in lines for m in [re.search(r"\[flip-time\] t=(\d+)us", l)] if m]
    aus = [int(m.group(1)) for l in lines for m in [re.search(r"\[vdec-au\] t=(\d+)us", l)] if m]
    if len(flips) > 10 and vt:
        import bisect
        flips.sort()
        def per_frame(times):
            c = collections.Counter(bisect.bisect(flips, t) for t in times)
            return collections.Counter(c.get(i, 0) for i in range(bisect.bisect(flips, vt[0][0]), len(flips)))
        span = (flips[-1] - flips[0]) / 1e6
        print(f"flips: {len(flips)} ({len(flips)/span:.1f}/s); frames by AUs fed: {dict(sorted(per_frame(aus).items()))};"
              f" by pictures taken: {dict(sorted(per_frame([t for t, _ in vt]).items()))}")
        busy = sum(1 for l in lines if "[vdec-au]" in l and "BUSY" in l)
        if busy:
            print(f"  DecodeAu refused BUSY {busy} times")

    hist = collections.Counter()
    for l in lines:
        m = re.search(r"\[interp-hist\]\s+ovl=(\d+) start=0x([0-9A-F]+) steps=(\d+)", l)
        if m:
            hist[(int(m.group(1)), m.group(2))] += int(m.group(3))
    if hist:
        tot = sum(hist.values())
        print(f"interpreted SPU instructions: {tot}; top entries:")
        for (ovl, pc), n in hist.most_common(8):
            print(f"  ovl={ovl:3} 0x{pc}  {100*n/tot:5.1f}%")
    starts = collections.Counter()
    for l in lines:
        m = re.search(r"microstep img=\d+ ovl=(\d+) start=", l)
        if m:
            starts[int(m.group(1))] += 1
    if starts:
        print("unlifted SPU entries seen, per overlay:", dict(sorted(starts.items())))

    for title in ("--- HLE calls", "--- lv2 syscalls", "--- usleep poll sites"):
        idx = [i for i, l in enumerate(lines) if l.startswith(title)]
        if idx:
            i = idx[-1]
            print(lines[i])
            for l in lines[i + 1:i + 9]:
                if not l.startswith("  "):
                    break
                print(l)

    checks = {
        "REJECTED": r"REJECTED", "HALT": r"HALT", "BRANCH-TO-0": r"BRANCH-TO-0",
        "UNSUPPORTED executed": r"\[spu\] UNSUPPORTED", "no free code span": r"no free resident code span",
        "WAIT_SIGNAL sleeping": r"sleeping \d+s in WAIT_SIGNAL", "exceptions": r"EXCEPTION|Access violation",
        "FIFO errors": r"FIFO error|malformed method|Invalid RSX method|FATAL|Assertion Failed",
    }
    found = {k: sum(1 for l in lines if re.search(p, l)) for k, p in checks.items()}
    found = {k: v for k, v in found.items() if v}
    if found:
        print("worth a look:", found)
    print("log:", path)


if __name__ == "__main__":
    main(sys.argv[1])
