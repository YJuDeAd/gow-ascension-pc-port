"""Put the latest dumped frame next to the PS3 reference of the main menu.

    python tools/compare_menu.py <frame-dump dir> <out.png> [reference.png]

The frame is the newest .ppm in the dump directory (LD_FRAME_DUMP); the
reference defaults to captures/referencia/referencia.png. Both are scaled to
640x360 and placed side by side, ours on the left.
"""
import glob
import os
import sys

from PIL import Image

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dump_dir, out = sys.argv[1], sys.argv[2]
ref = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, "captures", "referencia", "referencia.png")
frames = sorted(glob.glob(os.path.join(dump_dir, "*.ppm")))
if not frames:
    sys.exit("no frames in " + dump_dir)
sheet = Image.new("RGB", (1280, 360))
sheet.paste(Image.open(frames[-1]).convert("RGB").resize((640, 360)), (0, 0))
if os.path.exists(ref):
    r = Image.open(ref).convert("RGB")
    w, h = r.size
    # the reference is a video grab with a border; keep the 16:9 picture
    r = r.crop((int(w * 0.012), int(h * 0.03), int(w * 0.988), int(h * 0.985)))
    sheet.paste(r.resize((640, 360)), (640, 0))
sheet.save(out)
print(os.path.basename(frames[-1]), "->", out)
