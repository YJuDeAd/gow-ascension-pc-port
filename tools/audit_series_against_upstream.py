#!/usr/bin/env python3
import os
import subprocess
import glob
import re

REPO_DIR = os.path.abspath(r"..\gow-port-latest\ps3recomp")
PATCH_DIR = os.path.abspath(r"patches\series")

patch_files = sorted(glob.glob(os.path.join(PATCH_DIR, "*.patch")))

print(f"Total patch files found: {len(patch_files)}")
print(f"Target repository: {REPO_DIR}")

def get_patch_subject(patch_path):
    with open(patch_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("Subject:"):
                # strip Subject: [PATCH...] 
                subj = re.sub(r"^Subject:\s*(\[[^\]]+\])?\s*", "", line).strip()
                return subj
    return os.path.basename(patch_path)

def find_upstream_commit(subject):
    if not subject:
        return None
    # Search for subject in git log
    # clean subject of common prefixes
    clean_subj = subject
    for prefix in ["Apply ps3recomp-", ".patch", "feat:", "fix:", "refactor:", "perf:"]:
        clean_subj = clean_subj.replace(prefix, "")
    clean_subj = clean_subj.strip()
    
    # Try exact or substring search
    query = clean_subj[:40]
    res = subprocess.run(["git", "-C", REPO_DIR, "log", "--grep=" + query, "--oneline", "-n", "1"],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.stdout.strip():
        return res.stdout.strip()
    return None

results = []
for p in patch_files:
    fname = os.path.basename(p)
    subj = get_patch_subject(p)
    upstream = find_upstream_commit(subj)
    results.append({
        "file": fname,
        "subject": subj,
        "upstream": upstream
    })
    print(f"{fname[:35]:35s} | Upstream: {upstream if upstream else 'NOT FOUND'}")

with open(r"docs\reports\upstream_patch_match.json", "w", encoding="utf-8") as f:
    import json
    json.dump(results, f, indent=2)

