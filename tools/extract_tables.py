lines = open('build/run_port_stderr.log', errors='replace').readlines()

last_idx = -1
for i, line in enumerate(lines):
    if "=== [HLE-CALL-TABLE] tid=1 Distinct HLE Calls" in line:
        last_idx = i

if last_idx != -1:
    for j in range(last_idx, min(last_idx + 40, len(lines))):
        print(lines[j], end='')
