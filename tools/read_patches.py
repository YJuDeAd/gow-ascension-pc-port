with open('D:/gow-bilal-asia/ascension/host/ascension_patch_table.inc') as f:
    text = f.read()

import re
m_bytes = re.search(r'k_patch_bytes\[\] = \{(.*?)\};', text, re.DOTALL)
if m_bytes:
    raw_hex = re.findall(r'0x[0-9A-Fa-f]{2}', m_bytes.group(1))
    bytes_list = [int(x, 16) for x in raw_hex]
    print(f"Total patch bytes: {len(bytes_list)}")
    print(f"Write 13 (0x0081C9B7, 1B): orig=0x{bytes_list[104]:02X}, patch=0x{bytes_list[105]:02X}")
    orig14 = ' '.join(f"{bytes_list[106+i]:02X}" for i in range(4))
    patch14 = ' '.join(f"{bytes_list[110+i]:02X}" for i in range(4))
    print(f"Write 14 (0x00369B34, 4B): orig={orig14}, patch={patch14}")
    orig15 = ' '.join(f"{bytes_list[114+i]:02X}" for i in range(4))
    patch15 = ' '.join(f"{bytes_list[118+i]:02X}" for i in range(4))
    print(f"Write 15 (0x00369B64, 4B): orig={orig15}, patch={patch15}")
