import re

with open('D:/gow-bilal-asia/ascension/host/ascension_patch_table.inc', 'r', encoding='utf-8', errors='replace') as f:
    text = f.read()

# Pattern for struct ascension_patch { const char* name; const char* kind; int default_on; uint32_t first, count; ... }
patches = re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*(\d+),\s*(\d+),\s*(\d+)', text)
for p in patches:
    print(f'Name: {p[0]:<35} Kind: {p[1]:<10} DefaultOn: {p[2]} First: {p[3]} Count: {p[4]}')

# Pattern for writes: struct ascension_patch_write { uint32_t addr, len, orig, patched; };
writes = re.findall(r'\{\s*(0x[0-9a-fA-F]+|\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', text)
print(f'Total patch writes: {len(writes)}')
for w in writes:
    addr = int(w[0], 0)
    length = int(w[1])
    print(f'  Addr: 0x{addr:08X} len: {length}')
