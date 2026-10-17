import json, re

lines = open('src/gen/ppu_hle_nids.cpp', encoding='utf-8').readlines()
imports = json.load(open('imports.json'))
gcm_nids = {int(x['nid'], 16): x['nid'] for x in imports if 'cellGcm' in x.get('library', '')}

for line in lines:
    for nid_int, nid_str in gcm_nids.items():
        if f'0x{nid_int:08X}u' in line or f'0x{nid_int:08x}u' in line:
            m = re.search(r'"([^"]+)"', line)
            name = m.group(1) if m else "unknown"
            print(f"{nid_str}: {name}")
