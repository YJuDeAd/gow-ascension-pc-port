#!/usr/bin/env python3
"""Extract a SPURS policy module that the executable carries as raw SPU code
(not as an embedded SPU ELF) and wrap it as an SPU ELF loaded at LS 0xA00,
where SPURS loads policy modules, so build_spu_workloads.py lifts it and
registers it under the fingerprint of its code (the ".text" entry).

God of War: Ascension's Wws job system: 12288 bytes at 0x0077CA80, whose
FNV-1a-64 is 0xC8D774963A96DCF2 (what cellSpurs prints for "PM NOT LIFTED").

    python tools/extract_spu_pm.py ../work/input/EBOOT.elf meta/spu/spu_wws_pm_at_0077CA80.elf \\
        --address 0x77CA80 --size 12288 --fnv 0xC8D774963A96DCF2
"""
import argparse
import os
import struct
import subprocess
import sys


def fnv1a64(data):
    h = 1469598103934665603
    for x in data:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def read_va(elf, va, size):
    phoff = struct.unpack('>Q', elf[0x20:0x28])[0]
    for i in range(struct.unpack('>H', elf[0x38:0x3A])[0]):
        t, _, off, vaddr, _, filesz, _, _ = struct.unpack('>IIQQQQQQ', elf[phoff + i * 56:phoff + i * 56 + 56])
        if t == 1 and vaddr <= va and va + size <= vaddr + filesz:
            return elf[off + va - vaddr:off + va - vaddr + size]
    sys.exit(f'0x{va:X}+{size} is not inside a PT_LOAD segment')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('elf')
    ap.add_argument('out')
    ap.add_argument('--address', type=lambda s: int(s, 0), required=True)
    ap.add_argument('--size', type=lambda s: int(s, 0), required=True)
    ap.add_argument('--fnv', type=lambda s: int(s, 0), help='expected FNV-1a-64 of the bytes')
    ap.add_argument('--base', type=lambda s: int(s, 0), default=0xA00)
    a = ap.parse_args()

    blob = read_va(open(a.elf, 'rb').read(), a.address, a.size)
    h = fnv1a64(blob)
    print(f'{a.size} bytes at 0x{a.address:X}, FNV-1a-64 0x{h:016X}')
    if a.fnv is not None and h != a.fnv:
        sys.exit(f'fingerprint differs from the expected 0x{a.fnv:016X}: another release of the executable?')
    raw = a.out + '.bin'
    open(raw, 'wb').write(blob)
    top = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
    kit = 'ps3kit' if os.path.isdir(os.path.join(top, 'ps3kit')) else 'ps3recomp'   # the toolkit beside this repo
    wrap = os.path.join(top, kit, 'tools', 'wrap_spu_elf.py')
    subprocess.run([sys.executable, wrap, raw, '--base', hex(a.base), '--entry', hex(a.base), '--out', a.out], check=True)
    os.remove(raw)


if __name__ == '__main__':
    main()
