lines = open('build/run_port_stderr.log', errors='replace').readlines()

print("--- PAD & SYSUTIL LOG ENTRIES ---")
for line in lines:
    for tag in ['[PAD-INFO2]', '[PAD-DATA]', '[PAD-ACT]', '[SYSUTIL-REG]', '[SYSUTIL-EVENT', 'cellMsgDialog', 'cellSaveData', 'cellGameContent', 'cellVdec', 'cellSail', 'cellPamf', 'cellDmux']:
        if tag in line:
            print(line, end='')
            break
