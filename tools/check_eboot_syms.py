import sys

# In PowerPC assembly, 0x0099AAC4 would be formed as:
# lis rX, 0x9A (since 0xAAC4 has high bit 1, sign extension from -0x553C) or lis rX, 0x99.
# 0x0099AAC4 = (0x9A << 16) - 0x553C = 0x009A0000 - 0x553C = 0x0099AAC4.
# Let's search all generated files for 0x99AAC4 or (-21820) or 0xAAC4.

target_signed = -0x553C # -21820
target_hex = "99aac4"

print("Searching for 0x99AAC4 references...")
import glob
for fn in glob.glob("src/gen/ppu_recomp_*.cpp"):
    with open(fn, "r", encoding="utf-8", errors="ignore") as f:
        for i, line in enumerate(f):
            if "99aac4" in line.lower() or "0x99aac4" in line.lower():
                print(f"{fn}:{i+1}: {line.strip()}")
            elif "-21820" in line or "0xaac4" in line.lower():
                if "0x9a" in line.lower() or "0x99" in line.lower():
                    print(f"{fn}:{i+1}: {line.strip()}")

print("Done search.")
