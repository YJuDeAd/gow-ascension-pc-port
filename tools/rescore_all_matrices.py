import os
import glob
import re
import scipy.stats as stats

def parse_run_log(path):
    if not os.path.exists(path):
        return {'status': 'MISSING'}
    with open(path, 'r', encoding='latin1') as f:
        content = f.read()
    
    if len(content) < 500:
        return {'status': 'EMPTY'}
    
    # Extract main thread lr occurrences
    lrs = re.findall(r'Thread tid=1 \[main\]: state=\w+.*?lr=(0x[0-9A-Fa-f]+)', content)
    boot_lrs = re.findall(r'\[BOOT-DIAG @ \d+s\] lr=(0x[0-9A-Fa-f]+)', content)
    all_lrs = [x.upper() for x in (lrs if lrs else boot_lrs)]
    
    if not all_lrs:
        all_lrs = [x.upper() for x in re.findall(r'lr=(0x[0-9A-Fa-f]+)', content)]
    
    if not all_lrs:
        return {'status': 'NO_LR', 'lrs': []}
    
    stalled_at_61B9FC = any(lr in ['0X0061B9FC', '0X61B9FC'] for lr in all_lrs[-3:])
    left_61B9FC = not stalled_at_61B9FC
    last_lr = all_lrs[-1]
    return {'status': 'OK', 'left_61B9FC': left_61B9FC, 'last_lr': last_lr, 'all_lrs': all_lrs}

def main():
    datasets = {
        'baseline_CellB_MLAA_OFF': glob.glob('scratch/part1_runs/CellB_MLAA_OFF*_stderr.log'),
        'CellA_MLAA_ON': glob.glob('scratch/part1_runs/CellA_MLAA_ON*_stderr.log'),
        'matrix_cell1': glob.glob('scratch/matrix/cell1_*_stderr.log'),
        'matrix_cell2': glob.glob('scratch/matrix/cell2_*_stderr.log'),
        'matrix_cell3': glob.glob('scratch/matrix/cell3_*_stderr.log'),
        'matrix_cell4': glob.glob('scratch/part2_cell4_runs/*.log'),
        'cellA_their_rtm_our_spu': glob.glob('scratch/part1_cellA_runs/*.log'),
        'cellB_our_rtm_their_wws_pm': glob.glob('scratch/part1_cellB_runs/*.log'),
        'cellC_our_rtm_their_hdr': glob.glob('scratch/part1_cellC_runs/*.log'),
        'interp_wws_job_pm': glob.glob('scratch/part2_interp_runs/*.log'),
        'part3_relift_pipeline': glob.glob('scratch/part3_relift_runs/*.log'),
        'part3_stable_unified': glob.glob('scratch/part3_stable_runs/Part3_Stable_run*.log'),
        'part3_5min_runs': glob.glob('scratch/part3_stable_runs/Part3_5Min_run*.log'),
    }

    baseline_pass, baseline_fail = 7, 13
    print("==========================================================================================")
    print(" RAW LOG RESCORING AUDIT (Metric: main left 0x0061B9FC)")
    print(" Baseline: 7/20 (35.0%)")
    print("==========================================================================================")
    print(f"| {'Matrix / Dataset':30s} | {'Pass Rate':12s} | {'Percentage':10s} | {'Fisher p vs Base':16s} | {'Result':12s} |")
    print(f"|{'-'*32}|{'-'*14}|{'-'*12}|{'-'*18}|{'-'*14}|")

    for name, files in datasets.items():
        passed = 0
        total = 0
        last_lrs = {}
        for f in sorted(files):
            res = parse_run_log(f)
            if res['status'] == 'OK':
                total += 1
                if res['left_61B9FC']:
                    passed += 1
                last_lrs[res['last_lr']] = last_lrs.get(res['last_lr'], 0) + 1

        if total > 0:
            table = [[passed, total - passed], [baseline_pass, baseline_fail]]
            p = stats.fisher_exact(table)[1]
            verdict = "CONFIRMED" if p < 0.05 and passed > baseline_pass else "NO EFFECT"
            print(f"| {name:30s} | {passed:2d}/{total:2d}        | {passed/total*100:6.1f}%    | {p:16.6e} | {verdict:12s} |")

if __name__ == '__main__':
    main()
