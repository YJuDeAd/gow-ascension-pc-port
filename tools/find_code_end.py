#!/usr/bin/env python3
"""
find_code_end.py — Finds the boundary where executable code ends and read-only data begins in a PS3 ELF.
Emits the exact hex address boundary to pass as `--code-end 0xXXXXXXXX` to ppu_lifter.py.
"""
import sys
import struct
from pathlib import Path

def parse_elf_sections(elf_path):
    with open(elf_path, 'rb') as f:
        ident = f.read(16)
        if ident[:4] != b'\x7fELF':
            raise ValueError(f"Not an ELF file: {elf_path}")
        
        is_64bit = ident[4] == 2
        is_big_endian = ident[5] == 2
        endian = '>' if is_big_endian else '<'

        if not is_64bit:
            raise ValueError("Expected ELF64 (PS3 PPC64)")

        # Read ELF header (64 bytes)
        f.seek(0)
        hdr = f.read(64)
        entry = struct.unpack(endian + 'Q', hdr[24:32])[0]
        phoff = struct.unpack(endian + 'Q', hdr[32:40])[0]
        shoff = struct.unpack(endian + 'Q', hdr[40:48])[0]
        phnum = struct.unpack(endian + 'H', hdr[56:58])[0]
        shnum = struct.unpack(endian + 'H', hdr[60:62])[0]
        shstrndx = struct.unpack(endian + 'H', hdr[62:64])[0]

        # Read section string table header
        f.seek(shoff + shstrndx * 64)
        shstr_hdr = f.read(64)
        str_offset = struct.unpack(endian + 'Q', shstr_hdr[24:32])[0]
        str_size = struct.unpack(endian + 'Q', shstr_hdr[32:40])[0]

        # Read section string table
        f.seek(str_offset)
        shstrtab = f.read(str_size)

        sections = []
        for i in range(shnum):
            f.seek(shoff + i * 64)
            shdr = f.read(64)
            name_idx = struct.unpack(endian + 'I', shdr[0:4])[0]
            stype = struct.unpack(endian + 'I', shdr[4:8])[0]
            flags = struct.unpack(endian + 'Q', shdr[8:16])[0]
            addr = struct.unpack(endian + 'Q', shdr[16:24])[0]
            offset = struct.unpack(endian + 'Q', shdr[24:32])[0]
            size = struct.unpack(endian + 'Q', shdr[32:40])[0]

            name = shstrtab[name_idx:].split(b'\x00')[0].decode('latin1')
            sections.append({
                'index': i,
                'name': name,
                'type': stype,
                'flags': flags,
                'addr': addr,
                'offset': offset,
                'size': size,
                'end': addr + size
            })

        return entry, sections

def main():
    if len(sys.argv) < 2:
        print("Usage: python find_code_end.py <path_to_EBOOT.ELF>")
        sys.exit(1)

    elf_path = Path(sys.argv[1])
    if not elf_path.exists():
        print(f"Error: file not found: {elf_path}")
        sys.exit(1)

    entry, sections = parse_elf_sections(elf_path)
    print(f"ELF: {elf_path.name}")
    print(f"Entry point: 0x{entry:08X}\n")

    print(f"{'Idx':<4} {'Name':<22} {'Addr':<12} {'End':<12} {'Size':<12} {'Flags'}")
    print("-" * 72)

    SHF_WRITE = 0x1
    SHF_ALLOC = 0x2
    SHF_EXECINSTR = 0x4

    last_exec_sec = None
    first_rodata_sec = None

    for s in sections:
        flags_str = []
        if s['flags'] & SHF_ALLOC: flags_str.append('A')
        if s['flags'] & SHF_WRITE: flags_str.append('W')
        if s['flags'] & SHF_EXECINSTR: flags_str.append('X')
        fstr = "".join(flags_str)

        print(f"{s['index']:<4} {s['name']:<22} 0x{s['addr']:08X} 0x{s['end']:08X} 0x{s['size']:08X} {fstr}")

        if s['flags'] & SHF_EXECINSTR:
            last_exec_sec = s
        elif last_exec_sec and not first_rodata_sec and (s['flags'] & SHF_ALLOC) and not (s['flags'] & SHF_WRITE):
            first_rodata_sec = s

    print("\n" + "=" * 72)
    if last_exec_sec:
        code_end = last_exec_sec['end']
        print(f"Last executable section: {last_exec_sec['name']} (0x{last_exec_sec['addr']:08X} - 0x{last_exec_sec['end']:08X})")
        if first_rodata_sec:
            print(f"First rodata section:     {first_rodata_sec['name']} (0x{first_rodata_sec['addr']:08X} - 0x{first_rodata_sec['end']:08X})")
        print(f"\nRecommended --code-end boundary: 0x{code_end:08X}")
        print(f"CLI flag for ppu_lifter.py:      --code-end 0x{code_end:08X}")
    else:
        print("Warning: No executable sections found.")

if __name__ == '__main__':
    main()
