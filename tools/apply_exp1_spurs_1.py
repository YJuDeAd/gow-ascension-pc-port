import os

# 1. Update ps3recomp/libs/spurs/cellSpurs.h
with open('ps3recomp/libs/spurs/cellSpurs.h', 'r', encoding='utf-8') as f:
    h = f.read()
target_h = "#define CELL_SPURS_MAX_WORKLOAD             16"
repl_h   = "#define CELL_SPURS_MAX_WORKLOAD             32"
if target_h in h:
    h = h.replace(target_h, repl_h, 1)
    with open('ps3recomp/libs/spurs/cellSpurs.h', 'w', encoding='utf-8') as f:
        f.write(h)
    print("Updated cellSpurs.h successfully")

# 2. Update ps3recomp/include/ps3_diagnostics.h
with open('ps3recomp/include/ps3_diagnostics.h', 'r', encoding='utf-8') as f:
    diag_h = f.read()
target_diag = "extern spurs_wid_diag_t g_spurs_wid_diags[16];"
repl_diag   = "extern spurs_wid_diag_t g_spurs_wid_diags[32];"
if target_diag in diag_h:
    diag_h = diag_h.replace(target_diag, repl_diag, 1)
    with open('ps3recomp/include/ps3_diagnostics.h', 'w', encoding='utf-8') as f:
        f.write(diag_h)
    print("Updated ps3_diagnostics.h successfully")

# 3. Update ps3recomp/runtime/syscalls/sys_ppu_thread.c
with open('ps3recomp/runtime/syscalls/sys_ppu_thread.c', 'r', encoding='utf-8') as f:
    spt = f.read()
target_spt1 = "spurs_wid_diag_t      g_spurs_wid_diags[16];"
repl_spt1   = "spurs_wid_diag_t      g_spurs_wid_diags[32];"
if target_spt1 in spt:
    spt = spt.replace(target_spt1, repl_spt1, 1)
target_spt2 = "if (wid >= 16) return 0;"
repl_spt2   = "if (wid >= 32) return 0;"
if target_spt2 in spt:
    spt = spt.replace(target_spt2, repl_spt2, 1)
with open('ps3recomp/runtime/syscalls/sys_ppu_thread.c', 'w', encoding='utf-8') as f:
    f.write(spt)
print("Updated sys_ppu_thread.c successfully")

# 4. Update ps3recomp/runtime/spu/spu_channels.c
with open('ps3recomp/runtime/spu/spu_channels.c', 'r', encoding='utf-8') as f:
    ch = f.read()
target_ch1 = "if (ctx->wid < 16) {"
repl_ch1   = "if (ctx->wid < 32) {"
if target_ch1 in ch:
    ch = ch.replace(target_ch1, repl_ch1)
    with open('ps3recomp/runtime/spu/spu_channels.c', 'w', encoding='utf-8') as f:
        f.write(ch)
    print("Updated spu_channels.c successfully")

# 5. Update ps3recomp/runtime/spu/spurs_policy.c
with open('ps3recomp/runtime/spu/spurs_policy.c', 'r', encoding='utf-8') as f:
    sp = f.read()

target_sp1 = """static struct PmInstance {
    int used, retiring;
    unsigned users, workload_users[16];
    uint32_t ea;
    struct PmSlot slots[16][6];
} s_pm_instances[SPURS_PM_INSTANCES];"""

repl_sp1 = """static struct PmInstance {
    int used, retiring;
    unsigned users, workload_users[32];
    uint32_t ea;
    struct PmSlot slots[32][6];
} s_pm_instances[SPURS_PM_INSTANCES];"""
if target_sp1 in sp:
    sp = sp.replace(target_sp1, repl_sp1, 1)

target_sp2 = "int spurs_policy_release_workload(uint32_t ea, uint32_t wid)\n{\n    if (wid >= 16) return -1;"
repl_sp2   = "int spurs_policy_release_workload(uint32_t ea, uint32_t wid)\n{\n    if (wid >= 32) return -1;"
if target_sp2 in sp:
    sp = sp.replace(target_sp2, repl_sp2, 1)

target_sp3 = "enum { SPURS_PM_MAX_WKL = 16 };"
repl_sp3   = "enum { SPURS_PM_MAX_WKL = 32 };"
if target_sp3 in sp:
    sp = sp.replace(target_sp3, repl_sp3, 1)

target_sp4 = "if (wid < 16) {\n        g_spurs_wid_diags[wid].started_count++;"
repl_sp4   = "if (wid < 32) {\n        g_spurs_wid_diags[wid].started_count++;"
if target_sp4 in sp:
    sp = sp.replace(target_sp4, repl_sp4, 1)

target_sp5 = "if (wid < 16) {\n        g_spurs_wid_diags[wid].last_pc = (uint32_t)ctx->pc & 0x3FFFF;\n    }"
repl_sp5   = "if (wid < 32) {\n        g_spurs_wid_diags[wid].last_pc = (uint32_t)ctx->pc & 0x3FFFF;\n    }"
if target_sp5 in sp:
    sp = sp.replace(target_sp5, repl_sp5, 1)

target_sp6 = "        static SPU_THREAD_LOCAL unsigned long s_pn_wid[16] = {0};\n        unsigned long s_pn = ++s_pn_wid[wid & 15];"
repl_sp6   = "        static SPU_THREAD_LOCAL unsigned long s_pn_wid[32] = {0};\n        unsigned long s_pn = ++s_pn_wid[wid & 31];"
if target_sp6 in sp:
    sp = sp.replace(target_sp6, repl_sp6, 1)

with open('ps3recomp/runtime/spu/spurs_policy.c', 'w', encoding='utf-8') as f:
    f.write(sp)
print("Updated spurs_policy.c successfully")

# 6. Update src/main.cpp
with open('src/main.cpp', 'r', encoding='utf-8') as f:
    main_c = f.read()

target_main = """        fprintf(stderr, "=== [WATCHDOG T=3s] SPURS Workloads (WID 0..15) ===\\n");
        for (uint32_t wid = 0; wid < 16; wid++) {"""
repl_main   = """        fprintf(stderr, "=== [WATCHDOG T=3s] SPURS Workloads (WID 0..31) ===\\n");
        for (uint32_t wid = 0; wid < 32; wid++) {"""
if target_main in main_c:
    main_c = main_c.replace(target_main, repl_main, 1)
    with open('src/main.cpp', 'w', encoding='utf-8') as f:
        f.write(main_c)
    print("Updated src/main.cpp successfully")

# 7. Update src/compat/ascension_hle.cpp
with open('src/compat/ascension_hle.cpp', 'r', encoding='utf-8') as f:
    hle = f.read()

target_hle1 = "static uint32_t s_all_tasksets[16];"
repl_hle1   = "static uint32_t s_all_tasksets[32];"
if target_hle1 in hle:
    hle = hle.replace(target_hle1, repl_hle1, 1)

target_hle2 = "if (s_num_tasksets < 16) {"
repl_hle2   = "if (s_num_tasksets < 32) {"
if target_hle2 in hle:
    hle = hle.replace(target_hle2, repl_hle2, 1)

with open('src/compat/ascension_hle.cpp', 'w', encoding='utf-8') as f:
    f.write(hle)
print("Updated src/compat/ascension_hle.cpp successfully")
