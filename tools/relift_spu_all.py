import os
import sys
import time
import subprocess
import shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TOOLS = os.path.join(ROOT, "tools")
EBOOT = os.path.join(ROOT, "vfs", "PS3_GAME", "USRDIR", "EBOOT.ELF")
JOBMODS_DIR = "D:/gow-bilal-asia/work/jobmods"
EXTRA_FUNCS_JSON = os.path.join(TOOLS, "jobmods_extra_funcs.json")
JOB_FRAGMENTS_JSON = os.path.join(TOOLS, "job_fragments.json")
OUT_DIR = os.path.join(ROOT, "scratch", "spu_relifted")
EXTRACTED_SPU = os.path.join(ROOT, "scratch", "extracted_spu")

def run_step(cmd, env, desc):
    print(f"\n>>> [STEP] {desc}")
    print(f"    Command: {' '.join(cmd)}")
    t0 = time.time()
    res = subprocess.run(cmd, cwd=ROOT, env=env, text=True, capture_output=True)
    elapsed = time.time() - t0
    if res.returncode != 0:
        print(f"!!! ERROR in {desc} (code {res.returncode}):")
        print(res.stderr[:2000])
        print(res.stdout[:2000])
        sys.exit(res.returncode)
    print(f"    Done in {elapsed:.2f}s")
    return elapsed

def main():
    t_start = time.time()
    print("================================================================================")
    print("RE-LIFT SPU PIPELINE: --merge-chunks + --xregs")
    print(f"Root: {ROOT}")
    print(f"Out:  {OUT_DIR}")
    print("================================================================================")

    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(EXTRACTED_SPU, exist_ok=True)

    env = os.environ.copy()
    env["SPU_LIFT_XREGS"] = "1"
    env["LIFT_JOBMODS_MERGE"] = "1"
    env["LIFT_MERGE_CHUNKS"] = "1"

    # Step 1: Ensure extracted images exist
    print("Extracting SPU images from pristine EBOOT...")
    subprocess.run([sys.executable, os.path.join(ROOT, "ps3recomp", "tools", "extract_spu_images.py"), EBOOT, "-o", EXTRACTED_SPU], check=True)
    subprocess.run([sys.executable, os.path.join(TOOLS, "extract_spu_pm.py"), EBOOT, os.path.join(EXTRACTED_SPU, "spu_wws_pm_at_0077CA80.elf"),
                    "--address", "0x77CA80", "--size", "12288", "--fnv", "0xC8D774963A96DCF2"], check=True)

    # Step 2: Lift Jobmods
    step1_time = run_step([
        sys.executable, os.path.join(TOOLS, "lift_jobmods.py"),
        "--jobmods", JOBMODS_DIR,
        "--out", OUT_DIR,
        "--register", os.path.join(OUT_DIR, "jobmods_register.c"),
        "--symbol-prefix", "ascension",
        "--extra-funcs-json", EXTRA_FUNCS_JSON
    ], env, "Lift Job Modules (33 modules with --merge-chunks and --xregs)")

    # Step 3: Lift SPU PIC fragments
    step2_time = run_step([
        sys.executable, os.path.join(TOOLS, "lift_spu_pic.py"),
        "--elf", EBOOT,
        "--fragments", JOB_FRAGMENTS_JSON,
        "--out", OUT_DIR,
        "--register", os.path.join(OUT_DIR, "pic_register.c"),
        "--symbol-prefix", "ascension"
    ], env, "Lift SPU PIC Fragments from EBOOT with --xregs")

    # Step 4: Build SPU Workloads
    step3_time = run_step([
        sys.executable, os.path.join(TOOLS, "build_spu_workloads.py"),
        "--images", EXTRACTED_SPU,
        "--lifted", OUT_DIR,
        "--out", os.path.join(OUT_DIR, "spu_workloads.c"),
        "--register-fn", "ascension_spu_register_all",
        "--constructor",
        "--title", "ascension",
        "--relift",
        "--merge-chunks",
        "--extra-funcs", "spu_wws_pm_at_0077CA80=0xA2C,0xA68,0x3368,0x3378"
    ], env, "Build SPU Workload Images (5 ELFs with --merge-chunks and --xregs)")

    # Step 5: Post-wrap if needed
    spu_existing = os.path.join(ROOT, "src", "gen", "spu")
    if os.path.exists(spu_existing):
        for entry in os.listdir(spu_existing):
            full_p = os.path.join(spu_existing, entry)
            recomp_c = os.path.join(full_p, "spu_recomp.c")
            if os.path.isdir(full_p) and os.path.exists(recomp_c):
                # Check for wrap markers
                with open(recomp_c, "r", encoding="utf-8", errors="replace") as f:
                    content = f.read()
                import re
                m = re.findall(r"^static spu_fn_desc s_fnw_([0-9A-Fa-f]{8}) = \{ (\d+),", content, re.M)
                if m:
                    image_id = m[0][1]
                    addrs = [match[0] for match in m]
                    target_dir = os.path.join(OUT_DIR, entry)
                    if os.path.exists(target_dir):
                        print(f"Applying spu_wrap.py to {entry} (image {image_id}, addrs={addrs})...")
                        subprocess.run([sys.executable, os.path.join(TOOLS, "spu_wrap.py"), target_dir, image_id] + addrs, check=True)

    print(f"\nInstalling {OUT_DIR} into {spu_existing}...")
    shutil.rmtree(spu_existing, ignore_errors=True)
    shutil.copytree(OUT_DIR, spu_existing)
    print("Installation complete.")

    total_time = time.time() - t_start
    print("\n================================================================================")
    print(f"RE-LIFT COMPLETE: Total time = {total_time:.2f}s")
    print("================================================================================")

if __name__ == "__main__":
    main()
