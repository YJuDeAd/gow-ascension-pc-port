#!/usr/bin/env python3
"""
Lift SPU code fragments that a job streams into local store at addresses that
vary from job to job, position-independently.

Some WWS jobs carry their own code: the job's DMA list loads a header, then up
to three code fragments from the PPU executable, packed one after another into
the job's input buffer. Where a fragment lands depends on the sizes of the ones
loaded before it, so the same bytes run from many LS addresses. Each fragment
is lifted once with spu_lifter --pic at --lift-base and registered with
spu_overlay_register_pic(): the MFC GET of the fragment marks it resident at
whatever LS address it landed at, and dispatch runs the lift with
ctx->pic_delta = (that address) - lift base.

Input (--fragments): JSON list of {"ea": "0x71ab60", "span": "0x7c0"}, with an
optional "entries": ["0x71ab88", ...] of further entry EAs inside it (a job that
copies a long stretch of the executable can enter a function in the middle of
it; the fragment then starts at the quadword before). The span
may be an upper bound (fragments are packed back to back in the executable and
their sizes are only known from the jobs' DMA lists): the runtime takes the
real extent from the GET. Its first instruction is the entry.

Usage:
  python tools/lift_spu_pic.py --elf <EBOOT.ELF> --fragments <file.json>
         --out <port>/src/spu/pic --register <port>/src/spu/pic_register.c
         --symbol-prefix <port> [--image-base 300] [--lift-base 0x20000]
"""
import argparse, json, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
from wrap_spu_elf import wrap

LIFTER = os.path.join(os.path.dirname(__file__), "spu_lifter.py")


def ppu_elf_reader(path):
    """Return read(ea, n) over the PT_LOAD segments of a 64-bit BE PPU ELF."""
    f = open(path, "rb").read()
    if f[:4] != b"\x7fELF":
        sys.exit(f"{path}: not an ELF (decrypt EBOOT.BIN to EBOOT.ELF first)")
    phoff = struct.unpack_from(">Q", f, 0x20)[0]
    phes, phn = struct.unpack_from(">HH", f, 0x36)
    segs = []
    for i in range(phn):
        t, _fl, off, va, _pa, fs, _ms = struct.unpack_from(">IIQQQQQ", f, phoff + i * phes)
        if t == 1 and fs:
            segs.append((va, off, fs))

    def read(ea, n):
        for va, off, fs in segs:
            if va <= ea and ea + n <= va + fs:
                return f[off + ea - va: off + ea - va + n]
        sys.exit(f"EA 0x{ea:X}+0x{n:X} is not in a loaded segment of {path}")
    return read


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", required=True, help="The title's decrypted PPU executable.")
    ap.add_argument("--fragments", required=True, help="JSON list, see above.")
    ap.add_argument("--out", required=True, help="Directory to lift into.")
    ap.add_argument("--register", required=True, help="Registration TU to write.")
    ap.add_argument("--symbol-prefix", default="port")
    ap.add_argument("--image-base", type=lambda s: int(s, 0), default=300,
                    help="First image id (default 300, past the job modules' 200+).")
    ap.add_argument("--lift-base", type=lambda s: int(s, 0), default=0x20000,
                    help="LS address every fragment is lifted at (default 0x20000). "
                         "Only PC-relative code can run from a varying address, so "
                         "the choice does not change what the code does.")
    args = ap.parse_args()

    read = ppu_elf_reader(args.elf)
    frags = json.load(open(args.fragments))
    os.makedirs(args.out, exist_ok=True)
    tmp_elf = os.path.join(args.out, "_tmp.elf")
    base = args.lift_base
    lifted = []
    for fr in frags:
        ea, span = int(fr["ea"], 0), int(fr["span"], 0)
        if ea % 16 or base % 16:
            sys.exit(f"fragment 0x{ea:X}: EA and lift base must be quadword aligned")
        raw = read(ea, span)
        name = f"pic_{ea:08X}"
        prefix = name + "_"
        with open(tmp_elf, "wb") as f:
            f.write(wrap(raw, base=base, entry=base))
        extra = [base] + [base + int(e, 0) - ea for e in fr.get("entries", [])
                          if ea <= int(e, 0) < ea + span]
        cmd = [sys.executable, LIFTER, "--auto-functions", tmp_elf, "--base", hex(base),
               "--pic", "--return-entries", "--extra-funcs", ",".join(hex(x) for x in extra),
               "--symbol-prefix", prefix, "-o", os.path.join(args.out, name),
               "--source-name", "spu_recomp.c", "--header-name", "spu_recomp.h"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  FAIL {name}\n{r.stderr}", file=sys.stderr)
            continue
        funcs = unsup = 0
        for line in r.stdout.splitlines():
            s = line.strip()
            if s.endswith("function(s) lifted"):
                funcs = int(s.split()[0])
            if "unsupported instruction(s)" in s:
                unsup = int(s.split()[0])
        print(f"  {name:20} span=0x{span:04X} funcs={funcs:3} unsup={unsup:3}"
              + ("  <-- UNSUPPORTED" if unsup else ""))
        lifted.append((name, prefix, ea, span))
    if os.path.exists(tmp_elf):
        os.remove(tmp_elf)

    L = ["/* GENERATED by tools/lift_spu_pic.py: SPU code fragments a job streams",
         " * into local store at varying addresses, lifted position-independently",
         f" * at LS 0x{base:X} (spu_lifter --pic). */",
         "#include <stdint.h>", "",
         "extern void spu_begin_image(int image_id);",
         "extern void spu_overlay_register_pic(uint32_t content_ea, uint32_t span,"
         " uint32_t lift_base, int image_id);", ""]
    L += [f"extern void {p}spu_recomp_register(void);" for _, p, _, _ in lifted]
    L += ["", f"void {args.symbol_prefix}_pic_register_all(void)", "{"]
    for i, (name, prefix, ea, span) in enumerate(lifted):
        img = args.image_base + i
        L.append(f"    spu_begin_image({img}); {prefix}spu_recomp_register();")
        L.append(f"    spu_overlay_register_pic(0x{ea:08X}u, 0x{span:X}u, 0x{base:X}u, {img});")
    L += ["    spu_begin_image(0);", "}", "",
          f"__attribute__((constructor)) static void {args.symbol_prefix}_pic_register_ctor(void)",
          "{", f"    {args.symbol_prefix}_pic_register_all();", "}", ""]
    os.makedirs(os.path.dirname(os.path.abspath(args.register)), exist_ok=True)
    with open(args.register, "w") as f:
        f.write("\n".join(L))
    print(f"\n{len(lifted)} fragment(s), image ids {args.image_base}.."
          f"{args.image_base + len(lifted) - 1} -> {args.register}")


if __name__ == "__main__":
    main()
