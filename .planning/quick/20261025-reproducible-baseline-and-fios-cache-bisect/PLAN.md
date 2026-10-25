# Quick Plan: Reproducible Baseline, FIOS Cache A/B, and Good Run Reproduction

## Goal
Establish a deterministic and reproducible test baseline via automated smoke testing (`tools/smoke.ps1`), evaluate FIOS cache impact (cold vs warm A/B), reproduce the 1.69s boot completion from `run_task4_20261025_195847` on branch `good-195847`, categorize diffs, bisect the regression, and profile SPU host depth and rejoin counters.

## Tasks
1. **Task 1: Smoke Test Automation (`tools/smoke.ps1`)**
   - Implement `tools/smoke.ps1 -Seconds 90 -Cache cold|warm|keep`.
   - Record exactly 23 metadata metrics per run into `docs/smoke_history.csv`.
2. **Task 2: Current HEAD Cache A/B**
   - Execute current HEAD 3x with `-Cache cold` and 3x with `-Cache warm`.
   - Report the 6 rows and evaluate repeatability.
3. **Task 3: Reproduce the Good Run (`run_task4_20261025_195847`)**
   - Identify exact git commit, patches, and defaults of `run_task4_20261025_195847`.
   - Create branch `good-195847`, build, and run smoke test 2x cold, 2x warm.
4. **Task 4: Categorized Architectural Diff**
   - Map all changes between `good-195847` and HEAD across the 7 required subsystems.
5. **Task 5: Bisect Progression**
   - Apply/revert change groups one-by-one with cold cache until regression point is pinpointed.
6. **Task 6: SPU Depth & Rejoin Telemetry**
   - Measure max `host_depth` per lane over 90s, check SPU_REJOIN/REJOIN_CALL counters, and top-of-stack functions.
