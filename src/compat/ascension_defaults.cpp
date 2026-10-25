// Runtime defaults for God of War: Ascension, set before main() runs.
// Adapted from gow-ascension-pc@676eefe / host/ascension_defaults.cpp (MIT License).

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif
#include <stdint.h>
#include <stdio.h>

namespace {

void set_default(const char* name, const char* value)
{
#ifdef _WIN32
    if (GetEnvironmentVariableA(name, nullptr, 0)) return;
    SetEnvironmentVariableA(name, value);
#else
    setenv(name, value, 0);
#endif
}

struct AscensionDefaults {
    AscensionDefaults()
    {
        set_default("PS3_EDGE_ZLIB_HLE", "1");
        set_default("RSX_LIVE_DRAW", "1");
        set_default("LD_SWIN", "0");
        set_default("GCM_PARK_WAIT_MS", "2000");
        set_default("GCM_SEMA_ACQUIRE", "1");
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
    }
} s_defaults;

} // namespace

extern "C" const char* const spu_title_nolift = nullptr;
