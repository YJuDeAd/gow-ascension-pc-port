#!/usr/bin/env python3
"""Extract WWS SPU job code modules (C0DEC0DE) directly from God of War: Ascension's EBOOT.ELF.

All 33 Wws job modules are embedded inside the executable at file offsets
0x006A2480..0x0081B780.
Output: <outdir>/jobmod_<sha1[:12]>_e<entryOffset>.bin
"""
import argparse
import hashlib
import os
import re
import struct
import sys

# Exact verified byte boundaries for the 23 modules referenced in jobmods_extra_funcs.json
KNOWN_SIZES = {
    0x006A2480: 0x2790,
    0x006A4C80: 0x15A0,
    0x006A6280: 0xCE30,
    0x006B3100: 0x13560,
    0x006CA900: 0x29B0,
    0x006CFF80: 0x400,
    0x006DC180: 0x83E0,
    0x006E4580: 0x4EA0,
    0x006EB300: 0xD000,  # Module 218 (geometry/drawcmd)
    0x006FB380: 0xD90,
    0x006FC180: 0xC10,
    0x006FEE80: 0x4440,
    0x00751880: 0x8130,
    0x00759A00: 0x7040,
    0x00760A80: 0x1E30,
    0x00762900: 0x9840,
    0x0076C180: 0xC00,
    0x0076FD80: 0x13BC0,
    0x00784C00: 0x22000,
    0x007A6B80: 0x1D800,
    0x007C4200: 0x18290,
    0x007DD580: 0x6000,
    0x0081B780: 0xAA70,
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", default="vfs/PS3_GAME/USRDIR/EBOOT.ELF", help="Path to EBOOT.ELF")
    ap.add_argument("--out", default="build/jobmods", help="Output directory for jobmod_*.bin")
    args = ap.parse_args()

    if not os.path.exists(args.elf):
        sys.exit(f"ELF not found: {args.elf}")

    with open(args.elf, "rb") as f:
        data = f.read()

    matches = [m.start() for m in re.finditer(b"\xc0\xde\xc0\xde", data)]
    starts = [m - 0x20 for m in matches]
    print(f"Found {len(starts)} Wws job module signatures (0xC0DEC0DE) in {args.elf}")

    os.makedirs(args.out, exist_ok=True)
    for idx, start in enumerate(starts):
        hdr = data[start:start + 0x30]
        entry_off = struct.unpack_from(">I", hdr, 0x10)[0]
        if start in KNOWN_SIZES:
            sz = KNOWN_SIZES[start]
        else:
            next_start = starts[idx + 1] if idx + 1 < len(starts) else len(data)
            chunk = data[start:next_start]
            non_zeros = [i for i, b in enumerate(chunk) if b != 0]
            last_nz = non_zeros[-1] if non_zeros else 0
            sz = (last_nz + 1 + 15) & ~15
        blob = data[start:start + sz]
        sha = hashlib.sha1(blob).hexdigest()[:12]
        fn = f"jobmod_{sha}_e{entry_off:04X}.bin"
        out_path = os.path.join(args.out, fn)
        with open(out_path, "wb") as f:
            f.write(blob)
        print(f"  [{idx:02d}] 0x{start:08X} (VA 0x{start+0x10000:08X}) -> {fn} ({sz} bytes)")

    print(f"Extracted {len(starts)} modules to {args.out}")


if __name__ == "__main__":
    main()
