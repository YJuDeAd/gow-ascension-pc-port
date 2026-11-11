#!/usr/bin/env python3
"""
tools/relift_wws_pm.py
Automation tool to re-lift wws_job_pm with missing entrypoints.
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

    extra_funcs = "0x23B0,0x23BC,0x23C0,0x02C,0x068,0x2968,0x2978"

    cmd = [
        sys.executable, spu_lifter, elf,
        "--auto-functions", elf,
        "--symbol-prefix", "spu_wws_job_pm_",
        "--extra-funcs", extra_funcs,
        "-o", out_dir
    ]

    print(f"Running lifter: {' '.join(cmd)}")
    res = subprocess.run(cmd, check=True)

    recomp_c = os.path.join(out_dir, "spu_recomp.c")
    content = open(recomp_c, "r", encoding="utf-8").read()

    # Normalize include path as per standard CMake build
    content = content.replace('#include "../../runtime/spu/spu_helpers.h"', '#include "spu_helpers.h"')

    # Add unhooked execution counter to spu_wws_job_pm_spu_func_00001E48
    target_fn = "void spu_wws_job_pm_spu_func_00001E48(spu_context* ctx) {"
    counter_code = (
        "void spu_wws_job_pm_spu_func_00001E48(spu_context* ctx) {\n"
        "    { static uint64_t s_cnt_1e48 = 0; if (++s_cnt_1e48 <= 100 || (s_cnt_1e48 % 500) == 0) "
        "fprintf(stderr, \"[WWS-1E48] count=%llu spu_id=%u img=%d\\n\", (unsigned long long)s_cnt_1e48, ctx->spu_id, ctx->image_id); }"
    )
    if target_fn in content:
        content = content.replace(target_fn, counter_code, 1)
        print("Added execution counter to spu_wws_job_pm_spu_func_00001E48")
    else:
        print("WARNING: target_fn not found in spu_recomp.c")

    with open(recomp_c, "w", encoding="utf-8") as f:
        f.write(content)

    print("Re-lift complete. Checking for 0x23C0...")
    if "0x23C0" in content:
        print("VERIFIED: 0x23C0 successfully emitted in spu_recomp.c")
    else:
        print("ERROR: 0x23C0 not found in spu_recomp.c")
        sys.exit(1)

if __name__ == "__main__":
    main()
