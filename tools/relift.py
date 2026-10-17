#!/usr/bin/env python3
"""
relift.py — Windows-native wrapper around the ps3recomp PPU lifter pipeline for God of War: Ascension.

Orchestrates:
1. Boundary analysis (find_code_end.py) -> --code-end
2. OPD descriptor parsing (ppu_loader.py) -> analysis/EBOOT.functions.json & EBOOT.imports.json
3. Heuristic function recovery (find_functions.py) -> analysis/functions.json
4. PPU lifting (ppu_lifter.py) with --code-end and --hle-stubs -> src/gen/
5. HLE NID table generation (gen_hle_nids.py) -> src/gen/ppu_hle_nids.cpp
"""

import sys
import os
import subprocess
import argparse
from pathlib import Path

def run_step(cmd, desc):
    print(f"\n[relift] === {desc} ===")
    print(f"[relift] Running: {' '.join(str(c) for c in cmd)}")
    res = subprocess.run(cmd, check=True)
    return res

def get_code_end(elf_path):
    find_tool = Path(__file__).parent / "find_code_end.py"
    res = subprocess.run(
        [sys.executable, str(find_tool), str(elf_path)],
        capture_output=True,
        text=True,
        check=True
    )
    for line in res.stdout.splitlines():
        if "Recommended --code-end boundary:" in line:
            return line.split(":")[-1].strip()
    return None

def main():
    parser = argparse.ArgumentParser(description="PPU code lifting wrapper for God of War: Ascension")
    parser.add_argument("--elf", default="vfs/PS3_GAME/USRDIR/EBOOT.ELF", help="Path to EBOOT.ELF")
    parser.add_argument("--out", default="src/gen", help="Output directory for generated C++ chunks")
    parser.add_argument("--analysis", default="analysis", help="Directory for analysis JSON files")
    parser.add_argument("--chunks", type=int, default=8, help="Number of C++ chunks to split into")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count(), help="Parallel workers for lifting")
    parser.add_argument("--code-end", default=None, help="Override code-end hex boundary")
    parser.add_argument("--skip-loader", action="store_true", help="Skip loader & find_functions if analysis exists")
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

    # 1. Determine code-end boundary
    code_end = args.code_end
    if not code_end:
        code_end = get_code_end(elf_path)
        print(f"[relift] Auto-detected --code-end boundary: {code_end}")
    else:
        print(f"[relift] Using specified --code-end boundary: {code_end}")

    funcs_json = analysis_dir / "functions.json"
    imports_json = analysis_dir / "EBOOT.imports.json"

    # 2. Run loader & function recovery
    if not args.skip_loader or not funcs_json.exists() or not imports_json.exists():
        run_step(
            [sys.executable, str(ps3recomp_tools / "ppu_loader.py"), str(elf_path), "-o", str(analysis_dir) + "/"],
            "Stage 1: PPU Loader (.opd & imports detection)"
        )
        run_step(
            [sys.executable, str(ps3recomp_tools / "find_functions.py"), str(elf_path),
             "--seed-json", str(analysis_dir / "EBOOT.functions.json"),
             "--output", str(funcs_json)],
            "Stage 2: Heuristic Function Recovery (prologues & branch targets)"
        )

    # Copy imports.json to root
    if imports_json.exists():
        root_imports = root_dir / "imports.json"
        import shutil
        shutil.copy2(imports_json, root_imports)

    # 3. Lift PPU code to C++
    lift_cmd = [
        sys.executable, str(ps3recomp_tools / "ppu_lifter.py"), str(elf_path),
        "--functions", str(funcs_json),
        "--hle-stubs", str(imports_json),
        "--chunks", str(args.chunks),
        "--jobs", str(args.jobs),
        "--output", str(out_dir)
    ]
    if code_end:
        lift_cmd.extend(["--code-end", code_end])

    run_step(lift_cmd, "Stage 3: PPU Code Lifting to C++ chunks")

    # 4. Generate HLE NID table
    hle_nids_out = out_dir / "ppu_hle_nids.cpp"
    run_step(
        [sys.executable, str(ps3recomp_tools / "gen_hle_nids.py"), "--all", "--out", str(hle_nids_out)],
        "Stage 4: Generate HLE NID Dispatch Table"
    )

    # 5. Output summary (WITHOUT reading files into memory)
    print("\n[relift] === Lifting Complete ===")
    gen_files = list(out_dir.glob("*.cpp")) + list(out_dir.glob("*.c")) + list(out_dir.glob("*.h"))
    total_bytes = sum(f.stat().st_size for f in gen_files)
    print(f"[relift] Output directory: {out_dir}")
    print(f"[relift] Generated files count: {len(gen_files)}")
    print(f"[relift] Total generated size: {total_bytes / (1024*1024):.2f} MB")
    for f in sorted(gen_files):
        print(f"  - {f.name} ({f.stat().st_size / (1024*1024):.2f} MB)")

if __name__ == '__main__':
    main()
