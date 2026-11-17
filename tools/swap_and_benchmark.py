import os
import sys
import shutil
import hashlib
import subprocess
import time

FILES_MAP = {
    'spu_channels.c': ('runtime/spu/spu_channels.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_channels.c'),
    'spu_coherency.c': ('runtime/spu/spu_coherency.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_coherency.c'),
    'spu_drain.c': ('runtime/spu/spu_drain.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_drain.c'),
    'spu_dma.h': ('runtime/spu/spu_dma.h', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_dma.h'),
    'spu_workload.c': ('runtime/spu/spu_workload.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spu_workload.c'),
    'spurs_policy.c': ('runtime/spu/spurs_policy.c', 'D:/gow-bilal-asia/ps3kit/runtime/spu/spurs_policy.c'),
    'cellSpurs.c': ('libs/spurs/cellSpurs.c', 'D:/gow-bilal-asia/ps3kit/libs/spurs/cellSpurs.c'),
    'cellGcmSys.c': ('libs/video/cellGcmSys.c', 'D:/gow-bilal-asia/ps3kit/libs/video/cellGcmSys.c'),
}

BACKUP_DIR = 'scratch/our_runtime_backup'
PS3RECOMP_DIR = 'ps3recomp'
RUNTIME_LIB = 'ps3recomp/build/libps3recomp_runtime.a'
EXE = 'build/gow_ascension.exe'

def get_sha256(path):
    if not os.path.exists(path):
        return 'MISSING'
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest().upper()

def restore_all_baseline():
    for name, (rel_path, _) in FILES_MAP.items():
        src = os.path.join(BACKUP_DIR, name)
        dst = os.path.join(PS3RECOMP_DIR, rel_path)
        shutil.copyfile(src, dst)

def swap_single_file(target_name):
    restore_all_baseline()
    if target_name:
        rel_path, their_path = FILES_MAP[target_name]
        dst = os.path.join(PS3RECOMP_DIR, rel_path)
        shutil.copyfile(their_path, dst)
        print(f"Swapped {target_name} -> {dst}")

def build_all():
    # Build ps3recomp_runtime
    res = subprocess.run(['ninja', '-C', 'ps3recomp/build', 'ps3recomp_runtime'], capture_output=True, text=True)
    if res.returncode != 0:
        print("ERROR building ps3recomp_runtime:\n", res.stderr or res.stdout)
        return False
    # Build gow_ascension.exe
    res = subprocess.run(['ninja', '-C', 'build', 'gow_ascension.exe'], capture_output=True, text=True)
    if res.returncode != 0:
        print("ERROR building gow_ascension.exe:\n", res.stderr or res.stdout)
        return False
    return True

if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else None
    if target and target not in FILES_MAP:
        print(f"Unknown target: {target}")
        sys.exit(1)

    print(f"Applying swap: {target or 'BASELINE'}")
    swap_single_file(target)

    print("Building...")
    if not build_all():
        print("BUILD FAILED!")
        sys.exit(1)

    lib_sha = get_sha256(RUNTIME_LIB)
    exe_sha = get_sha256(EXE)
    print(f"BUILD OK: lib_sha={lib_sha[:8]} ({lib_sha}) | exe_sha={exe_sha[:8]} ({exe_sha})")
