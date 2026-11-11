#!/usr/bin/env python3
import os
import subprocess
import glob
import re
import json

REPO_DIR = os.path.abspath(r"..\gow-port-latest\ps3recomp")
SERIES_DIR = os.path.abspath(r"patches\series")

patch_files = sorted(glob.glob(os.path.join(SERIES_DIR, "*.patch")))

print(f"Total patches to evaluate: {len(patch_files)}")

# Reset port-series to origin/master
subprocess.run(["git", "-C", REPO_DIR, "am", "--abort"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run(["git", "-C", REPO_DIR, "reset", "--hard", "origin/master"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

results = []

def get_patch_info(p):
    fname = os.path.basename(p)
    subj = ""
    author = ""
    is_mbox = False
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        first_few = [f.readline() for _ in range(15)]
        for line in first_few:
            if line.startswith("From "):
                is_mbox = True
            if line.startswith("Subject:"):
                subj = re.sub(r"^Subject:\s*(\[[^\]]+\])?\s*", "", line).strip()
            if line.startswith("From:"):
                author = line.strip()
    if not subj:
        subj = fname.replace(".patch", "")
    return fname, subj, author, is_mbox

for idx, p in enumerate(patch_files, 1):
    fname, subj, author, is_mbox = get_patch_info(p)
    print(f"\n[{idx:02d}/{len(patch_files)}] Evaluating {fname}...")
    
    # Check if changes are already reverse-applicable (already upstream)
    res_rev = subprocess.run(["git", "-C", REPO_DIR, "apply", "--check", "--reverse", p],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if res_rev.returncode == 0:
        # Search git log for cite commit
        res_log = subprocess.run(["git", "-C", REPO_DIR, "log", "--grep=" + subj[:30], "--oneline", "-n", "1"],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        cite = res_log.stdout.strip() if res_log.stdout.strip() else "Merged in upstream history"
        print(f"  -> PRESENT UPSTREAM: {cite}")
        results.append({
            "patch": fname,
            "subject": subj,
            "status": "PRESENT_UPSTREAM",
            "cite": cite,
            "resolution": "Already present upstream; skipped."
        })
        continue

    # Try apply
    if is_mbox:
        res = subprocess.run(["git", "-C", REPO_DIR, "am", "--3way", p],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    else:
        res = subprocess.run(["git", "-C", REPO_DIR, "apply", "--3way", p],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0:
            subprocess.run(["git", "-C", REPO_DIR, "add", "-A"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["git", "-C", REPO_DIR, "commit", "-m", subj], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    if res.returncode == 0:
        print(f"  -> APPLIED CLEAN")
        results.append({
            "patch": fname,
            "subject": subj,
            "status": "APPLIED_CLEAN",
            "cite": None,
            "resolution": "Applied cleanly via 3-way merge."
        })
    else:
        err = res.stderr + res.stdout
        # Check if am failed because patch empty
        if "Patch is empty" in err or "previous cherry-pick" in err:
            print(f"  -> PRESENT UPSTREAM (empty patch)")
            subprocess.run(["git", "-C", REPO_DIR, "am", "--abort"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            results.append({
                "patch": fname,
                "subject": subj,
                "status": "PRESENT_UPSTREAM",
                "cite": "Upstream commit contains identical changes",
                "resolution": "Patch empty against upstream; skipped."
            })
            continue

        # Check conflicted files
        res_st = subprocess.run(["git", "-C", REPO_DIR, "status", "--porcelain"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        conflicts = [line.split()[-1] for line in res_st.stdout.splitlines() if line.startswith("UU") or line.startswith("U ") or line.startswith(" U")]
        print(f"  -> CONFLICT in {conflicts}")
        
        # We will log the conflict
        results.append({
            "patch": fname,
            "subject": subj,
            "status": "CONFLICT",
            "cite": None,
            "conflicts": conflicts,
            "err": err[:300]
        })
        # For now, abort to keep moving and inspect all
        subprocess.run(["git", "-C", REPO_DIR, "am", "--abort"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["git", "-C", REPO_DIR, "reset", "--hard", "HEAD"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

with open(r"docs\reports\rebase_chain_initial.json", "w", encoding="utf-8") as f:
    json.dump(results, f, indent=2)

print("\nChain evaluation complete! Saved to docs/reports/rebase_chain_initial.json")
