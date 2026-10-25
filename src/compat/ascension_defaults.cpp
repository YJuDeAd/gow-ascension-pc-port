// Runtime defaults for God of War: Ascension, set at startup before anything else.
// Adapted from gow-ascension-pc@676eefe / host/ascension_defaults.cpp (MIT License).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void set_default(const char* name, const char* value)
{
    const char* cur = getenv(name);
    if (cur && cur[0] != '\0') {
        printf("  [ENV] %-24s = %-35s (from environment)\n", name, cur);
        return;
    }
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
    printf("  [DEF] %-24s = %-35s (default)\n", name, value);
}

extern "C" void ascension_apply_defaults(void)
{
    printf("=== [ASCENSION-DEFAULTS] Applying Engine Runtime Defaults ===\n");
    set_default("PS3_EDGE_ZLIB_HLE", "1");
    set_default("RSX_LIVE_DRAW", "1");
    set_default("LD_SWIN", "0");
    set_default("GCM_PARK_WAIT_MS", "2000");
    set_default("GCM_SEMA_ACQUIRE", "0");
    set_default("GCM_PARK_SPU_HOLD_MS", "1000");
    set_default("GCM_REPORT_SPU_HOLD_MS", "50");
    set_default("GCM_ROUND_GATE", "9BAC30");
    set_default("GCM_ROUND_RING", "io:1ED00,18");
    set_default("SPURS_READY_LEVEL", "1");
    set_default("SPURS_MAXCONT", "3=5,13=3,15=5,16=5,17=5");
    set_default("SPURS_IDLE_BACKOFF_MAX", "0");
    set_default("SPURS_PIN", "1");
    set_default("PS3_PIN_PPU", "1");
    set_default("SPU_REJOIN", "204:9700-1C000");
    set_default("SPU_REJOIN_CALL", "1");
    set_default("LD_PERF_OVERLAY", "1");
    set_default("PS3_OVERLAY_COLORS", "#E2B86AFF,#0B090899,#E2B86AFF,#00000000");
    set_default("PS3_VERBOSE", "0");
    set_default("PS3_MODS", "mods/skip-intro");
    set_default("GOW_FIOS_CACHE", "off");
    set_default("SPU_INTERP_UNLIFTED", "1");
    set_default("SPU_DMA_LAX", "1");
    printf("=============================================================\n\n");
}

extern "C" const char* const spu_title_nolift = nullptr;
