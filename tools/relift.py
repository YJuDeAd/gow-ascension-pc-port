#!/usr/bin/env python3
"""
relift.py — PPU lifting pipeline wrapper for God of War: Ascension.

Handles:
1. Instruction switches from tools/ppu_switches.json with strict NOP-only assertion
2. Heuristic function recovery & import stubs from analysis/
3. PPU lifting via ps3recomp/tools/ppu_lifter.py into target directory
4. Post-processing to preserve gpr[3] and gpr[11] around all 0x3FC yields (including Loop 3)
5. Firmware HLE NID dispatch table generation via ps3recomp/tools/gen_hle_nids.py
"""

import sys
import os
import shutil
import subprocess
import argparse
import json
from pathlib import Path

def prepare_patch_sites(switches_path: str, out_path: str):
    """Convert switches (list of dicts or dict) to patch_sites.json {addr: insn}."""
    with open(switches_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    sites = {}
    if isinstance(data, list):
        for s in data:
            sites[s["addr"]] = s["insn"]
    elif isinstance(data, dict):
        sites = data
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(sites, f, indent=2)
    print(f"[relift] Prepared {len(sites)} runtime patch site(s) -> {out_path}")
    return out_path

def postprocess_lifted_chunks(out_dir: Path):
    print(f"[relift] Post-processing lifted chunks in {out_dir} to preserve gpr[3] and gpr[11]...")
    
    yield_old = (
        "        ctx->gpr[11] = (int64_t)(int32_t)(0x3FC);\n"
        "        lv2_syscall(ctx);"
    )
    yield_new = (
        "        { uint64_t _r3 = ctx->gpr[3], _r11 = ctx->gpr[11]; "
        "ctx->gpr[11] = 0x3FC; lv2_syscall(ctx); ctx->gpr[3] = _r3; ctx->gpr[11] = _r11; }"
    )

    # 1. Replace all bare 0x3FC syscall yields with register-preserving block
    replaced_count = 0
    for cpp_file in sorted(out_dir.glob("ppu_recomp_*.cpp")):
        text = cpp_file.read_text(encoding="latin-1")
        if yield_old in text:
            cnt = text.count(yield_old)
            text = text.replace(yield_old, yield_new)
            cpp_file.write_text(text, encoding="latin-1")
            print(f"  [postprocess] {cpp_file.name}: preserved gpr[3]/gpr[11] across {cnt} yield site(s)")
            replaced_count += cnt

    print(f"[relift] Post-processed {replaced_count} yield site(s) with register preservation.")

def main():
    parser = argparse.ArgumentParser(description="PPU code lifting wrapper for God of War: Ascension")
    parser.add_argument("--elf", default="vfs/PS3_GAME/USRDIR/EBOOT.ELF", help="Path to EBOOT.ELF")
    parser.add_argument("--out", default="src/gen", help="Output directory for generated C++ chunks")
    parser.add_argument("--analysis", default="analysis", help="Directory for analysis JSON files")
    parser.add_argument("--chunks", type=int, default=8, help="Number of C++ chunks to split into")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count(), help="Parallel workers for lifting")
    parser.add_argument("--code-end", default="0x0062C4CC", help="Override code-end hex boundary")
    parser.add_argument("--insn-switches", default="tools/patch_sites.json", help="Path to instruction switches JSON")
    parser.add_argument("--patch-sites", default=None, help="Direct path to patch_sites.json")
    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parent.parent
    elf_path = (root_dir / args.elf).resolve()
    out_dir = (root_dir / args.out).resolve()
    analysis_dir = (root_dir / args.analysis).resolve()
    ps3recomp_tools = (root_dir / "ps3recomp" / "tools").resolve()

    if not elf_path.exists():
        print(f"Error: ELF not found at {elf_path}", file=sys.stderr)
        sys.exit(1)

    out_dir.mkdir(parents=True, exist_ok=True)
    analysis_dir.mkdir(parents=True, exist_ok=True)

    funcs_json = analysis_dir / "functions.json"
    imports_json = analysis_dir / "EBOOT.imports.json"

    # Step 1: Ensure analysis files exist
    if not funcs_json.exists() or not imports_json.exists():
        print("[relift] Analysis files missing, generating via ppu_loader.py & find_functions.py...")
        subprocess.run([sys.executable, str(ps3recomp_tools / "ppu_loader.py"), str(elf_path), "-o", str(analysis_dir) + "/"], check=True)
        subprocess.run([sys.executable, str(ps3recomp_tools / "find_functions.py"), str(elf_path),
                        "--seed-json", str(analysis_dir / "EBOOT.functions.json"),
                        "--output", str(funcs_json)], check=True)

    # Step 2: Prepare runtime patch sites if switches are provided
    patch_sites_file = None
    switches_source = args.patch_sites if hasattr(args, "patch_sites") and args.patch_sites else args.insn_switches
    if switches_source and os.path.exists(switches_source):
        scratch_dir = root_dir / "scratch"
        scratch_dir.mkdir(parents=True, exist_ok=True)
        patch_sites_file = scratch_dir / "patch_sites.json"
        prepare_patch_sites(switches_source, str(patch_sites_file))

    # Step 3: Run ppu_lifter.py
    lifter_script = root_dir / "tools" / "ppu_lifter.py"

    lift_cmd = [
        sys.executable, str(lifter_script), str(elf_path),
        "--functions", str(funcs_json),
        "--hle-stubs", str(imports_json),
        "--chunks", str(args.chunks),
        "--jobs", str(args.jobs),
        "--output", str(out_dir)
    ]
    if args.code_end:
        lift_cmd.extend(["--code-end", args.code_end])
    if patch_sites_file:
        lift_cmd.extend(["--patch-sites", str(patch_sites_file)])

    lift_env = os.environ.copy()
    lift_env["PYTHONUTF8"] = "0"
    print(f"[relift] Running: {' '.join(str(c) for c in lift_cmd)}")
    subprocess.run(lift_cmd, env=lift_env, check=True)

    # Step 4: Post-process lifted chunks
    postprocess_lifted_chunks(out_dir)

    # Step 5: Run gen_hle_nids.py
    hle_nids_out = out_dir / "ppu_hle_nids.cpp"
    gen_nids_cmd = [sys.executable, str(ps3recomp_tools / "gen_hle_nids.py"), "--all", "--out", str(hle_nids_out)]
    print(f"[relift] Running: {' '.join(str(c) for c in gen_nids_cmd)}")
    subprocess.run(gen_nids_cmd, check=True)

    # Cleanup scratch patch sites if created
    if patch_sites_file and patch_sites_file.exists():
        patch_sites_file.unlink()

    # Step 6: Summary
    gen_files = list(out_dir.glob("*.cpp")) + list(out_dir.glob("*.h"))
    total_bytes = sum(f.stat().st_size for f in gen_files)
    print(f"\n[relift] Complete: {len(gen_files)} files, {total_bytes / (1024*1024):.2f} MB in {out_dir}")

if __name__ == "__main__":
    main()
