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

def apply_insn_switches(elf_in_path, elf_out_path, switches_path):
    print(f"[relift] Applying instruction switches from {switches_path}...")
    with open(switches_path, "r", encoding="utf-8") as f:
        switches = json.load(f)

    # Read input ELF
    with open(elf_in_path, "rb") as f:
        elf_bytes = bytearray(f.read())

    # Parse ELF64 program headers to map vaddr -> file offset
    e_phoff = int.from_bytes(elf_bytes[0x20:0x28], "big")
    e_phentsize = int.from_bytes(elf_bytes[0x36:0x38], "big")
    e_phnum = int.from_bytes(elf_bytes[0x38:0x3A], "big")

    segments = []
    for i in range(e_phnum):
        ph_off = e_phoff + i * e_phentsize
        p_type = int.from_bytes(elf_bytes[ph_off:ph_off+4], "big")
        p_offset = int.from_bytes(elf_bytes[ph_off+8:ph_off+16], "big")
        p_vaddr = int.from_bytes(elf_bytes[ph_off+16:ph_off+24], "big")
        p_filesz = int.from_bytes(elf_bytes[ph_off+32:ph_off+40], "big")
        if p_type == 1: # PT_LOAD
            segments.append((p_vaddr, p_filesz, p_offset))

    applied_count = 0
    for s in switches:
        addr = int(s["addr"], 0)
        insn = int(s["insn"], 0)
        patched = False
        for p_vaddr, p_filesz, p_offset in segments:
            if p_vaddr <= addr < p_vaddr + p_filesz:
                file_off = addr - p_vaddr + p_offset
                orig_insn = int.from_bytes(elf_bytes[file_off:file_off+4], "big")
                
                # Hard guardrail: all spin-yield patches MUST overwrite pure NOPs (0x60000000)
                if insn in (0x396003FC, 0x44000002):
                    assert orig_insn == 0x60000000, (
                        f"[FATAL GUARDRAIL] Attempted to patch non-NOP at {s['addr']} (file off 0x{file_off:X}): "
                        f"original instruction is 0x{orig_insn:08X}, expected 0x60000000 (NOP). Aborting."
                    )
                
                elf_bytes[file_off:file_off+4] = insn.to_bytes(4, "big")
                note = s.get("note", "")[:50]
                print(f"  [switch] {s['addr']} (file off 0x{file_off:X}): orig 0x{orig_insn:08X} -> new 0x{insn:08X} ({note})")
                applied_count += 1
                patched = True
                break
        if not patched:
            raise RuntimeError(f"[relift] Switch address {s['addr']} not found in any PT_LOAD segment!")

    with open(elf_out_path, "wb") as f:
        f.write(elf_bytes)
    print(f"[relift] Applied {applied_count} instruction switches -> {elf_out_path}")

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
    parser.add_argument("--insn-switches", default="tools/ppu_switches.json", help="Path to instruction switches JSON")
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

    # Step 2: Apply switches if provided
    lift_elf = elf_path
    temp_elf = None
    if args.insn_switches and os.path.exists(args.insn_switches):
        scratch_dir = root_dir / "scratch"
        scratch_dir.mkdir(parents=True, exist_ok=True)
        temp_elf = scratch_dir / "EBOOT_switched.ELF"
        apply_insn_switches(elf_path, temp_elf, args.insn_switches)
        lift_elf = temp_elf

    # Step 3: Run ppu_lifter.py
    lift_cmd = [
        sys.executable, str(ps3recomp_tools / "ppu_lifter.py"), str(lift_elf),
        "--functions", str(funcs_json),
        "--hle-stubs", str(imports_json),
        "--chunks", str(args.chunks),
        "--jobs", str(args.jobs),
        "--output", str(out_dir)
    ]
    if args.code_end:
        lift_cmd.extend(["--code-end", args.code_end])

    print(f"[relift] Running: {' '.join(str(c) for c in lift_cmd)}")
    subprocess.run(lift_cmd, check=True)

    # Step 4: Post-process lifted chunks
    postprocess_lifted_chunks(out_dir)

    # Step 5: Run gen_hle_nids.py
    hle_nids_out = out_dir / "ppu_hle_nids.cpp"
    gen_nids_cmd = [sys.executable, str(ps3recomp_tools / "gen_hle_nids.py"), "--all", "--out", str(hle_nids_out)]
    print(f"[relift] Running: {' '.join(str(c) for c in gen_nids_cmd)}")
    subprocess.run(gen_nids_cmd, check=True)

    # Cleanup temp elf if created
    if temp_elf and temp_elf.exists():
        temp_elf.unlink()

    # Step 6: Summary
    gen_files = list(out_dir.glob("*.cpp")) + list(out_dir.glob("*.h"))
    total_bytes = sum(f.stat().st_size for f in gen_files)
    print(f"\n[relift] Complete: {len(gen_files)} files, {total_bytes / (1024*1024):.2f} MB in {out_dir}")

if __name__ == "__main__":
    main()
