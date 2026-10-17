import struct
import json

with open('vfs/PS3_GAME/USRDIR/EBOOT.ELF', 'rb') as f:
    elf = f.read()

def va_to_offset(va):
    # Seg 0: 0x10000 .. 0x7F33E8 -> offset va - 0x10000
    # Seg 1: 0x800000 .. 0x83BF70 -> offset va - 0x10000
    if 0x10000 <= va < 0x800000:
        return va - 0x10000
    elif 0x800000 <= va < 0x900000:
        return va - 0x10000
    return None

def read_u32(va):
    off = va_to_offset(va)
    if off is None or off + 4 > len(elf):
        return None
    return struct.unpack('>I', elf[off:off+4])[0]

def read_str(va):
    off = va_to_offset(va)
    if off is None or off >= len(elf):
        return ''
    end = elf.find(b'\x00', off)
    if end == -1: end = off + 100
    return elf[off:end].decode('latin1', errors='replace')

toc = 0x0081EF60
offsets = [-0x5FEC, -0x5F38, -0x5F30, -0x5F98, -0x5F34, -0x5F90]
print("=== TOC OFFSETS IN func_0061B918 ===")
for o in offsets:
    addr = toc + o
    val = read_u32(addr)
    s = read_str(val) if val else ''
    print(f"TOC {o:+05x} (0x{addr:08x}) -> 0x{val:08x}: '{s}'")

print("\n=== FINDING FUNCTIONS FOR 0x006245B8 (brbDriver) and 0x0062308C (brbClient) ===")
with open('analysis/functions.json', 'r') as f:
    funcs = json.load(f)

for target, name in [(0x006245B8, 'brbDriver LR'), (0x0062308C, 'brbClient LR'), (0x00816DA8, 'brbDriver entry OPD'), (0x00816DB0, 'brbClient entry OPD')]:
    if target >= 0x800000:
        # It's an OPD: func_addr = read_u32(target), toc = read_u32(target+4)
        f_addr = read_u32(target)
        f_toc = read_u32(target+4)
        print(f"OPD 0x{target:08x} -> func 0x{f_addr:08x}, TOC 0x{f_toc:08x}")
        target = f_addr

    matched = None
    for fn in funcs:
        start = int(fn['start'], 16) if isinstance(fn['start'], str) else fn['start']
        end = int(fn['end'], 16) if isinstance(fn['end'], str) else fn['end']
        if start <= target < end:
            matched = fn
            break
    if matched:
        print(f"{name} 0x{target:08x} is in function [0x{matched['start']} .. 0x{matched['end']}]")
    else:
        print(f"{name} 0x{target:08x} not found in functions.json")

print("\n=== FINDING CALLS AND STRINGS IN 0x00466B88 ===")
import re
off = va_to_offset(0x00466B88)
chunk = elf[off:off+0x200]
for m in re.finditer(rb'[A-Za-z0-9_/:. -]{5,}', chunk):
    print(f"0x{off + m.start() + 0x10000:08x}: {m.group(0).decode('latin1')}")

# Also check what functions 0x00466B88 calls or references:
# bl 0x6288DC
for target, name in [(0x00466B88, '0x466B88'), (0x006288DC, '0x6288DC')]:
    for fn in funcs:
        start = int(fn['start'], 16) if isinstance(fn['start'], str) else fn['start']
        end = int(fn['end'], 16) if isinstance(fn['end'], str) else fn['end']
        if start <= target < end:
            print(f"{name} is in function [0x{start:08x} .. 0x{end:08x}]")

print("\n=== STRINGS REFERENCED IN CALLER 0x005FFC80 ===")
for va in range(0x005FFC80, 0x00600900, 4):
    w = read_u32(va)
    if w is None: continue
    if (w >> 26) == 32 and ((w >> 16) & 0x1F) == 2:
        disp = w & 0xFFFF
        if disp & 0x8000: disp -= 0x10000
        ptr = read_u32(toc + disp)
        s = read_str(ptr) if ptr else ''
        s_safe = s.encode('ascii', errors='replace').decode('ascii')
        if len(s_safe) > 3:
            print(f"0x{va:08x}: TOC {disp:+05x} -> 0x{ptr:08x}: \"{s_safe}\"")




