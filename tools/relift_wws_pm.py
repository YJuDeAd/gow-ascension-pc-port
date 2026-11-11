#!/usr/bin/env python3
"""
tools/relift_wws_pm.py
Automation tool to re-lift wws_job_pm with the reference entrypoints:
0x2C, 0x68, 0x2968, 0x2978 (matching gow-ascension-pc scripts/unix/relift.sh:42).
Never hand-edit src/gen/ -- all changes are produced via this tool.
"""
import os
import subprocess
import sys

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    spu_lifter = os.path.join(root, "ps3recomp", "tools", "spu_lifter.py")
    elf = os.path.join(root, "spu_programs", "wws_job_pm.elf")
    out_dir = os.path.join(root, "src", "gen", "spu", "wws_job_pm")

    # Reference entry points (gow-ascension-pc relift.sh:42: 0xA2C, 0xA68, 0x3368, 0x3378 relative to base 0xA00)
    extra_funcs = "0x2C,0x68,0x2968,0x2978"

    cmd = [
        sys.executable, spu_lifter, elf,
        "--auto-functions", elf,
        "--symbol-prefix", "spu_wws_job_pm_",
        "--extra-funcs", extra_funcs,
        "-o", out_dir
    ]

    print(f"Running lifter: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

    recomp_c = os.path.join(out_dir, "spu_recomp.c")
    content = open(recomp_c, "r", encoding="utf-8").read()

    # Normalize include path as per standard CMake build (matching relift.ps1:78)
    content = content.replace('#include "../../runtime/spu/spu_helpers.h"', '#include "spu_helpers.h"')

    with open(recomp_c, "w", encoding="utf-8") as f:
        f.write(content)

    print("Re-lift with reference entrypoints complete.")

if __name__ == "__main__":
    main()
