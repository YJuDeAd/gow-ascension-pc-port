"""Sample the busiest host threads of a running AscensionRecomp and name the hot code.

usage: python tools/profile_threads.py --pid <pid> --dbg <PS3_DEBUG file>
           [--threads 4] [--secs 3] [--map build/win/AscensionRecomp.map] [--out prof.txt]

The game must have been started with PS3_DEBUG=<dbg file> (scripts/windows/run.ps1 -Profile
does it). Measures every thread's CPU time over 3 s, asks the debug console for
`prof <tid> <secs>` on the busiest ones, waits for the answers and resolves the
addresses with tools/maprva.py. Prints the top exclusive entries per thread.
"""
import argparse
import os
import subprocess
import sys
import time


def thread_cpu(pid):
    ps = ("$p = Get-Process -Id %d -ErrorAction Stop; $p.Threads | ForEach-Object "
          "{ '{0} {1}' -f $_.Id, $_.TotalProcessorTime.TotalMilliseconds }" % pid)
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                         capture_output=True, text=True).stdout
    cpu = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) == 2:
            try:
                cpu[int(f[0])] = float(f[1].replace(",", "."))
            except ValueError:
                pass
    return cpu


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--dbg", required=True)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--secs", type=int, default=3)
    ap.add_argument("--map", default=os.path.join(os.path.dirname(__file__), "..", "build", "win", "AscensionRecomp.map"))
    ap.add_argument("--out", default=None)
    ap.add_argument("--verb", default="prof", help="prof (per function) or profip (exclusive per instruction)")
    a = ap.parse_args()

    before = thread_cpu(a.pid)
    time.sleep(3)
    after = thread_cpu(a.pid)
    busy = sorted(((after[t] - before.get(t, 0.0), t) for t in after), reverse=True)[:a.threads]
    print("busiest threads (ms of CPU in 3 s):", ", ".join(f"{t}={ms:.0f}" for ms, t in busy))

    out_raw = a.dbg + ".out"
    start = os.path.getsize(out_raw) if os.path.exists(out_raw) else 0
    for _, tid in busy:
        with open(a.dbg, "w") as f:
            f.write(f"{a.verb} {tid} {a.secs}\n")
        time.sleep(a.secs + 1.5)
    time.sleep(1)
    if not os.path.exists(out_raw):
        sys.exit("no answer from the debug console (was PS3_DEBUG set?)")
    with open(out_raw, encoding="latin1") as f:
        f.seek(start)
        raw = f.read()
    tmp = out_raw + ".part"
    with open(tmp, "w", encoding="latin1") as f:
        f.write(raw)
    res = subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(__file__), "maprva.py"), a.map, tmp],
                         capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    os.remove(tmp)
    if a.out:
        # utf-8 explicitly: the console code page cannot encode every symbol
        with open(a.out, "w", encoding="utf-8", errors="replace") as f:
            f.write(res)
    # Top of each thread's exclusive list.
    block, take = [], 0
    for line in res.splitlines():
        if "prof tid" in line:
            print(line.strip()); take = 0
        elif "--- exclusive" in line:
            take = 8
        elif take:
            print("   " + line.strip()[:100]); take -= 1
    if a.out:
        print("full profile:", a.out)


if __name__ == "__main__":
    main()
