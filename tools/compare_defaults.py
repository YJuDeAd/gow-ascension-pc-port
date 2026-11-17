import re

def get_defaults(path):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    return dict(re.findall(r'set_default\("([^"]+)",\s*"([^"]+)"\)', text))

our_def = get_defaults('src/compat/ascension_defaults.cpp')
their_def = get_defaults('D:/gow-bilal-asia/ascension/host/ascension_defaults.cpp')

print(f"Our defaults count: {len(our_def)}")
print(f"Their defaults count: {len(their_def)}")

all_keys = sorted(set(our_def.keys()) | set(their_def.keys()))
print("\nDifferences:")
for k in all_keys:
    v1 = our_def.get(k, '<MISSING>')
    v2 = their_def.get(k, '<MISSING>')
    if v1 != v2:
        print(f"  {k:25s}: our={v1:15s} | their={v2}")
