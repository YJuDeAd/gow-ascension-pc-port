import os, sys, time, subprocess

EXE = "build/gow_ascension.exe"
TARGET_ELF = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"
LOG_DIR = "scratch/part2"
os.makedirs(LOG_DIR, exist_ok=True)

stdout_path = os.path.join(LOG_DIR, "part2_test_single_stdout.log")
stderr_path = os.path.join(LOG_DIR, "part2_test_single_stderr.log")

env = os.environ.copy()
env["PS3_VFS_ROOT"] = "E:"
env["SPU_ATOM_EA"] = "70A9E900"
env["SPU_ATOM_EA_MAX"] = "0"
env["SPU_ATOM_FULL"] = "2"
env["SPU_PUTLLC_WHY"] = "1"
env["SPURS_PM_CLAIMLOG"] = "1"

print(f"[TEST SINGLE] Starting 62s run at {time.strftime('%H:%M:%S')}...")
with open(stdout_path, "wb") as f_out, open(stderr_path, "wb") as f_err:
    proc = subprocess.Popen([EXE, TARGET_ELF], stdout=f_out, stderr=f_err, env=env)
    try:
        proc.wait(timeout=62)
    except subprocess.TimeoutExpired:
        print("[TEST SINGLE] 62s reached, terminating...")
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

print("[TEST SINGLE] Run finished. Checking stderr logs...")
