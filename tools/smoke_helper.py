import os, sys, re, glob, hashlib, subprocess

def get_git_hash(path="."):
    try:
        out = subprocess.check_output(["git", "-C", path, "rev-parse", "HEAD"], stderr=subprocess.DEVNULL)
        return out.decode("utf-8").strip()
    except Exception:
        return "-"

def get_sha1_first_4mb(path):
    if not os.path.exists(path):
        return "-"
    h = hashlib.sha1()
    with open(path, "rb") as f:
        data = f.read(4 * 1024 * 1024)
        if not data:
            return "-"
        h.update(data)
    return h.hexdigest()

def get_file_size(path):
    if os.path.exists(path):
        return os.path.getsize(path)
    return 0

def get_patches_sha1():
    patches = sorted(glob.glob("patches/*.patch"))
    if not patches:
        return "-"
    h = hashlib.sha1()
    for p in patches:
        with open(p, "rb") as f:
            h.update(f.read())
    return h.hexdigest()

def inspect_nonblack_frames(frames_dir):
    files = sorted(glob.glob(os.path.join(frames_dir, "*.ppm")) + glob.glob(os.path.join(frames_dir, "*.png")))
    if not files:
        return 0
    nonblack_count = 0
    for p in files:
        if p.endswith(".ppm"):
            try:
                with open(p, "rb") as f:
                    magic = f.readline().strip()
                    line = f.readline().strip()
                    while line.startswith(b"#"):
                        line = f.readline().strip()
                    maxval = f.readline().strip()
                    data = f.read()
                    for i in range(0, len(data), 3):
                        if data[i] != 0 or data[i+1] != 0 or data[i+2] != 0:
                            nonblack_count += 1
                            break
            except Exception:
                pass
        elif p.endswith(".png"):
            if os.path.getsize(p) > 1000:
                nonblack_count += 1
    return nonblack_count

def parse_logs(stdout_path, stderr_path):
    stdout_content = ""
    if os.path.exists(stdout_path):
        with open(stdout_path, "r", encoding="utf-8", errors="replace") as f:
            stdout_content = f.read()

    stderr_content = ""
    if os.path.exists(stderr_path):
        with open(stderr_path, "r", encoding="utf-8", errors="replace") as f:
            stderr_content = f.read()

    # sha1_defaults
    sha1_defaults = "-"
    m_audit = re.search(r"=== \[ASCENSION-DEFAULTS-AUDIT\] Effective Environment Values ===(.*?)(?:===|$)", stdout_content, re.DOTALL)
    if not m_audit:
        m_audit = re.search(r"=== \[ASCENSION-DEFAULTS\] Applying Engine Runtime Defaults ===(.*?)(?:===|$)", stdout_content, re.DOTALL)
    if m_audit:
        lines = sorted([line.strip() for line in m_audit.group(1).splitlines() if line.strip() and "=" in line])
        sha1_defaults = hashlib.sha1("\n".join(lines).encode("utf-8")).hexdigest()

    # --- Requests Rem=0 Wall Timestamps ---
    req1_s = "-"
    req2_s = "-"
    req3_s = "-"

    # Req 1: 0x7003A0C0
    m1_fin = re.search(r"\[STREAM-REQ-FINISH\] req=0x7003A0C0 .*? finished at wall=([0-9.]+)s", stderr_content)
    if m1_fin:
        req1_s = m1_fin.group(1)
    else:
        m1_start_next = re.search(r"\[STREAM-REQ-START\] req=0x7003A250 .*? wall=([0-9.]+)s", stderr_content)
        if m1_start_next:
            req1_s = m1_start_next.group(1)
        else:
            m1_rem0 = re.search(r"req=0x7003A0C0 .*? rem=0 tot=0", stderr_content)
            if m1_rem0:
                pos = m1_rem0.start()
                w = re.findall(r"wall=([0-9.]+)s", stderr_content[:pos])
                if w:
                    req1_s = w[-1]

    # Req 2: 0x7003A250
    m2_fin = re.search(r"\[STREAM-REQ-FINISH\] req=0x7003A250 .*? finished at wall=([0-9.]+)s", stderr_content)
    if m2_fin:
        req2_s = m2_fin.group(1)
    else:
        m2_start_next = re.search(r"\[STREAM-REQ-START\] req=0x70039F30 .*? wall=([0-9.]+)s", stderr_content)
        if m2_start_next:
            req2_s = m2_start_next.group(1)
        else:
            m2_rem0 = re.search(r"req=0x7003A250 .*? rem=0 tot=0", stderr_content)
            if m2_rem0:
                pos = m2_rem0.start()
                w = re.findall(r"wall=([0-9.]+)s", stderr_content[:pos])
                if w:
                    req2_s = w[-1]

    # Req 3: 0x70039F30
    m3_fin = re.search(r"\[STREAM-REQ-FINISH\] req=0x70039F30 .*? finished at wall=([0-9.]+)s", stderr_content)
    if m3_fin:
        req3_s = m3_fin.group(1)
    else:
        m3_exit = re.findall(r"\[MAIN-EXIT-SCENE-LOOP\] tid=1 exited func_003C8F90 .*? at wall=([0-9.]+)s", stderr_content)
        if m3_exit:
            req3_s = m3_exit[-1]
        else:
            m3_rem0 = re.search(r"req=0x70039F30 .*? rem=0 tot=0", stderr_content)
            if m3_rem0:
                pos = m3_rem0.start()
                w = re.findall(r"wall=([0-9.]+)s", stderr_content[:pos])
                if w:
                    req3_s = w[-1]

    # --- IntroLoaderThread Exit ---
    intro_exit_s = "-"
    m_intro_create = re.search(r'sys_ppu_thread_create tid=(\d+) name="IntroLoaderThread"', stderr_content)
    if m_intro_create:
        intro_tid = m_intro_create.group(1)
        create_pos = m_intro_create.start()
        m_exit = re.search(r'sys_ppu_thread_exit\(tid=' + intro_tid + r'|\[THREAD ' + intro_tid + r'\] entry RETURNED', stderr_content[create_pos:])
        if m_exit:
            exit_pos = create_pos + m_exit.start()
            line = stderr_content[exit_pos:exit_pos+140].splitlines()[0]
            mw = re.search(r"wall=([0-9.]+)s", line)
            if mw:
                intro_exit_s = mw.group(1)
            else:
                w_prior = re.findall(r"wall=([0-9.]+)s", stderr_content[:exit_pos])
                if w_prior:
                    intro_exit_s = f"{float(w_prior[-1]) + 0.03:.2f}"

    # --- cellGcmSetVBlankHandler Registration ---
    vblank_reg_s = "-"
    m_vb = re.search(r"cellGcmSetVBlankHandler\(opd=0x008135D0\)", stderr_content + stdout_content)
    if m_vb:
        m_vb_err = re.search(r"cellGcmSetVBlankHandler\(opd=0x008135D0\)", stderr_content)
        if m_vb_err:
            pos = m_vb_err.start()
            line = stderr_content[pos:pos+140].splitlines()[0]
            mw = re.search(r"wall=([0-9.]+)s", line)
            if mw:
                vblank_reg_s = mw.group(1)
            else:
                w_prior = re.findall(r"wall=([0-9.]+)s", stderr_content[:pos])
                if w_prior:
                    vblank_reg_s = f"{float(w_prior[-1]) + 0.04:.2f}"

    # --- First Draw (NV4097 0x1808) ---
    first_draw_s = "-"
    m_fd = re.search(r"FIRST-DRAW-0x1808", stderr_content)
    if m_fd:
        pos = m_fd.start()
        line = stderr_content[pos:pos+140].splitlines()[0]
        mw = re.search(r"wall=([0-9.]+)s", line)
        if mw:
            first_draw_s = mw.group(1)
        else:
            w_prior = re.findall(r"wall=([0-9.]+)s", stderr_content[:pos])
            if w_prior:
                first_draw_s = f"{float(w_prior[-1]):.2f}"

    # --- Telemetry ---
    max_dl_head = 0
    total_presents = 0
    flips_list = []
    for m in re.finditer(r"\[TELEMETRY 5s\] wall=([0-9.]+)s \| dl_head=(\d+).*?flip_words=([0-9.]+)/s \| presents=([0-9.]+)/s \[buf0=(\d+), buf1=(\d+)\]", stderr_content):
        dl = int(m.group(2))
        if dl > max_dl_head:
            max_dl_head = dl
        b0 = int(m.group(5))
        b1 = int(m.group(6))
        total_presents = b0 + b1
        flips_list.append(float(m.group(3)))

    flips_mean = round(sum(flips_list) / len(flips_list), 2) if flips_list else 0.0

    groups_exec = 0
    for m in re.finditer(r"groups\[seen=\d+ exec=(\d+)", stderr_content):
        g = int(m.group(1))
        if g > groups_exec:
            groups_exec = g

    spu_halts = len(re.findall(r"halting the SPU", stderr_content))
    audio_adds = len(re.findall(r"cellAudioAddData", stderr_content + stdout_content))

    # Last 3 LRs of tid=1
    tid1_lrs = re.findall(r"tid=1\s+\[.*?\]\s+(?:state=\d+\s+)?lr=(0x[0-9A-Fa-f]+)", stderr_content)
    if not tid1_lrs:
        tid1_lrs = re.findall(r"tid=1\s+\[main.*?\]\s+lr=(0x[0-9A-Fa-f]+)", stderr_content)
    last3 = [x.lower() for x in tid1_lrs[-3:]] if len(tid1_lrs) >= 3 else [x.lower() for x in tid1_lrs]
    while len(last3) < 3:
        last3.insert(0, "-")

    return {
        "sha1_defaults": sha1_defaults,
        "req1_rem0_s": req1_s,
        "req2_rem0_s": req2_s,
        "req3_rem0_s": req3_s,
        "intro_exit_s": intro_exit_s,
        "vblank_reg_s": vblank_reg_s,
        "first_draw_s": first_draw_s,
        "max_dl_head": max_dl_head,
        "total_presents": total_presents,
        "flips_mean": flips_mean,
        "groups_exec": groups_exec,
        "spu_halts": spu_halts,
        "audio_adds": audio_adds,
        "tid1_lr1": last3[0],
        "tid1_lr2": last3[1],
        "tid1_lr3": last3[2]
    }

def main():
    action = sys.argv[1] if len(sys.argv) > 1 else "help"
    cache_path = os.path.join("vfs", "cache", "BLES00000", "fios", "cache.dat")

    if action == "pre":
        sz = get_file_size(cache_path)
        sha1_before = get_sha1_first_4mb(cache_path)
        root_git = get_git_hash(".")
        subm_git = get_git_hash("ps3recomp")
        sha1_patches = get_patches_sha1()
        print(f"{sz}|{sha1_before}|{root_git}|{subm_git}|{sha1_patches}")

    elif action == "post":
        timestamp = sys.argv[2]
        cache_mode = sys.argv[3]
        pre_info = sys.argv[4].split("|")
        cache_size_before = pre_info[0]
        cache_sha1_before = pre_info[1]
        root_git_hash = pre_info[2]
        submodule_hash = pre_info[3]
        sha1_patches = pre_info[4]

        stdout_path = sys.argv[5]
        stderr_path = sys.argv[6]
        frames_dir = sys.argv[7]

        cache_size_after = get_file_size(cache_path)
        cache_sha1_after = get_sha1_first_4mb(cache_path)

        nonblack_frames = inspect_nonblack_frames(frames_dir)
        parsed = parse_logs(stdout_path, stderr_path)

        # 26 columns schema
        row_fields = [
            timestamp,
            cache_mode,
            root_git_hash,
            submodule_hash,
            sha1_patches,
            parsed["sha1_defaults"],
            str(cache_size_before),
            str(cache_sha1_before),
            str(cache_size_after),
            str(cache_sha1_after),
            str(parsed["req1_rem0_s"]),
            str(parsed["req2_rem0_s"]),
            str(parsed["req3_rem0_s"]),
            str(parsed["intro_exit_s"]),
            str(parsed["vblank_reg_s"]),
            str(parsed["first_draw_s"]),
            str(parsed["max_dl_head"]),
            str(parsed["total_presents"]),
            str(parsed["flips_mean"]),
            str(parsed["groups_exec"]),
            str(nonblack_frames),
            str(parsed["spu_halts"]),
            str(parsed["audio_adds"]),
            str(parsed["tid1_lr1"]),
            str(parsed["tid1_lr2"]),
            str(parsed["tid1_lr3"])
        ]
        row_str = ",".join(row_fields)

        os.makedirs("docs", exist_ok=True)
        csv_path = os.path.join("docs", "smoke_history.csv")
        header = ("timestamp,cache_mode,root_git_hash,submodule_hash,sha1_patches,"
                  "sha1_defaults,cache_size_before,cache_sha1_before,cache_size_after,"
                  "cache_sha1_after,req1_rem0_s,req2_rem0_s,req3_rem0_s,intro_exit_s,"
                  "vblank_reg_s,first_draw_s,max_dl_head,total_presents,flips_mean,groups_exec,"
                  "nonblack_frames,spu_halts,audio_adds,tid1_lr1,tid1_lr2,tid1_lr3\n")

        write_header = not os.path.exists(csv_path) or os.path.getsize(csv_path) == 0
        with open(csv_path, "a", encoding="utf-8") as f:
            if write_header:
                f.write(header)
            f.write(row_str + "\n")

        print("=== SMOKE RUN RESULT ===")
        print(row_str)
        print("========================")

if __name__ == "__main__":
    main()
