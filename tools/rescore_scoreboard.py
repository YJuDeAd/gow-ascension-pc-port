import os
import re
import glob

def score_log(run_name):
    stderr_p = os.path.join("build/logs", f"{run_name}_stderr.log")
    stdout_p = os.path.join("build/logs", f"{run_name}_stdout.log")
    if not os.path.exists(stderr_p):
        return None
    with open(stderr_p, "r", encoding="utf-8", errors="replace") as f:
        err = f.read()
    with open(stdout_p, "r", encoding="utf-8", errors="replace") as f:
        out = f.read()

    # first_draw_s
    first_draw_s = "-"
    m_fd = re.search(r"(?:FIRST-DRAW-0x1808|\[FIRST-DRAW-0x1808).*?(?:@|wall=)\s*([0-9.]+)s", err + out)
    if m_fd:
        first_draw_s = m_fd.group(1)

    # groups
    groups = 0
    for m in re.finditer(r"groups\[seen=\d+ exec=(\d+)", err):
        g = int(m.group(1))
        if g > groups:
            groups = g

    # dl_head
    dl_head = 0
    for m in re.finditer(r"dl_head=(\d+)", err):
        d = int(m.group(1))
        if d > dl_head:
            dl_head = d

    # presents
    presents = 0
    ld_matches = list(re.finditer(r"\[live-draw\] frame (\d+)", err))
    if ld_matches:
        presents = int(ld_matches[-1].group(1))

    # duration & flips/s
    m_sec = list(re.finditer(r"=== \[TASK1-SPURS-TELEMETRY @ (\d+)s\] ===", err))
    duration = float(m_sec[-1].group(1)) if m_sec else 0.0
    flips_s = round(presents / duration, 2) if duration > 0 and presents > 0 else 0.0

    # nonblack from [frame-dump] lines
    nonblack = 0
    for m in re.finditer(r"\[frame-dump\].*?nonblack=(\d+)", err):
        if int(m.group(1)) > 0:
            nonblack += 1

    # audio_peak
    audio_peak = 0.0
    for m in re.finditer(r"\[audio-peak\] peak=([0-9.]+)", err):
        val = float(m.group(1))
        if val > audio_peak:
            audio_peak = val

    # wid9_ready_to_0
    wid9_ready_to_0 = "no"
    for line in err.splitlines():
        if "wid  9:" in line and "ready_gst=0" in line and "pm_entries=" in line:
            wid9_ready_to_0 = "yes"
            break

    # preempt_events
    preempt_events = len(re.findall(r"\[SPU-POLL-PREEMPT", err))

    # lane_wakeups
    lane_wakeups = len(re.findall(r"\[spu-int\] TAKEN|spu_ch_wake", err))

    # pm_entries_wid8_14 & lanes_held_by_wid0_3 from final telemetry
    last_telem = err.rfind("=== [TASK1-SPURS-TELEMETRY")
    telem_block = err[last_telem:] if last_telem != -1 else err

    lanes_held = 0
    for m in re.finditer(r"Lane \d+: state=running pm wid=(\d+)", telem_block):
        w = int(m.group(1))
        if 0 <= w <= 3:
            lanes_held += 1

    pm_entries_8_14 = {w: 0 for w in range(8, 15)}
    for m in re.finditer(r"wid\s+(\d+):.*?pm_entries=(\d+)", telem_block):
        w = int(m.group(1))
        if 8 <= w <= 14:
            pm_entries_8_14[w] = int(m.group(2))
    pm_entries_summary = ",".join(f"w{w}:{pm_entries_8_14[w]}" for w in range(8, 15))

    # Identify tid=1 by thread name "main"
    tid_main_lr = "-"
    for line in reversed(err.splitlines()):
        m = re.search(r"Thread tid=\d+ \[main\]:.*?lr=(0x[0-9a-fA-F]+)", line)
        if m:
            tid_main_lr = m.group(1).lower()
            break

    # watchdog_threads
    threads = set()
    for m in re.finditer(r"Thread tid=\d+ \[([^\]]+)\]", err):
        threads.add(m.group(1))
    watchdog_threads = len(threads)

    return {
        "run": run_name,
        "first_draw_s": first_draw_s,
        "groups": groups,
        "dl_head": dl_head,
        "presents": presents,
        "flips_s": flips_s,
        "nonblack": nonblack,
        "audio_peak": audio_peak,
        "wid9_ready_to_0": wid9_ready_to_0,
        "preempt_events": preempt_events,
        "lane_wakeups": lane_wakeups,
        "lanes_held_by_wid0_3": lanes_held,
        "pm_entries_wid8_14": pm_entries_summary,
        "tid_main_lr": tid_main_lr,
        "watchdog_threads": watchdog_threads
    }

if __name__ == "__main__":
    runs = [
        "smoke_20261028_174635",
        "smoke_20261028_174948",
        "smoke_20261028_175051",
        "smoke_20261028_175454",
        "smoke_20261028_175606"
    ]
    print(f"{'Run':<26} | {'first_draw':<10} | {'groups':<6} | {'dl_head':<7} | {'presents':<8} | {'flips/s':<7} | {'nonblack':<8} | {'peak':<6} | {'wid9->0':<7} | {'preempt':<7} | {'wakeups':<7} | {'held0-3':<7} | {'pm_entries(8-14)':<35} | {'main_lr':<10}")
    print("-" * 180)
    for r in runs:
        res = score_log(r)
        if res:
            print(f"{res['run']:<26} | {res['first_draw_s']:<10} | {res['groups']:<6} | {res['dl_head']:<7} | {res['presents']:<8} | {res['flips_s']:<7} | {res['nonblack']:<8} | {res['audio_peak']:<6} | {res['wid9_ready_to_0']:<7} | {res['preempt_events']:<7} | {res['lane_wakeups']:<7} | {res['lanes_held_by_wid0_3']:<7} | {res['pm_entries_wid8_14']:<35} | {res['tid_main_lr']:<10}")
