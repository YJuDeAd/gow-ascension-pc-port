#!/usr/bin/env python3
import re

with open("build/logs/phase0e_run_01.stderr.log", "r", encoding="utf-8", errors="replace") as f:
    lines = f.readlines()

print("--- Lines around transition in Run 01 ---")
for i, line in enumerate(lines):
    if "[TRANSITION-0x747D3510]" in line and "new=0xFFFFFFFF" in line:
        start = max(0, i - 15)
        end = min(len(lines), i + 15)
        for j in range(start, end):
            print(f"{j:5d}: {lines[j].strip()}")
        break
