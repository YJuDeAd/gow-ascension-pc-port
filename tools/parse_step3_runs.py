import re, glob, os

runs = []
for i in range(1, 11):
    log_name = f'scratch/step3/run{i:02d}_stderr.log'
    if not os.path.exists(log_name):
        continue
    content = open(log_name, 'r', encoding='utf-8', errors='ignore').read()
    
    # Telemetry
    tele_matches = re.findall(r'\[TELEMETRY 5s\][^\n]*total_presents=(\d+)', content)
    total_presents = tele_matches[-1] if tele_matches else '0'
    
    # Live draw
    ld_matches = re.findall(r'\[live-draw\] frame (\d+)[^\n]*exec=(\d+)', content)
    ld_frames = ld_matches[-1][0] if ld_matches else '0'
    ld_exec = ld_matches[-1][1] if ld_matches else '0'
    
    # 1E48 calls
    c_1e48 = len(re.findall(r'\[WWS-1E48\]', content))
    
    # 0x747D3510
    m_3510 = re.findall(r'\[0x747D3510\]=0x([0-9A-Fa-f]+)\s*\((-?\d+)\)', content)
    last_3510 = f"0x{m_3510[-1][0]} ({m_3510[-1][1]})" if m_3510 else 'N/A'
    
    # brbDriver lr
    m_lr = re.findall(r'\[THREAD9-brbDriver\][^\n]*lr=0x([0-9A-Fa-f]+)', content)
    last_lr = f"0x{m_lr[-1]}" if m_lr else 'N/A'
    
    # Checkpoints
    cps = re.findall(r'(CP\d_[A-Z0-9_]+)', content)
    last_cp = cps[-1] if cps else 'CP0'
    
    runs.append({
        'run': i,
        'log': f'run{i:02d}_stderr.log',
        'presents': total_presents,
        'live_draw': f'{ld_frames} (exec={ld_exec})',
        'c_1e48': c_1e48,
        'val_3510': last_3510,
        'brb_lr': last_lr,
        'last_cp': last_cp
    })

print('| Run | Log File | True Presents (Telemetry) | Live-Draw Frames | 0x1E48 Calls | [0x747D3510] at 60s | brbDriver lr | Last Checkpoint |')
print('|---|---|---|---|---|---|---|---|')
for r in runs:
    print(f"| {r['run']} | `{r['log']}` | {r['presents']} | {r['live_draw']} | {r['c_1e48']} | {r['val_3510']} | {r['brb_lr']} | {r['last_cp']} |")
