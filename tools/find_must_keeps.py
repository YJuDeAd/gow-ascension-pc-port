#!/usr/bin/env python3
import os
import glob
import re

MUST_KEEPS = {
    "Edge Zlib HLE": ["edge_inflate.cpp", "PS3_EDGE_ZLIB_HLE", "_cellSpursLFQueuePushBody"],
    "SPURS_MAXCONT / READY_LEVEL": ["SPURS_MAXCONT", "READY_LEVEL"],
    "10 GB free-space floor": ["10 * 1024", "10ULL * 1024", "free-space floor", "0x280000000ULL"],
    "hdd0 writable + hdd1 ENOENT": ["/dev_hdd0", "/dev_hdd1", "ENOENT"],
    "getenv caching": ["getenv_cached", "getenv"],
    "per-context SN-defer": ["spu_sn_defer", "sn_defer"],
    "SPU shadow return stack": ["shadow_stack", "return_stack", "SPU shadow"],
    "window title wide APIs": ["SetWindowTextW", "window_title", "title_w"],
    "overlay (ld_perf_overlay)": ["ld_perf_overlay", "perf_overlay"],
    "GCM unmap/offset-table fixes": ["gcm_offset_table", "cellGcmUnmapIoAddress", "gcm_io_offset_to_ea"],
    "0x9D4 selectWorkload": ["0x9D4", "selectWorkload", "poll_select"]
}

patches = sorted(glob.glob("patches/series/*.patch"))

for item, patterns in MUST_KEEPS.items():
    matching_patches = []
    for p in patches:
        with open(p, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()
            if any(pat in content for pat in patterns):
                matching_patches.append(os.path.basename(p))
    print(f"\n=== MUST-KEEP: {item} ===")
    print("Found in patches:")
    for mp in matching_patches:
        print(f"  - {mp}")
