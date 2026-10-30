#!/usr/bin/env python3
import sys
import bisect
import re

def load_map(map_path="build/gow_ascension.map"):
    symbols = []
    with open(map_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if len(line) < 42:
                continue
            addr_str = line[:8].strip()
            if len(addr_str) == 8:
                try:
                    addr = int(addr_str, 16)
                    parts = line.split(None, 3)
                    if len(parts) >= 4:
                        sym = parts[3].strip()
                        if not sym.endswith(".o:") and not sym.startswith((".", "CMakeFiles")):
                            symbols.append((addr, sym))
                except ValueError:
                    pass
    symbols.sort(key=lambda x: x[0])
    return symbols

def resolve(rva, symbols):
    addrs = [s[0] for s in symbols]
    idx = bisect.bisect_right(addrs, rva) - 1
    if idx >= 0:
        base_addr, sym = symbols[idx]
        offset = rva - base_addr
        return f"{sym} + 0x{offset:X} (RVA 0x{rva:X})"
    return f"0x{rva:X}"

def process_file(file_path, symbols):
    pattern = re.compile(r'(?:host_)?rva=0x([0-9a-fA-F]+)')
    try:
        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.rstrip()
                match = pattern.search(line)
                if match:
                    rva = int(match.group(1), 16)
                    sym_info = resolve(rva, symbols)
                    print(f"{line}  -->  {sym_info}")
                elif any(k in line for k in ("TASK4", "THREAD-AUDIT", "WATCHDOG", "STACK", "MAIN-DETAIL", "ALL-THREADS", "STAGE-E", "REGISTRATION")):
                    print(line)
    except BrokenPipeError:
        pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: resolve_rva.py <rva_hex> [rva_hex2 ...] OR resolve_rva.py --file <logfile>")
        sys.exit(1)
    syms = load_map()
    if sys.argv[1] == "--file" and len(sys.argv) > 2:
        process_file(sys.argv[2], syms)
    else:
        for arg in sys.argv[1:]:
            val = int(arg, 16) if arg.startswith(("0x", "0X")) else int(arg, 16)
            print(f"0x{val:X} -> {resolve(val, syms)}")
