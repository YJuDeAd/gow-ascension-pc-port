import os
import sys

# 1. Update src/compat/ascension_hle.cpp
with open('src/compat/ascension_hle.cpp', 'r', encoding='utf-8') as f:
    hle = f.read()

target1 = """static void hook_func_00466E28(ppu_context* ctx)
{
    const uint32_t mgr_ea = (uint32_t)ctx->gpr[3];"""
repl1 = """static void hook_func_00466E28(ppu_context* ctx)
{
    if (getenv("GOW_NO_WWS_HACK")) {
        func_00466E28(ctx);
        return;
    }

    const uint32_t mgr_ea = (uint32_t)ctx->gpr[3];"""
assert target1 in hle, "target1 not found in ascension_hle.cpp"
hle = hle.replace(target1, repl1, 1)

target2 = """static void gow_unblock_wws_mgr(uint32_t mgr_ea)
{
    if (mgr_ea < 0x10000u) return;"""
repl2 = """static void gow_unblock_wws_mgr(uint32_t mgr_ea)
{
    if (getenv("GOW_NO_WWS_HACK")) return;
    if (mgr_ea < 0x10000u) return;"""
assert target2 in hle, "target2 not found in ascension_hle.cpp"
hle = hle.replace(target2, repl2, 1)

target3 = """        if (mlr == 0x00467E1Cu) {
            uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[30];"""
repl3 = """        if (mlr == 0x00467E1Cu) {
            if (!getenv("GOW_NO_WWS_HACK")) {
                uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[30];"""
assert target3 in hle, "target3 not found in ascension_hle.cpp"
hle = hle.replace(target3, repl3, 1)

target3_close = """                }
            }
        }
        if (mlr == 0x00375A78u || mlr == 0x00375AB4u) {"""
repl3_close = """                }
            }
            }
        }
        if (mlr == 0x00375A78u || mlr == 0x00375AB4u) {"""
assert target3_close in hle, "target3_close not found in ascension_hle.cpp"
hle = hle.replace(target3_close, repl3_close, 1)

target4 = """        if (mlr == 0x00467B1Cu || mlr == 0x00467B34u || mlr == 0x00467B84u || mlr == 0x0046700Cu || mlr == 0x00467030u) {
            uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[31];"""
repl4 = """        if (mlr == 0x00467B1Cu || mlr == 0x00467B34u || mlr == 0x00467B84u || mlr == 0x0046700Cu || mlr == 0x00467030u) {
            if (getenv("GOW_NO_WWS_HACK")) {
                static uint32_t s_wws_spin_ticks = 0;
                if (++s_wws_spin_ticks <= 20 || (s_wws_spin_ticks % 500 == 0)) {
                    uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[31];
                    const uint32_t r5 = (mgr_ea >= 0x10000u) ? vm_read32(mgr_ea) : 0;
                    uint32_t cur_b = (uint32_t)g_ppu_main_ctx->gpr[4];
                    uint16_t req = (uint16_t)g_ppu_main_ctx->gpr[5];
                    uint16_t cur_val = (r5 >= 0x10000u && (cur_b < 8)) ? vm_read16(r5 + (cur_b << 4) + 0x40u) : 0;
                    fprintf(stderr, "[GOW_NO_WWS_HACK] BLOCKED tid=1 mlr=0x%08X mgr=0x%08X r5=0x%08X lane=%u target=%u cur_val=%u spins=%u\\n",
                            mlr, mgr_ea, r5, cur_b, req, cur_val, s_wws_spin_ticks);
                }
            } else {
            uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[31];"""
assert target4 in hle, "target4 not found in ascension_hle.cpp"
hle = hle.replace(target4, repl4, 1)

target4_close = """                    static uint32_t s_main_wws_unblock = 0;
                    if (++s_main_wws_unblock <= 20 || (s_main_wws_unblock % 100 == 0)) {
                        fprintf(stderr, "[WWS-MAIN-UNBLOCK #%u] lr=0x%08X mgr=0x%08X r5=0x%08X cur_b=%u completed all lanes\\n",
                                s_main_wws_unblock, mlr, mgr_ea, r5, cur_b);
                    }
                }
            }
        }
        if ((mlr >= 0x00464A9Cu && mlr <= 0x00465100u)"""
repl4_close = """                    static uint32_t s_main_wws_unblock = 0;
                    if (++s_main_wws_unblock <= 20 || (s_main_wws_unblock % 100 == 0)) {
                        fprintf(stderr, "[WWS-MAIN-UNBLOCK #%u] lr=0x%08X mgr=0x%08X r5=0x%08X cur_b=%u completed all lanes\\n",
                                s_main_wws_unblock, mlr, mgr_ea, r5, cur_b);
                    }
                }
            }
            }
        }
        if ((mlr >= 0x00464A9Cu && mlr <= 0x00465100u)"""
assert target4_close in hle, "target4_close not found in ascension_hle.cpp"
hle = hle.replace(target4_close, repl4_close, 1)

with open('src/compat/ascension_hle.cpp', 'w', encoding='utf-8') as f:
    f.write(hle)
print("Updated src/compat/ascension_hle.cpp successfully")
