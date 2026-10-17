#!/usr/bin/env python3
"""
tools/nid_triage.py — Cross-reference imports.json against ps3recomp runtime coverage
and categorize imports into COVERED, PSN (offline stub), and MISSING.

Usage:
    python ./tools/nid_triage.py [--json report.json] [--verbose]
"""

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "ps3recomp", "tools"))

try:
    from nid_database import NIDDatabase, compute_nid
except ImportError:
    NIDDatabase = None
    compute_nid = None

# Modules and prefixes that belong to online / PSN services that should be stubbed to CELL_OK
PSN_MODULES = {
    "sceNp",
    "sceNp2",
    "sceNpCommerce2",
    "sceNpTrophy",
    "sceNpTus",
    "sceNpUtil",
    "cellVoice",
    "cellSysutilNpEula",
    "cellSysutilAvc2",
    "cellSysutilAvconfExt",
}

def load_imports(imports_path):
    with open(imports_path, "r", encoding="utf-8") as f:
        return json.load(f)

def load_registered_nids(hle_nids_path, runtime_dir=None):
    """Extract all NIDs registered in ppu_hle_nids.cpp and runtime cpp files."""
    registered = set()
    name_map = {}

    paths = [hle_nids_path]
    if runtime_dir:
        paths.extend([
            os.path.join(runtime_dir, "ppu", "ppu_sysprx.cpp"),
            os.path.join(runtime_dir, "ppu", "ppu_fs.cpp"),
            os.path.join(runtime_dir, "ppu", "ppu_loader.cpp"),
            os.path.join(runtime_dir, "ppu", "ppu_hle.cpp"),
        ])

    for path in paths:
        if not path or not os.path.isfile(path):
            continue
        with open(path, "r", encoding="latin1") as f:
            text = f.read()

        # Match ps3_hle_register(0x...u, "name", ...)
        for m in re.finditer(r'ps3_hle_register(?:_ctx)?\s*\(\s*([^,]+),\s*"([^"]+)"', text):
            arg = m.group(1).strip().rstrip('uU')
            name = m.group(2).strip()
            val = None
            if arg.startswith('0x') or arg.startswith('0X'):
                try:
                    val = int(arg, 16)
                except ValueError:
                    pass
            elif compute_nid and ("compute_nid" in arg or "ps3_compute_nid" in arg):
                val = compute_nid(name)

            if val is not None:
                registered.add(val)
                name_map[val] = name

    return registered, name_map

def main():
    parser = argparse.ArgumentParser(description="Triage God of War: Ascension imports against ps3recomp")
    parser.add_argument("--imports", default=os.path.join(ROOT, "imports.json"), help="Path to imports.json")
    parser.add_argument("--hle-nids", default=os.path.join(ROOT, "src", "gen", "ppu_hle_nids.cpp"), help="Path to ppu_hle_nids.cpp")
    parser.add_argument("--json", dest="json_out", default=None, help="Save JSON report to path")
    parser.add_argument("--verbose", "-v", action="store_true", help="Print detailed per-NID list")
    args = parser.parse_args()

    imports = load_imports(args.imports)
    runtime_dir = os.path.join(ROOT, "ps3recomp", "runtime")
    hle_nids, hle_names = load_registered_nids(args.hle_nids, runtime_dir=runtime_dir)

    # Initialize NID database
    db = None
    if NIDDatabase:
        db = NIDDatabase()
        db.load_builtins()
        db.load_implemented()
        protos_json = os.path.join(ROOT, "ps3recomp", "tools", "nid_from_protos.json")
        if os.path.isfile(protos_json):
            db.load_json(protos_json)

    categorized = {
        "COVERED": [],
        "PSN": [],
        "MISSING": []
    }

    lib_stats = {}

    for item in imports:
        nid_val = int(item["nid"], 16)
        lib = item["library"]
        stub = item.get("stub", "")

        # Try to find symbol name
        name = hle_names.get(nid_val)
        if not name and db:
            entry = db.lookup_nid(nid_val)
            if entry:
                name = entry[1] if isinstance(entry, (tuple, list)) else getattr(entry, 'name', str(entry))

        info = {
            "nid": item["nid"],
            "nid_val": nid_val,
            "library": lib,
            "name": name or "(unknown)",
            "stub": stub
        }

        lib_stats.setdefault(lib, {"total": 0, "covered": 0, "psn": 0, "missing": 0})
        lib_stats[lib]["total"] += 1

        if nid_val in hle_nids:
            categorized["COVERED"].append(info)
            lib_stats[lib]["covered"] += 1
        elif lib in PSN_MODULES or lib.startswith("sceNp") or lib.startswith("cellVoice"):
            categorized["PSN"].append(info)
            lib_stats[lib]["psn"] += 1
        else:
            categorized["MISSING"].append(info)
            lib_stats[lib]["missing"] += 1

    total = len(imports)
    c_count = len(categorized["COVERED"])
    p_count = len(categorized["PSN"])
    m_count = len(categorized["MISSING"])

    print("=" * 78)
    print(f" GOD OF WAR: ASCENSION — NID TRIAGE REPORT")
    print("=" * 78)
    print(f" Total Imports Extracted:  {total}")
    print(f"   [+] COVERED (in runtime): {c_count:3d} ({c_count*100.0/total:5.1f}%)")
    print(f"   [*] PSN (to be stubbed):  {p_count:3d} ({p_count*100.0/total:5.1f}%)")
    print(f"   [-] MISSING (needs HLE):  {m_count:3d} ({m_count*100.0/total:5.1f}%)")
    effective_cov = (c_count + p_count) * 100.0 / total
    print(f" Effective Coverage (Runtime + PSN Stubs): {effective_cov:5.1f}%\n")

    print(f"{'Library':<24} {'Total':>6} {'Covered':>8} {'PSN':>6} {'Missing':>8}")
    print("-" * 56)
    for lib, st in sorted(lib_stats.items(), key=lambda x: -x[1]["total"]):
        print(f"{lib:<24} {st['total']:>6} {st['covered']:>8} {st['psn']:>6} {st['missing']:>8}")
    print("-" * 56)

    if m_count > 0:
        print(f"\nMissing Imports by Library ({m_count} items):")
        missing_by_lib = {}
        for m in categorized["MISSING"]:
            missing_by_lib.setdefault(m["library"], []).append(m)
        for lib, items in sorted(missing_by_lib.items(), key=lambda x: -len(x[1])):
            print(f"  {lib} ({len(items)}):")
            for it in items:
                print(f"    - {it['nid']} -> {it['name']} (stub @ {it['stub']})")

    if args.verbose and p_count > 0:
        print(f"\nPSN Imports to be Stubbed ({p_count} items):")
        for it in categorized["PSN"]:
            print(f"    - {it['library']:<16} {it['nid']} -> {it['name']}")

    if args.json_out:
        out_data = {
            "total": total,
            "covered_count": c_count,
            "psn_count": p_count,
            "missing_count": m_count,
            "effective_coverage_pct": effective_cov,
            "libraries": lib_stats,
            "categorized": categorized
        }
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(out_data, f, indent=2)
        print(f"\nWrote JSON triage report to {args.json_out}")

if __name__ == "__main__":
    main()
