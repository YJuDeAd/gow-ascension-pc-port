#!/usr/bin/env python3
import re

def inspect_log(path, label):
    print(f"\n=======================================================")
    print(f"=== {label}: {path} ===")
    print(f"=======================================================")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    print("--- [TRANSITION-0x747D3510] ---")
    for line in lines:
        if "[TRANSITION-0x747D3510]" in line:
            print(line.strip())

    print("\n--- PPU [ww] writes to 0x747D3510 ---")
    for line in lines:
        if "[ww]" in line and "0x747D3510" in line:
            print(line.strip())

    print("\n--- SPU DMA [watchea] writes covering 0x747D3510 (cmd=0x20 PUT) ---")
    for line in lines:
        if "[watchea]" in line and ("cmd=0x20" in line or "cmd=0x24" in line or "cmd=0x28" in line or "cmd=0x2C" in line):
            print(line.strip())

    print("\n--- Wid 8 and brbDriver activity ---")
    for line in lines:
        if "BrBDblBufWrkld" in line or "THREAD9-brbDriver" in line or "cellSpursAddWorkload" in line:
            print(line.strip())

inspect_log("build/logs/phase0e_run_01.stderr.log", "STALLING RUN 01")
inspect_log("build/logs/phase0e_run_08.stderr.log", "PASSING RUN 08")
inspect_log("build/logs/phase0e_run_09.stderr.log", "PASSING RUN 09")
