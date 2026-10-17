import json

data = json.load(open('analysis/EBOOT.imports.json'))
triage = json.load(open('analysis/nid_triage.json'))
prefixes = ('cellPad', 'cellMsgDialog', 'cellSysutil', 'cellSail', 'cellPamf', 'cellVdec', 'cellDmux', 'cellSaveData', 'cellGameContent')

nid_map = {}
for cat in ['COVERED', 'PSN', 'MISSING']:
    for it in triage['categorized'].get(cat, []):
        nid_map[it.get('nid')] = (it.get('name'), cat)

print("Matching imports by NID in EBOOT:")
found = 0
for e in data:
    nid = e.get('nid')
    if nid in nid_map:
        name, cat = nid_map[nid]
        for p in prefixes:
            if name.startswith(p):
                stub = int(str(e.get('stub', 0)), 0)
                print(f"  {name} (nid={nid}, stub=0x{stub:08X}, {cat})")
                found += 1
                break
print(f"Total matching imports: {found}")
