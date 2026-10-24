# tools/patch_ascension_hle.py
import sys

def main():
    path = "src/compat/ascension_hle.cpp"
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    target1 = "void     ppu_gcm_pump(void);"
    replacement1 = """void     ppu_gcm_pump(void);
extern "C" void ppu_gcm_pump_caller(const char* file, int line);
#define ppu_gcm_pump() ppu_gcm_pump_caller(__FILE__, __LINE__)"""

    if target1 not in content:
        print("ERROR: target1 not found in ascension_hle.cpp")
        sys.exit(1)

    content = content.replace(target1, replacement1, 1)

    target2 = """extern "C" uint32_t cellGcmGetFlipStatus(void);
extern "C" void ppu_gcm_pump(void);
extern "C" void cellGcmTickFlip(void);"""

    replacement2 = """extern "C" uint32_t cellGcmGetFlipStatus(void);
extern "C" void cellGcmTickFlip(void);"""

    if target2 not in content:
        print("ERROR: target2 not found in ascension_hle.cpp")
        sys.exit(1)

    content = content.replace(target2, replacement2, 1)

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

    print("SUCCESS: ascension_hle.cpp patched successfully")

if __name__ == "__main__":
    main()
