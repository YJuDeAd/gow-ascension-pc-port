# tools/patch_ppu_hle.py
import sys

def main():
    path = "ps3recomp/runtime/ppu/ppu_hle.cpp"
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    # Restore line 405 if it had the invalid block-scope extern "C"
    old_target = """    extern "C" void ppu_gcm_pump_caller(const char* file, int line);
    if (ppu_gcm_pump) ppu_gcm_pump_caller(__FILE__, __LINE__);"""

    content = content.replace(old_target, "    if (ppu_gcm_pump_caller) ppu_gcm_pump_caller(__FILE__, __LINE__);\n    else if (ppu_gcm_pump) ppu_gcm_pump();")

    decl_target = 'extern "C" __attribute__((weak)) void ppu_gcm_pump(void);'
    decl_replacement = """extern "C" __attribute__((weak)) void ppu_gcm_pump(void);
extern "C" __attribute__((weak)) void ppu_gcm_pump_caller(const char* file, int line);"""

    if decl_target not in content:
        print("ERROR: decl_target not found in ppu_hle.cpp")
        sys.exit(1)

    content = content.replace(decl_target, decl_replacement, 1)

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

    print("SUCCESS: ppu_hle.cpp patched successfully with file-scope weak declaration")

if __name__ == "__main__":
    main()
