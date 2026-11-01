import os, sys, subprocess, time, json
from analyze_run import analyze

os.makedirs("logs/honest_20_runs", exist_ok=True)

runs_data = []

print("=== Starting 20 Cold Runs x 90s (Defaults Only, GOW_FAKE_DONE=0) ===")
sys.stdout.flush()

for r in range(1, 21):
    out_log = f"logs/honest_20_runs/run_{r:02d}_out.log"
    err_log = f"logs/honest_20_runs/run_{r:02d}_err.log"

    if os.path.exists(out_log): os.remove(out_log)
    if os.path.exists(err_log): os.remove(err_log)

    env = os.environ.copy()
    env["GOW_FAKE_DONE"] = "0"
    env["GOW_7B1C_BYPASS"] = "0"

    print(f"\n[RUN {r:02d}/20] Launching gow_ascension for 90s...")
    sys.stdout.flush()

    with open(out_log, "wb") as f_out, open(err_log, "wb") as f_err:
        proc = subprocess.Popen(
            ["build/gow_ascension.exe", "vfs/PS3_GAME/USRDIR/EBOOT.ELF"],
            stdout=f_out,
            stderr=f_err,
            env=env
        )

        t0 = time.time()
        while time.time() - t0 < 90.0:
            if proc.poll() is not None:
                print(f"[RUN {r:02d}/20] Process exited early at {time.time()-t0:.1f}s with code {proc.returncode}")
                break
            time.sleep(1.0)

        if proc.poll() is None:
            proc.kill()
            try:
                proc.wait(timeout=5)
            except:
                pass
            print(f"[RUN {r:02d}/20] Reached 90s, stopped.")

    # Analyze
    data = analyze(err_log)
    if data:
        runs_data.append(data)
        print(f"[RUN {r:02d}/20] Result: stage={data['stage_name']} presents={data['presents']} groups={data['groups_exec']} draws={data['max_live_draws']} 7b1c_reached={data['reached_7b1c']}")
        print(f"            pm_entries(8-18)={data['pm_entries_8_18']}")
        print(f"            lanes@30s={data['lanes_30s']}")
    sys.stdout.flush()

# Save all results to json
with open("logs/honest_20_runs/summary.json", "w", encoding="utf-8") as f:
    json.dump(runs_data, f, indent=2)

print("\n=== All 20 Runs Completed Successfully ===")
