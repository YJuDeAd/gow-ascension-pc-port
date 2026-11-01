import os

# 1. Update ps3recomp/libs/spurs/cellSpurs.c
path = 'ps3recomp/libs/spurs/cellSpurs.c'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

target = """    /* If another workload with higher or equal priority wants this SPU, yield to it */
    if (pick != 0x20 && (pick_pri <= cur_pri || (!cur_sig && cur_ready < si->active[cur]))) {
        return pick;
    }"""

replacement = """    /* If another workload with higher or equal priority wants this SPU, yield to it */
    static int s_revert_dde = -1;
    if (s_revert_dde < 0) { const char* e = getenv("GOW_REVERT_DDE3F83"); s_revert_dde = (e && *e == '1') ? 1 : 0; }
    if (!s_revert_dde) {
        if (pick != 0x20 && (pick_pri <= cur_pri || (!cur_sig && cur_ready < si->active[cur]))) {
            return pick;
        }
    }"""

if target in text:
    text = text.replace(target, replacement, 1)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print("Updated cellSpurs.c")
else:
    print("cellSpurs.c target not found (may already be updated)")

# 2. Update src/compat/upstream_compat.cpp
path = 'src/compat/upstream_compat.cpp'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

target = """    void gow_register_spu_overrides(void)
    {
        static bool s_registered = false;
        if (s_registered) return;
        s_registered = true;"""

replacement = """    void gow_register_spu_overrides(void)
    {
        static bool s_registered = false;
        if (s_registered) return;
        s_registered = true;

        if (getenv("GOW_REVERT_DDE3F83") || getenv("GOW_NO_SPU_OVERRIDES")) {
            fprintf(stderr, "[ascension] SPU img=2 overrides DISABLED by env (GOW_REVERT_DDE3F83/GOW_NO_SPU_OVERRIDES)\\n");
            return;
        }"""

if target in text:
    text = text.replace(target, replacement, 1)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    print("Updated upstream_compat.cpp")
else:
    print("upstream_compat.cpp target not found")

# 3. Update src/compat/ascension_defaults.cpp
path = 'src/compat/ascension_defaults.cpp'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

target = """    set_default("SPURS_READY_LEVEL", "1");
    set_default("SPURS_MAXCONT", "3=5,11=5,13=3,15=5,16=5,17=5,18=5");
    set_default("SPURS_IDLE_BACKOFF_MAX", "0");
    set_default("SPU_REJOIN", "204:9700-1C000");
    set_default("SPU_REJOIN_CALL", "1");
    set_default("GCM_FENCE_HACK", "375A50");
    set_default("RSX_LIVE_DRAW", "1");
    set_default("SPU_INTERP_UNLIFTED", "1");
    set_default("SPU_DMA_LAX", "1");
    set_default("SPURS_PIN", "1");
    set_default("PS3_PIN_PPU", "1");"""

replacement = """    set_default("SPURS_READY_LEVEL", "1");
    if (getenv("GOW_REVERT_DDE3F83")) {
        set_default("SPURS_MAXCONT", "3=5,13=3,15=5,16=5,17=5");
    } else {
        set_default("SPURS_MAXCONT", "3=5,11=5,13=3,15=5,16=5,17=5,18=5");
        set_default("SPURS_PIN", "1");
        set_default("PS3_PIN_PPU", "1");
    }
    set_default("SPURS_IDLE_BACKOFF_MAX", "0");
    set_default("SPU_REJOIN", "204:9700-1C000");
    set_default("SPU_REJOIN_CALL", "1");
    set_default("GCM_FENCE_HACK", "375A50");
    set_default("RSX_LIVE_DRAW", "1");
    set_default("SPU_INTERP_UNLIFTED", "1");
    set_default("SPU_DMA_LAX", "1");"""

if target in text:
    text = text.replace(target, replacement, 1)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    print("Updated ascension_defaults.cpp")
else:
    print("ascension_defaults.cpp target not found")
