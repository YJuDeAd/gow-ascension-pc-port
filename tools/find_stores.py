import struct
import json

with open('vfs/PS3_GAME/USRDIR/EBOOT.ELF', 'rb') as f:
    elf = f.read()

def va_to_offset(va):
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

# In PowerPC:
# stw rx, 0x24(ry) is opcode 36 (0x90000000):
# opcode: 36 (6 bits: 100100 -> 0x90)
# rx (5 bits), ry (5 bits), disp (16 bits: 0x0024)
# Form: (36 << 26) | (rx << 21) | (ry << 16) | 0x0024

print("Searching for stw *, 0x24(*) in range 0x610000 .. 0x630000 (brb code)...")
for va in range(0x610000, 0x630000, 4):
    w = read_u32(va)
    if w is None: continue
    op = (w >> 26) & 0x3F
    disp = w & 0xFFFF
    if op == 36 and disp == 0x0024: # stw
        rx = (w >> 21) & 0x1F
        ry = (w >> 16) & 0x1F
        print(f"0x{va:08x}: stw r{rx}, 0x24(r{ry})")
    elif op == 37 and disp == 0x0024: # stwu
        rx = (w >> 21) & 0x1F
        ry = (w >> 16) & 0x1F
        print(f"0x{va:08x}: stwu r{rx}, 0x24(r{ry})")
    elif op == 31: # possibly stwx
        xo = (w >> 1) & 0x3FF
        if xo == 151: # stwx
            pass
