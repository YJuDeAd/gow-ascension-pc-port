#!/usr/bin/env python3
import sys
import os
import glob
import re

def main():
    if len(sys.argv) < 2:
        print("Usage: show_func.py <address_or_func_name> [context_lines]")
        sys.exit(1)

    target = sys.argv[1].strip()
    if target.startswith("0x") or target.startswith("0X"):
        target_addr = target[2:].upper().zfill(8)
        target_func = f"func_{target_addr}"
    elif target.startswith("func_"):
        target_func = target
        target_addr = target[5:].upper().zfill(8)
    else:
        target_func = f"func_{target.upper().zfill(8)}"
        target_addr = target.upper().zfill(8)

    context_lines = int(sys.argv[2]) if len(sys.argv) > 2 else 50

    # Search in src/gen/ppu_recomp_*.cpp or src/gen/*.cpp
    files = sorted(glob.glob("src/gen/ppu_recomp_*.cpp") + glob.glob("src/gen/*.cpp"))
    
    # First pass: search for function definition
    found = False
    for filepath in files:
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
        for i, line in enumerate(lines):
            m = re.match(r"^(?:void|uint\w+_t|int\w+_t)\s+(func_[0-9A-Fa-f]{8})\s*\(", line)
            if m and m.group(1) == target_func:
                print(f"=== Found definition in {filepath} at line {i+1} (Function: {target_func}) ===")
                start = max(0, i)
                end = min(len(lines), i + context_lines)
                for lno in range(start, end):
                    print(f"    {lno+1}: {lines[lno]}", end="")
                return

    # Second pass: search for occurrence
    for filepath in files:
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
        for i, line in enumerate(lines):
            if target_addr in line:
                print(f"=== Found occurrence in {filepath} at line {i+1} ===")
                start = max(0, i - 10)
                end = min(len(lines), i + context_lines)
                for lno in range(start, end):
                    prefix = ">>> " if lno == i else "    "
                    print(f"{prefix}{lno+1}: {lines[lno]}", end="")
                return

    if not found:
        print(f"Address/Function {target} not found in src/gen/*.cpp")

if __name__ == "__main__":
    main()
