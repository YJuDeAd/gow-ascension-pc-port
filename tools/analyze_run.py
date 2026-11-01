import sys, re, os

def analyze(log_path):
    if not os.path.exists(log_path):
        print(f"File not found: {log_path}")
        return None

    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    result = {
        "log": os.path.basename(log_path),
        "lanes_30s": "N/A",
        "pm_entries_8_18": {},
        "pm_entries_0_7": {},
        "reached_7b1c": False,
        "time_reached_7b1c": None,
        "left_7b1c": False,
        "time_left_7b1c": None,
        "q3_exit": "N/A",
        "yields_per_lane": [0]*6,
        "host_cpu_per_lane": ["0 ms"]*6,
        "max_live_draws": 0,
        "first_draw_s": None,
        "groups_exec": 0,
        "presents": 0,
        "last_stage": "N/A",
        "stage_name": "UNKNOWN"
    }

    for i, line in enumerate(lines):
        # Stage lines
        m_stage = re.search(r"\[STAGE\]\s+t=(\d+)\s+main_lr=(0x[0-9A-Fa-f]+)\s+main_last_hle=([^\s]+)\s+live_draws=(\d+)\s+presents=(\d+)", line)
        if m_stage:
            t_sec = int(m_stage.group(1))
            lr = m_stage.group(2).upper()
            hle = m_stage.group(3)
            draws = int(m_stage.group(4))
            presents = int(m_stage.group(5))
            result["last_stage"] = f"t={t_sec} lr={lr} hle={hle} presents={presents}"
            result["stage_name"] = f"{lr} ({hle})"
            if draws > result["max_live_draws"]:
                result["max_live_draws"] = draws
            if draws > 0 and result["first_draw_s"] is None:
                result["first_draw_s"] = t_sec
            if presents > result["presents"]:
                result["presents"] = presents

            if lr in ("0x00467B1C", "0x00466E28"):
                if not result["reached_7b1c"]:
                    result["reached_7b1c"] = True
                    result["time_reached_7b1c"] = t_sec
            elif result["reached_7b1c"] and not result["left_7b1c"]:
                result["left_7b1c"] = True
                result["time_left_7b1c"] = t_sec

        # Lane audit at 30s
        if "=== [SPU-LANE-AUDIT @ 30s] ===" in line:
            lanes = []
            for j in range(i+1, min(len(lines), i+30)):
                m_lane = re.search(r"Lane\s+(\d+):\s+wid=(\d+)", lines[j])
                if m_lane:
                    lanes.append(f"L{m_lane.group(1)}:w{m_lane.group(2)}")
                if "=====" in lines[j] and j > i+3:
                    break
            if lanes:
                result["lanes_30s"] = " ".join(lanes)

        # HostCPU & Yields in SPU lane stats
        m_lane_cpu = re.search(r"Lane\s+(\d+):\s+wid=\d+.*?HostCPU=(\d+)\s*ms.*?Yields=(\d+)", line)
        if m_lane_cpu:
            l_idx = int(m_lane_cpu.group(1))
            if l_idx < 6:
                result["host_cpu_per_lane"][l_idx] = f"{m_lane_cpu.group(2)} ms"
                result["yields_per_lane"][l_idx] = int(m_lane_cpu.group(3))

        # Individual spu-yield line
        m_yield = re.search(r"\[spu-yield\]\s+lane=(\d+)", line)
        if m_yield:
            l_idx = int(m_yield.group(1))
            if l_idx < 6:
                result["yields_per_lane"][l_idx] += 1

        # Workload pm_entries
        m_wk = re.search(r"Workload\s+'([^']+)'\s+\(wid=(\d+)[^)]*\):.*?pm_entries=(\d+)", line)
        if m_wk:
            wid = int(m_wk.group(2))
            entries = int(m_wk.group(3))
            if wid >= 8:
                result["pm_entries_8_18"][wid] = entries
            else:
                result["pm_entries_0_7"][wid] = entries

        # Queue 3 telemetry
        m_q3 = re.search(r"\[QUEUE3-TELEMETRY\s+@\s+\d+s\]\s+(target=\d+\s+min_L=\d+\s+:\s+L0=\d+.*)", line)
        if m_q3:
            result["q3_exit"] = m_q3.group(1).strip()

        # Groups exec
        m_grp = re.search(r"groups\[seen=\d+\s+exec=(\d+)", line)
        if m_grp:
            g = int(m_grp.group(1))
            if g > result["groups_exec"]:
                result["groups_exec"] = g

    return result

if __name__ == "__main__":
    if len(sys.argv) > 1:
        res = analyze(sys.argv[1])
        if res:
            print("--- Analysis Result ---")
            for k, v in res.items():
                print(f"{k}: {v}")
