import sys
import os
import json

sys.path.insert(0, 'ps3recomp/tools')
from ppu_disasm import decode

with open('vfs/PS3_GAME/USRDIR/EBOOT.ELF', 'rb') as f:
    elf = bytearray(f.read())

e_phoff = int.from_bytes(elf[0x20:0x28], 'big')
e_phentsize = int.from_bytes(elf[0x36:0x38], 'big')
e_phnum = int.from_bytes(elf[0x38:0x3A], 'big')
segments = []
for i in range(e_phnum):
    ph_off = e_phoff + i * e_phentsize
    p_type = int.from_bytes(elf[ph_off:ph_off+4], 'big')
    p_offset = int.from_bytes(elf[ph_off+8:ph_off+16], 'big')
    p_vaddr = int.from_bytes(elf[ph_off+16:ph_off+24], 'big')
    p_filesz = int.from_bytes(elf[ph_off+32:ph_off+40], 'big')
    if p_type == 1:
        segments.append((p_vaddr, p_filesz, p_offset))

def read_u32(addr):
    for p_vaddr, p_filesz, p_offset in segments:
        if p_vaddr <= addr < p_vaddr + p_filesz:
            off = addr - p_vaddr + p_offset
            val = int.from_bytes(elf[off:off+4], 'big')
            raw = elf[off:off+4].hex().upper()
            return val, raw, off
    return None, None, None

with open('tools/ppu_switches.json') as f:
    switches = json.load(f)

for s in switches:
    addr = int(s['addr'], 0)
    val, raw, off = read_u32(addr)
    insn = decode(val, addr) if val is not None else None
    dis = (insn.mnemonic + ' ' + insn.operands) if insn else '???'
    
    surr = []
    for delta in (-8, -4, 0, 4, 8):
        va = addr + delta
        v, r, _ = read_u32(va)
        d = decode(v, va) if v is not None else None
        d_str = (d.mnemonic + ' ' + d.operands) if d else '???'
        mark = '==>' if delta == 0 else '   '
        surr.append('%s 0x%08X: %s %s' % (mark, va, r, d_str))
    
    print('=== Switch: %s | Target: %s (%s) ===' % (s['addr'], s['insn'], s.get('note', '')[:50]))
    print('Original: %s (%s)' % (raw, dis))
    print('Surrounding block:')
    for l in surr:
        print('  ' + l)
    print()
