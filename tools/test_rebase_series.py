#!/usr/bin/env python3
import os
import subprocess
import glob
import re

REPO_DIR = os.path.abspath(r"..\gow-port-latest\ps3recomp")
SERIES_DIR = os.path.abspath(r"patches\series")

patch_files = sorted(glob.glob(os.path.join(SERIES_DIR, "*.patch")))

print(f"Auditing {len(patch_files)} patches against {REPO_DIR}")

# Ensure clean git state on port-series branch
subprocess.run(["git", "-C", REPO_DIR, "am", "--abort"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run(["git", "-C", REPO_DIR, "reset", "--hard", "origin/master"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

audit_report = []

for idx, p in enumerate(patch_files, 1):
    fname = os.path.basename(p)
    
    # Read subject
    subj = ""
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("Subject:"):
                subj = re.sub(r"^Subject:\s*(\[[^\]]+\])?\s*", "", line).strip()
                break
    if not subj:
        subj = fname

    # 1. Check if git apply --check -R succeeds (i.e. all changes already exist in working copy)
    res_rev = subprocess.run(["git", "-C", REPO_DIR, "apply", "--check", "--reverse", p],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    already_in_tree = (res_rev.returncode == 0)

    # 2. Search git log for matching PR or subject
    pr_match = re.search(r"#(\d+)", subj)
    upstream_commit = None
    if pr_match:
        pr_num = pr_match.group(1)
        res_log = subprocess.run(["git", "-C", REPO_DIR, "log", "--grep=#" + pr_num, "--oneline", "-n", "1"],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_log.stdout.strip():
            upstream_commit = res_log.stdout.strip()
    
    if not upstream_commit:
        # Search first 30 chars of subject
        clean_s = re.sub(r"^(Apply ps3recomp-\d+-|\.patch|feat:|fix:|refactor:|perf:)", "", subj).strip()
        if len(clean_s) > 10:
            res_log = subprocess.run(["git", "-C", REPO_DIR, "log", "--grep=" + clean_s[:30], "--oneline", "-n", "1"],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if res_log.stdout.strip():
                upstream_commit = res_log.stdout.strip()

    # 3. Try git am --3way
    res_am = subprocess.run(["git", "-C", REPO_DIR, "am", "--3way", p],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    status = "UNKNOWN"
    details = ""

    if res_am.returncode == 0:
        status = "APPLIED_CLEAN"
        details = "Applied cleanly via git am --3way"
    else:
        # Check why it failed
        err = res_am.stderr + res_am.stdout
        if "Patch is empty" in err or "previous cherry-pick" in err or already_in_tree:
            status = "PRESENT_UPSTREAM"
            details = f"Changes already present upstream ({upstream_commit if upstream_commit else 'verified via diff'})"
            subprocess.run(["git", "-C", REPO_DIR, "am", "--abort"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elif "Patch failed at" in err or "conflict" in err.lower():
            # Check what files conflicted
            res_status = subprocess.run(["git", "-C", REPO_DIR, "status", "--porcelain"],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            conflict_files = [l.split()[-1] for l in res_status.stdout.splitlines() if l.startswith("UU") or l.startswith("U ") or l.startswith(" U")]
            status = "CONFLICT"
            details = f"Conflict in: {', '.join(conflict_files) if conflict_files else 'unmerged hunks'}"
            subprocess.run(["git", "-C", REPO_DIR, "am", "--abort"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            status = "FAILED"
            details = err.strip().splitlines()[-1] if err.strip() else "Unknown failure"
            subprocess.run(["git", "-C", REPO_DIR, "am", "--abort"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print(f"[{idx:02d}/{len(patch_files)}] {fname[:40]:40s} | {status:16s} | {details}")
    audit_report.append({
        "index": idx,
        "file": fname,
        "subject": subj,
        "status": status,
        "upstream_commit": upstream_commit,
        "details": details
    })

import json
with open("docs/reports/rebase_audit_report.json", "w", encoding="utf-8") as f:
    json.dump(audit_report, f, indent=2)

print("\nAudit complete! Saved to docs/reports/rebase_audit_report.json")
