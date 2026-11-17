import shutil

with open('D:/gow-bilal-asia/ascension/host/ascension_defaults.cpp', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

shim = """#include <string.h>
#include <stdlib.h>
extern "C" int ascension_patch_on(const char* name) {
    const char* p = getenv("PS3_PATCHES");
    return (p && strstr(p, name)) ? 1 : 0;
}
extern "C" int ascension_patch_list_main(void) { return 0; }
extern "C" int spu_fnrec_replay_main(void) { return 0; }
"""

with open('src/compat/ascension_defaults.cpp', 'w', encoding='utf-8') as f:
    f.write(shim + "\n" + text)

print("Updated src/compat/ascension_defaults.cpp with stubs and their defaults.")
