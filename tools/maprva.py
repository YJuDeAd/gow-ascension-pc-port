"""Resolve rva=0x... in a profile dump against an lld-link /MAP file.

usage: python tools/maprva.py build/win/AscensionRecomp.map <profile.txt>

The profile is what PS3_DEBUG's `prof <tid> <s>` writes to <file>.out.
"""
import bisect
import re
import sys

BASE = 0x140000000


def load(path):
    syms = []
    pat = re.compile(r'^\s*0001:[0-9a-f]+\s+(\S+)\s+([0-9a-f]{16})\s+(?:f\s+)?(\S+)', re.I)
    for line in open(path, encoding='latin1'):
        m = pat.match(line)
        if m:
            syms.append((int(m.group(2), 16) - BASE, m.group(1), m.group(3)))
    syms.sort()
    return syms


def demangle(s):
    m = re.match(r'\?([A-Za-z_0-9]+)@', s)
    return m.group(1) if m else s


def main():
    syms = load(sys.argv[1])
    keys = [s[0] for s in syms]
    for line in open(sys.argv[2], encoding='latin1'):
        line = line.rstrip('\n')
        m = re.search(r'rva=0x([0-9A-Fa-f]+)', line)
        if m:
            rva = int(m.group(1), 16)
            i = bisect.bisect_right(keys, rva) - 1
            if i >= 0:
                a, name, obj = syms[i]
                line += f'  {demangle(name)}+0x{rva - a:X}  [{obj}]'
        print(line)


main()
