import os, sys, time, subprocess

LOCK_FILE = "build/run.lock"
EXE = "build/gow_ascension.exe"
TARGET_ELF = "vfs/PS3_GAME/USRDIR/EBOOT.ELF"
LOG_DIR = "scratch/part3"
os.makedirs(LOG_DIR, exist_ok=True)

def acquire_lock():
    while True:
        try:
            fd = os.open(LOCK_FILE, os.O_CREAT | os.O_EXCL | os.O_RDWR)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            print(f"[LOCK] Acquired {LOCK_FILE}")
            break
        except FileExistsError:
            print(f"[LOCK] Waiting for {LOCK_FILE}...")
            time.sleep(1)

def release_lock():
    try:
        os.remove(LOCK_FILE)
        print(f"[LOCK] Released {LOCK_FILE}")
    except OSError:
        pass

def run_test(name, extra_env, duration=20):
    acquire_lock()
    try:
        env = os.environ.copy()
        env["PS3_VFS_ROOT"] = "E:"
        env.update(extra_env)

        stdout_path = os.path.join(LOG_DIR, f"{name}_stdout.log")
        stderr_path = os.path.join(LOG_DIR, f"{name}_stderr.log")

        print(f"\n========================================================")
        print(f"[RUN {name}] Starting {duration}s run at {time.strftime('%H:%M:%S')}...")
        with open(stdout_path, "wb") as f_out, open(stderr_path, "wb") as f_err:
            proc = subprocess.Popen([EXE, TARGET_ELF], stdout=f_out, stderr=f_err, env=env)
            try:
                proc.wait(timeout=duration)
            except subprocess.TimeoutExpired:
                print(f"[RUN {name}] {duration}s reached, terminating...")
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()

        # Check stdout/stderr for patch lines
        print(f"[RUN {name}] Checking output:")
        with open(stdout_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "[patches]" in line:
                    print(f"  STDOUT: {line.strip()}")
        with open(stderr_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "[patches]" in line or "boot:entry" in line:
                    print(f"  STDERR: {line.strip()}")
    finally:
        release_lock()

if __name__ == "__main__":
    # Test 1: Default ON (no extra env)
    run_test("test_default_on", {}, duration=15)

    # Test 2: Explicit OFF (GOWA_NO_MLAA=0)
    run_test("test_explicit_off", {"GOWA_NO_MLAA": "0"}, duration=10)

    # Test 3: Explicit ON (PS3_PATCHES="Disable MLAA")
    run_test("test_patches_env_on", {"PS3_PATCHES": "Disable MLAA"}, duration=10)

    # Test 4: Explicit None (PS3_PATCHES="none")
    run_test("test_patches_env_none", {"PS3_PATCHES": "none"}, duration=10)
