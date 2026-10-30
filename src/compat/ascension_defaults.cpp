// Runtime defaults for God of War: Ascension, set before main() runs.
// Directly mirrors gow-ascension-pc/host/ascension_defaults.cpp (MIT License).

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

namespace {

// Minimal self-contained SHA-1 implementation
struct Sha1Context {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];

    void init() {
        state[0] = 0x67452301;
        state[1] = 0xEFCDAB89;
        state[2] = 0x98BADCFE;
        state[3] = 0x10325476;
        state[4] = 0xC3D2E1F0;
        count = 0;
    }

    static uint32_t rol(uint32_t v, int b) {
        return (v << b) | (v >> (32 - b));
    }

    void transform(const uint8_t block[64]) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)block[i * 4] << 24) |
                   ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) |
                   ((uint32_t)block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    void update(const uint8_t* data, size_t len) {
        size_t i = 0;
        size_t idx = (size_t)((count >> 3) & 63);
        count += (uint64_t)len << 3;
        size_t part_len = 64 - idx;

        if (len >= part_len) {
            memcpy(&buffer[idx], data, part_len);
            transform(buffer);
            for (i = part_len; i + 63 < len; i += 64) {
                transform(&data[i]);
            }
            idx = 0;
        }
        memcpy(&buffer[idx], &data[i], len - i);
    }

    void final(uint8_t digest[20]) {
        uint8_t final_count[8];
        for (int i = 0; i < 8; i++) {
            final_count[i] = (uint8_t)((count >> ((7 - i) * 8)) & 0xFF);
        }
        update((const uint8_t*)"\x80", 1);
        while ((count & 504) != 448) {
            update((const uint8_t*)"\0", 1);
        }
        update(final_count, 8);
        for (int i = 0; i < 20; i++) {
            digest[i] = (uint8_t)((state[i >> 2] >> ((3 - (i & 3)) * 8)) & 0xFF);
        }
    }
};

static std::vector<std::string> s_recorded_lines;

void set_default(const char* name, const char* value)
{
    char line_buf[256];
#ifdef _WIN32
    char existing[1024] = {0};
    DWORD len = GetEnvironmentVariableA(name, existing, sizeof(existing));
    if (len > 0) {
        snprintf(line_buf, sizeof(line_buf), "  [ENV] %-24s = %-35s (from environment)", name, existing);
        printf("%s\n", line_buf);
        s_recorded_lines.push_back(line_buf);
        return;
    }
    SetEnvironmentVariableA(name, value);
    _putenv_s(name, value);
    snprintf(line_buf, sizeof(line_buf), "  [DEF] %-24s = %-35s (default)", name, value);
    printf("%s\n", line_buf);
    s_recorded_lines.push_back(line_buf);
#else
    const char* existing = getenv(name);
    if (existing && existing[0] != '\0') {
        snprintf(line_buf, sizeof(line_buf), "  [ENV] %-24s = %-35s (from environment)", name, existing);
        printf("%s\n", line_buf);
        s_recorded_lines.push_back(line_buf);
        return;
    }
    setenv(name, value, 0);
    snprintf(line_buf, sizeof(line_buf), "  [DEF] %-24s = %-35s (default)", name, value);
    printf("%s\n", line_buf);
    s_recorded_lines.push_back(line_buf);
#endif
}

static std::string get_project_root()
{
    char exe[4096] = {0};
#ifdef _WIN32
    if (!GetModuleFileNameA(nullptr, exe, sizeof(exe))) return ".";
#elif defined(__APPLE__)
    uint32_t n = sizeof(exe);
    if (_NSGetExecutablePath(exe, &n)) return ".";
#else
    if (readlink("/proc/self/exe", exe, sizeof(exe) - 1) <= 0) return ".";
#endif
    std::string dir = exe;
    for (char& c : dir) if (c == '\\') c = '/';
    auto slash = dir.find_last_of('/');
    if (slash != std::string::npos) dir.resize(slash);
    auto last_slash = dir.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string comp = dir.substr(last_slash + 1);
        if (comp == "Release" || comp == "Debug") {
            dir.resize(last_slash);
            last_slash = dir.find_last_of('/');
            if (last_slash != std::string::npos && dir.substr(last_slash + 1) == "build") {
                dir.resize(last_slash);
            }
        } else if (comp == "build") {
            dir.resize(last_slash);
        }
    }
    return dir;
}

void set_default_overlay_font()
{
    std::string dir = get_project_root();
    const std::string font = dir + "/launcher/Cinzel-Black.ttf";
    if (FILE* f = fopen(font.c_str(), "rb")) {
        fclose(f);
        set_default("PS3_OVERLAY_FONT", font.c_str());
    }
}

} // namespace

extern "C" void ascension_apply_defaults(void)
{
    static bool s_applied = false;
    if (s_applied) return;
    s_applied = true;

    s_recorded_lines.clear();
    printf("=== [ASCENSION-DEFAULTS-AUDIT] Effective Environment Values ===\n");
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
    set_default("SPU_REJOIN", "204:9700-1C000");
    set_default("SPU_REJOIN_CALL", "1");
    set_default("LD_PERF_OVERLAY", "1");
    set_default("PS3_OVERLAY_COLORS", "#E2B86AFF,#0B090899,#E2B86AFF,#00000000");
    set_default_overlay_font();
    set_default("PS3_VERBOSE", "0");
    // Title-specific defaults:
    std::string root = get_project_root();
    std::string skip_intro_mod = root + "/mods/skip-intro";
    set_default("PS3_MODS", skip_intro_mod.c_str());
    set_default("GOW_FIOS_CACHE", "off");
    set_default("SPU_INTERP_UNLIFTED", "1");
    set_default("SPU_DMA_LAX", "1");
    set_default("WATCHDOG_AT", "999999,9999999");

    // A run killed while FIOS rewrites its HDD cache leaves cache.idx zero-filled,
    // and every later boot loops re-reading it on a black screen.
    // Only delete if the file exists, size > 0, and ALL bytes are zero.
    auto check_and_clean_fios_cache = [](const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return;
        fseek(f, 0, SEEK_END);
        long long sz = _ftelli64(f);
        if (sz <= 0) {
            fclose(f);
            return;
        }
        fseek(f, 0, SEEK_SET);
        char buf[4096];
        bool all_zero = true;
        size_t n = 0;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            for (size_t i = 0; i < n; i++) {
                if (buf[i] != 0) {
                    all_zero = false;
                    break;
                }
            }
            if (!all_zero) break;
        }
        fclose(f);
        if (all_zero) {
            fprintf(stderr, "[FIOS] Deleted zero-filled cache.idx: path=%s, size=%lld bytes\n",
                    path.c_str(), sz);
#ifdef _WIN32
            DeleteFileA(path.c_str());
#else
            unlink(path.c_str());
#endif
        }
    };
    check_and_clean_fios_cache("vfs/cache/BCAS25016/fios/cache.idx");
    check_and_clean_fios_cache("vfs/cache/BCES01741/fios/cache.idx");
    check_and_clean_fios_cache("vfs/cache/NPUA80918/fios/cache.idx");
    check_and_clean_fios_cache("vfs/cache/UCES01741/fios/cache.idx");

    // VFS root check
    const char* env_vfs = getenv("PS3_VFS_ROOT");
    if (!env_vfs || !*env_vfs) {
        if (FILE* f = fopen("E:/PS3_GAME/USRDIR/GAME.PSARC", "rb")) {
            fclose(f);
            set_default("PS3_VFS_ROOT", "E:");
        } else {
            fprintf(stderr, "\n============================================================================\n");
            fprintf(stderr, "[FATAL] Disc image is not mounted!\n");
            fprintf(stderr, "  Could not find E:\\PS3_GAME\\USRDIR\\GAME.PSARC.\n");
            fprintf(stderr, "  Please mount the decrypted game ISO before running gow_ascension.exe.\n");
            fprintf(stderr, "============================================================================\n\n");
            fflush(stderr);
#ifdef _WIN32
            ExitProcess(1);
#else
            exit(1);
#endif
        }
    }

    // Sort lines to compute stable deterministic SHA-1
    std::vector<std::string> sorted_lines;
    for (const auto& line : s_recorded_lines) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            // Trim leading spaces
            size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos) {
                sorted_lines.push_back(line.substr(start));
            }
        }
    }
    std::sort(sorted_lines.begin(), sorted_lines.end());

    Sha1Context sha;
    sha.init();
    for (size_t i = 0; i < sorted_lines.size(); i++) {
        sha.update((const uint8_t*)sorted_lines[i].data(), sorted_lines[i].size());
        if (i + 1 < sorted_lines.size()) {
            sha.update((const uint8_t*)"\n", 1);
        }
    }
    uint8_t digest[20] = {0};
    sha.final(digest);

    char sha_hex[41] = {0};
    for (int i = 0; i < 20; i++) {
        snprintf(&sha_hex[i * 2], 3, "%02x", digest[i]);
    }

    printf("  [ASCENSION-DEFAULTS-SHA1] %s\n", sha_hex);
    printf("============================================================================\n\n");
    fflush(stdout);
}

namespace {
struct AscensionDefaultsInit {
    AscensionDefaultsInit() {
        ascension_apply_defaults();
    }
} s_init;
} // namespace

extern "C" const char* const spu_title_nolift = nullptr;

