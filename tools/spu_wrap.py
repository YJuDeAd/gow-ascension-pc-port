"""Wrap lifted SPU functions so they can be recorded, replayed and replaced by
native code (host/spu/spu_fnrec.h).

usage: python tools/spu_wrap.py <module dir> <image id> <hex addr> [<hex addr> ...]
       python tools/spu_wrap.py src/spu/jobmod_7868e9b847d1_e6D98 218 53B8 73E0 9090

Each chosen function's definition is renamed <name>_lifted, and a wrapper with
the original name calls spu_fn_dispatch. Callers, the function table and the
header keep using the original name, so nothing else changes. A native version
is used when some host/spu/*.c defines spu_native_<image>_<ADDR8>(spu_context*);
the script looks for those definitions each time it runs.

Idempotent: run it again with a different list (or after re-lifting) and the
wrapper block is rebuilt. Run with no addresses to remove every wrapper.

The block also tells spu_fnrec.c how the image was built: a hash of the lifted
source (as lifted, before wrapping) and the float mode the file compiles with
(spu_fnrec_note_image; the FNR2 recordings carry both as the build id).
"""
import os
import re
import sys

BEGIN = "/* ---- tools/spu_wrap.py: wrappers (begin) ---- */"
END = "/* ---- tools/spu_wrap.py: wrappers (end) ---- */"
INCLUDE = '#include "spu_fnrec.h"   /* tools/spu_wrap.py */'


def natives(root, image):
    found = set()
    hdir = os.path.join(root, "host", "spu")
    for dirpath, _, files in os.walk(hdir):
        for fn in files:
            if fn.endswith(".c"):
                txt = open(os.path.join(dirpath, fn), encoding="utf-8", errors="replace").read()
                found.update(int(a, 16) for a in re.findall(
                    r"^void\s+spu_native_%d_([0-9A-Fa-f]{8})\s*\(\s*spu_context\s*\*" % image, txt, re.M))
    return found


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mdir, image = sys.argv[1], int(sys.argv[2])
    addrs = [int(a, 16) for a in sys.argv[3:]]
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    path = os.path.join(mdir, "spu_recomp.c")
    src = open(path, encoding="utf-8", errors="replace").read()
    prefix = os.path.basename(os.path.normpath(mdir)) + "_spu_func_"

    # Undo a previous run: wrapper block out, _lifted names back.
    if BEGIN in src:
        src = src[:src.index(BEGIN)].rstrip("\n") + "\n" + src[src.index(END) + len(END):].lstrip("\n")
    src = re.sub(r"^void (" + re.escape(prefix) + r"[0-9A-F]{8})_lifted\(spu_context\* ctx\) \{",
                 r"void \1(spu_context* ctx) {", src, flags=re.M)
    src = src.replace(INCLUDE + "\n", "")

    h = 0xCBF29CE484222325
    for b in src.encode("utf-8"):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    nat = natives(root, image)
    block = []
    for a in addrs:
        name = "%s%08X" % (prefix, a)
        head = "void %s(spu_context* ctx) {" % name
        if src.count(head) != 1:
            sys.exit("no single definition of %s" % name)
        src = src.replace(head, "void %s_lifted(spu_context* ctx) {" % name)
        block.append("static spu_fn_desc s_fnw_%08X = { %d, 0x%Xu, \"%s\", %s_lifted, 0, -1, 0, 0 };"
                     % (a, image, a, name, name))
        block.append("void %s(spu_context* ctx) { spu_fn_dispatch(&s_fnw_%08X, ctx); }" % (name, a))

    if addrs:
        block.append("__attribute__((constructor)) static void s_fnw_note_image(void) "
                     "{ spu_fnrec_note_image(%d, \"%016X\", SPU_FNREC_FLOAT_MODE); }" % (image, h))
        # The include goes after the file's first #include line.
        m = re.search(r"^#include [^\n]*\n", src, re.M)
        src = src[:m.end()] + INCLUDE + "\n" + src[m.end():]
        src = src.rstrip("\n") + "\n\n" + BEGIN + "\n" + "\n".join(block) + "\n" + END + "\n"
    open(path, "w", encoding="utf-8", newline="\n").write(src)
    print("%s: %d wrapped (%d with a native version: %s)" % (
        path, len(addrs), len([a for a in addrs if a in nat]),
        ", ".join("%X" % a for a in addrs if a in nat) or "none"))


if __name__ == "__main__":
    main()
