/*
 * upstream_compat.cpp — Fallback definitions for compiling against upstream ps3recomp baseline.
 *
 * Uses weak symbols so that when building against an upstream ps3recomp commit without
 * port-specific telemetry or diagnostics, the symbols resolve cleanly.
 */

#include "ps3_diagnostics.h"
#include "sys_ppu_thread.h"
#include "spu_context.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <chrono>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <atomic>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

#define GOW_WEAK __attribute__((weak))

typedef struct GcmFifoBreakdown {
    volatile uint64_t lock_wait_qpc;
    volatile uint64_t parked_qpc;
    volatile uint64_t decode_qpc;
    volatile uint64_t method_qpc;
    volatile uint64_t fence_pub_qpc;
    volatile uint64_t total_qpc;
    volatile uint64_t calls;
    struct {
        volatile uint32_t tid;
        volatile uint64_t count;
    } callers[8];
} GcmFifoBreakdown;


extern "C" {
extern uint8_t* vm_base;
GOW_WEAK volatile uint32_t g_spurs_pm_entry_count[32] = {0};
mfc_hist_t g_spu_mfc_hist[8][5] = {{{0}}};
uint32_t g_spu_mfc_hist_idx[8] = {0};
volatile uint32_t g_spurs_lane_active_wid[8] = {255, 255, 255, 255, 255, 255, 255, 255};
GOW_WEAK spu_context* volatile g_spurs_lane_ctx[8] = {0};
GOW_WEAK HANDLE g_spurs_lane_thread[8] = {0};
GOW_WEAK LaneAtomicStats g_lane_atomic[8] = {{0}};
GOW_WEAK SpursPollEntry g_spurs_poll_hist[5] = {{0}};
GOW_WEAK volatile uint32_t g_spurs_poll_hist_idx = 0;
extern unsigned long long ps3_qpc_us(void);
extern ppu_context* volatile g_ppu_main_ctx;
extern ppu_thread_info g_ppu_threads[PPU_THREAD_MAX];
}

struct SpursCallRate {
    uint32_t count_1s;
    uint32_t total;
};
static std::mutex s_task4_mutex;
static std::map<std::string, SpursCallRate> s_task4_spurs_calls;
static std::atomic<bool> s_task4_monitor_started{false};

#ifdef _WIN32
static std::atomic<DWORD> s_main_host_tid{0};
static HANDLE s_main_host_handle = NULL;


// ---------------------------------------------------------------------------
// Lock contention & Registration monitor (Task 1)
// ---------------------------------------------------------------------------
struct LockThreadStats {
    uint32_t acq_count;
    uint64_t max_hold_us;
    uint64_t max_wait_us;
    uint64_t total_hold_us;
    uint64_t total_wait_us;
};

struct LockMonitor {
    const char* name;
    SRWLOCK* target_lock;
    std::atomic<DWORD> owner_tid{0};
    std::atomic<uint64_t> acquire_qpc{0};
    std::atomic<uint64_t> max_hold_ever{0};
    std::atomic<DWORD> max_holder_tid{0};
    std::mutex mtx;
    std::map<DWORD, LockThreadStats> stats_1s;
};

static LockMonitor s_si_lock_mon{"si->lock", nullptr};
static LockMonitor s_reg_lock_mon{"s_workload_registry_lock", nullptr};

typedef void (WINAPI *AcquireSRWLockExclusive_fn)(PSRWLOCK SRWLock);
typedef void (WINAPI *ReleaseSRWLockExclusive_fn)(PSRWLOCK SRWLock);
static AcquireSRWLockExclusive_fn g_real_AcquireSRWLock = nullptr;
static ReleaseSRWLockExclusive_fn g_real_ReleaseSRWLock = nullptr;

static void* s_spurs_inst_ptr = nullptr;

static const char* get_thread_role_name(DWORD host_tid, char* buf, size_t sz) {
    if (host_tid == 0) return "none";
    if (host_tid == s_main_host_tid.load()) return "main";
    for (uint32_t tid = 2; tid <= PPU_THREAD_MAX; tid++) {
        if (g_ppu_threads[tid - 1].state != PPU_THREAD_STATE_FREE &&
            g_ppu_threads[tid - 1].host_tid == host_tid) {
            snprintf(buf, sz, "guest_%u[%s]", tid, g_ppu_threads[tid - 1].name[0] ? g_ppu_threads[tid - 1].name : "unnamed");
            return buf;
        }
    }
    if (s_spurs_inst_ptr) {
        uint8_t* p = (uint8_t*)s_spurs_inst_ptr;
        HANDLE coord = *(HANDLE*)(p + 0x38);
        if (coord && GetThreadId(coord) == host_tid) return "spurs_kernel";
        for (int k = 0; k < 6; k++) {
            HANDLE th = *(HANDLE*)(p + 0x40 + k * 64 + 56);
            if (th && GetThreadId(th) == host_tid) {
                snprintf(buf, sz, "spu_lane_%d", k);
                return buf;
            }
        }
    }
    snprintf(buf, sz, "host_%lu", (unsigned long)host_tid);
    return buf;
}

static void WINAPI my_AcquireSRWLockExclusive(PSRWLOCK lock) {
    if (g_real_AcquireSRWLock == nullptr) {
        AcquireSRWLockExclusive(lock);
        return;
    }
    LockMonitor* mon = (lock == s_si_lock_mon.target_lock) ? &s_si_lock_mon :
                       (lock == s_reg_lock_mon.target_lock) ? &s_reg_lock_mon : nullptr;
    if (!mon) {
        g_real_AcquireSRWLock(lock);
        return;
    }
    uint64_t t0 = ps3_qpc_us();
    g_real_AcquireSRWLock(lock);
    uint64_t t1 = ps3_qpc_us();
    uint64_t wait_us = (t1 > t0) ? (t1 - t0) : 0;
    DWORD tid = GetCurrentThreadId();
    mon->owner_tid.store(tid);
    mon->acquire_qpc.store(t1);

    std::lock_guard<std::mutex> lk(mon->mtx);
    auto& st = mon->stats_1s[tid];
    st.acq_count++;
    if (wait_us > st.max_wait_us) st.max_wait_us = wait_us;
    st.total_wait_us += wait_us;
}

static void WINAPI my_ReleaseSRWLockExclusive(PSRWLOCK lock) {
    if (g_real_ReleaseSRWLock == nullptr) {
        ReleaseSRWLockExclusive(lock);
        return;
    }
    LockMonitor* mon = (lock == s_si_lock_mon.target_lock) ? &s_si_lock_mon :
                       (lock == s_reg_lock_mon.target_lock) ? &s_reg_lock_mon : nullptr;
    if (!mon) {
        g_real_ReleaseSRWLock(lock);
        return;
    }
    uint64_t t1 = ps3_qpc_us();
    uint64_t t0 = mon->acquire_qpc.load();
    uint64_t hold_us = (t1 > t0 && t0 > 0) ? (t1 - t0) : 0;
    DWORD tid = GetCurrentThreadId();
    mon->owner_tid.store(0);

    {
        std::lock_guard<std::mutex> lk(mon->mtx);
        auto& st = mon->stats_1s[tid];
        if (hold_us > st.max_hold_us) st.max_hold_us = hold_us;
        st.total_hold_us += hold_us;
    }
    if (hold_us > mon->max_hold_ever.load()) {
        mon->max_hold_ever.store(hold_us);
        mon->max_holder_tid.store(tid);
    }
    g_real_ReleaseSRWLock(lock);
}


typedef char* (*getenv_fn_t)(const char*);
static getenv_fn_t g_real_getenv = nullptr;

static char* my_getenv(const char* name)
{
    if (!name || !*name) return nullptr;
    if (strcmp(name, "SPU_PCSAMPLE") == 0) return nullptr;
    if (strcmp(name, "SPURS_PM_DUMP") == 0) return nullptr;
    if (strcmp(name, "WATCHDOG_UNWIND") == 0) return nullptr;

    static SRWLOCK s_env_cache_lock = SRWLOCK_INIT;
    static std::map<std::string, std::string> s_env_cache;

    AcquireSRWLockShared(&s_env_cache_lock);
    auto it = s_env_cache.find(name);
    if (it != s_env_cache.end()) {
        char* res = it->second.empty() ? nullptr : (char*)it->second.c_str();
        ReleaseSRWLockShared(&s_env_cache_lock);
        return res;
    }
    ReleaseSRWLockShared(&s_env_cache_lock);

    AcquireSRWLockExclusive(&s_env_cache_lock);
    it = s_env_cache.find(name);
    if (it != s_env_cache.end()) {
        char* res = it->second.empty() ? nullptr : (char*)it->second.c_str();
        ReleaseSRWLockExclusive(&s_env_cache_lock);
        return res;
    }

    char buf[1024];
    DWORD len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        s_env_cache[name] = std::string(buf, len);
    } else {
        s_env_cache[name] = "";
    }
    char* res = s_env_cache[name].empty() ? nullptr : (char*)s_env_cache[name].c_str();
    ReleaseSRWLockExclusive(&s_env_cache_lock);
    return res;
}

static bool hook_iat(HMODULE mod, const char* func_name, void* hook_func, void** orig_func) {
    uint8_t* base = (uint8_t*)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY* dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress) return false;
    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir->VirtualAddress);
    for (; imp->Name; imp++) {
        IMAGE_THUNK_DATA* oft = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA* ft = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + oft->u1.AddressOfData);
            if (strcmp(ibn->Name, func_name) == 0) {
                DWORD old_prot = 0;
                VirtualProtect(&ft->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_prot);
                *orig_func = (void*)ft->u1.Function;
                ft->u1.Function = (ULONG_PTR)hook_func;
                VirtualProtect(&ft->u1.Function, sizeof(void*), old_prot, &old_prot);
                return true;
            }
        }
    }
    return false;
}

static void init_lock_hooks() {
    static bool inited = false;
    if (inited) return;
    inited = true;
    HMODULE self = GetModuleHandleA(NULL);
    uint8_t* base = (uint8_t*)self;

    // Dynamically resolve s_inst and s_workload_registry_lock from map file
    FILE* mf = fopen("build/gow_ascension.map", "r");
    if (!mf) mf = fopen("gow_ascension.map", "r");
    if (mf) {
        char line[512];
        while (fgets(line, sizeof(line), mf)) {
            if (strstr(line, " s_inst\n") || strstr(line, " s_inst\r\n") || strstr(line, " s_inst ")) {
                uint64_t rva = strtoull(line, nullptr, 16);
                if (rva > 0) {
                    s_spurs_inst_ptr = (void*)(base + rva);
                    s_si_lock_mon.target_lock = (SRWLOCK*)((uint8_t*)s_spurs_inst_ptr + 0x28);
                }
            } else if (strstr(line, " s_workload_registry_lock\n") || strstr(line, " s_workload_registry_lock\r\n") || strstr(line, " s_workload_registry_lock ")) {
                uint64_t rva = strtoull(line, nullptr, 16);
                if (rva > 0) {
                    s_reg_lock_mon.target_lock = (SRWLOCK*)(base + rva);
                }
            }
        }
        fclose(mf);
    }

    hook_iat(self, "AcquireSRWLockExclusive", (void*)my_AcquireSRWLockExclusive, (void**)&g_real_AcquireSRWLock);
    hook_iat(self, "ReleaseSRWLockExclusive", (void*)my_ReleaseSRWLockExclusive, (void**)&g_real_ReleaseSRWLock);
    // hook_iat(self, "getenv", (void*)my_getenv, (void**)&g_real_getenv); // removed: cached at source
    fprintf(stderr, "[LOCK-HOOK] Installed IAT hooks: si->lock=%p, registry_lock=%p, base=%p\n",
            s_si_lock_mon.target_lock, s_reg_lock_mon.target_lock, base);
}

// Registration tracker
struct RegInfo {
    uint32_t wid;
    uint64_t start_qpc;
    uint32_t start_pass;
};
static RegInfo s_current_reg{999, 0, 0};
static std::mutex s_reg_mutex;


#include <timeapi.h>
#include <vector>
#include <algorithm>

static std::atomic<bool> s_task1_stop{false};
static std::map<uint32_t, uint32_t> s_pc_hist_1s[6];
static std::mutex s_pc_hist_mtx;

static void start_pc_sampler(void) {
    static bool s_started = false;
    if (s_started) return;
    s_started = true;
    timeBeginPeriod(1);
    std::thread([]() {
        while (!s_task1_stop.load(std::memory_order_relaxed)) {
            Sleep(1);
            std::lock_guard<std::mutex> lk(s_pc_hist_mtx);
            for (int l = 0; l < 6; l++) {
                spu_context* c = g_spurs_lane_ctx[l];
                if (c) {
                    uint32_t pc = (uint32_t)c->pc & SPU_LS_MASK;
                    s_pc_hist_1s[l][pc]++;
                }
            }
        }
        timeEndPeriod(1);
    }).detach();
}

static void task4_monitor_thread_func();

extern "C" void gow_register_main_thread(void)
{
    init_lock_hooks();
        start_pc_sampler();
    if (s_main_host_tid.load() == 0) {
        s_main_host_tid.store(GetCurrentThreadId());
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                        GetCurrentProcess(), &s_main_host_handle,
                        THREAD_ALL_ACCESS, FALSE, 0);
    }
    if (!s_task4_monitor_started.exchange(true)) {
        std::thread(task4_monitor_thread_func).detach();
    }
}

static bool safe_read_u64(uint64_t addr, uint64_t* out)
{
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, sizeof(uint64_t), &read) && (read == sizeof(uint64_t));
}

static void dump_host_stack(HANDLE th, DWORD host_tid, const char* label)
{
    HMODULE self = GetModuleHandleA(NULL);
    DWORD suspend_count = SuspendThread(th);
    if (suspend_count == (DWORD)-1) {
        fprintf(stderr, "  [STACK %s host_tid=%lu] SuspendThread failed (err=%lu)\n",
                label, (unsigned long)host_tid, GetLastError());
        return;
    }

    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_ALL;
    if (GetThreadContext(th, &ctx)) {
        HMODULE rip_mod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)ctx.Rip, &rip_mod);
        char mod_name[MAX_PATH] = "?";
        if (rip_mod) GetModuleFileNameA(rip_mod, mod_name, sizeof(mod_name));
        const char* base_mod = strrchr(mod_name, '\\');
        if (base_mod) base_mod++; else base_mod = mod_name;

        fprintf(stderr, "  [STACK %s host_tid=%lu] RIP=0x%016llX (in %s)\n",
                label, (unsigned long)host_tid, (unsigned long long)ctx.Rip, base_mod);

        // 1. Unwind using RtlVirtualUnwind
        CONTEXT walk_ctx = ctx;
        int frame = 0;
        while (walk_ctx.Rip && frame < 32) {
            uint64_t rip = walk_ctx.Rip;
            HMODULE frame_mod = NULL;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)rip, &frame_mod);
            if (frame_mod == self) {
                uint64_t rva = rip - (uint64_t)self;
                fprintf(stderr, "    frame #%02d: host_rva=0x%llX\n", frame, (unsigned long long)rva);
            } else {
                char fm_name[MAX_PATH] = "?";
                if (frame_mod) GetModuleFileNameA(frame_mod, fm_name, sizeof(fm_name));
                const char* fmb = strrchr(fm_name, '\\');
                if (fmb) fmb++; else fmb = fm_name;
                fprintf(stderr, "    frame #%02d: in %s+0x%llX\n",
                        frame, fmb, (unsigned long long)(rip - (uint64_t)frame_mod));
            }

            DWORD64 img_base = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(walk_ctx.Rip, &img_base, NULL);
            if (!fn) {
                if (walk_ctx.Rsp) {
                    uint64_t next_rip = 0;
                    if (safe_read_u64(walk_ctx.Rsp, &next_rip) && next_rip != 0) {
                        walk_ctx.Rip = next_rip;
                        walk_ctx.Rsp += 8;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else {
                __try {
                    PVOID handler_data = NULL;
                    DWORD64 establisher_frame = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, img_base, walk_ctx.Rip, fn,
                                     &walk_ctx, &handler_data, &establisher_frame, NULL);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    break;
                }
            }
            frame++;
        }

        // 2. Scan stack for self return addresses
        uint64_t* sp = (uint64_t*)ctx.Rsp;
        if (sp) {
            int found = 0;
            for (int k = 0; k < 0x2000 / 8 && found < 16; k++) {
                uint64_t v = 0;
                if (!safe_read_u64((uint64_t)(sp + k), &v)) break;
                if (v < (uint64_t)self) continue;
                HMODULE mm = NULL;
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)v, &mm);
                if (mm == self) {
                    fprintf(stderr, "    [stack_scan] ret host_rva=0x%llX\n",
                            (unsigned long long)(v - (uint64_t)self));
                    found++;
                }
            }
        }
    } else {
        fprintf(stderr, "  [STACK %s host_tid=%lu] GetThreadContext failed (err=%lu)\n",
                label, (unsigned long)host_tid, GetLastError());
    }

    ResumeThread(th);
}

static void dump_all_threads_stacks(int sec)
{
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te; te.dwSize = sizeof(te);
    if (snap == INVALID_HANDLE_VALUE) return;

    fprintf(stderr, "\n=== [ALL-THREADS-STACKS @ %ds] ===\n", sec);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
            uint32_t guest_tid = 0;
            const char* guest_name = "internal";
            DWORD main_tid = s_main_host_tid.load();
            if (main_tid != 0 && te.th32ThreadID == main_tid) {
                guest_tid = 1;
                guest_name = "main";
            } else {
                for (uint32_t tid = 2; tid <= PPU_THREAD_MAX; tid++) {
                    if (g_ppu_threads[tid - 1].state != PPU_THREAD_STATE_FREE &&
                        g_ppu_threads[tid - 1].host_tid == te.th32ThreadID) {
                        guest_tid = tid;
                        guest_name = g_ppu_threads[tid - 1].name;
                        break;
                    }
                }
            }
            char role[64];
            get_thread_role_name(te.th32ThreadID, role, sizeof(role));
            char label[128];
            if (guest_tid > 0) {
                snprintf(label, sizeof(label), "guest_tid=%u[%s] (role: %s)", guest_tid, guest_name[0] ? guest_name : "unnamed", role);
            } else {
                snprintf(label, sizeof(label), "host_tid=%lu (role: %s)", (unsigned long)te.th32ThreadID, role);
            }
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                                   FALSE, te.th32ThreadID);
            if (th) {
                dump_host_stack(th, te.th32ThreadID, label);
                CloseHandle(th);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    fprintf(stderr, "==================================\n\n");
    fflush(stderr);
}
#else
extern "C" void gow_register_main_thread(void) {}
static void dump_all_threads_stacks(int sec) {}
#endif

extern "C" uint32_t rsx_live_draw_get_last_draws(void);
extern "C" uint32_t rsx_live_draw_get_frames(void);

static void task4_monitor_thread_func()
{
    static bool s_seen_thread[PPU_THREAD_MAX + 1] = {false};

    for (int sec = 1; sec <= 300; sec++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Always-on cheap stage probe every 2 seconds (no thread suspension)
        if (sec % 2 == 0) {
            uint32_t main_lr = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->lr : 0;
            const char* main_last_hle = g_ppu_thread_diags[1].last_hle_name ? g_ppu_thread_diags[1].last_hle_name : "(none)";
            uint32_t live_draws = rsx_live_draw_get_last_draws();
            uint32_t presents = rsx_live_draw_get_frames();
            fprintf(stderr, "[STAGE] t=%d main_lr=0x%08X main_last_hle=%s live_draws=%u presents=%u\n",
                    sec, main_lr, main_last_hle, live_draws, presents);
            fflush(stderr);
        }

        static int s_gow_watchdog = -1;
        if (s_gow_watchdog == -1) {
            const char* env_wd = getenv("GOW_WATCHDOG");
            s_gow_watchdog = (env_wd && (strcmp(env_wd, "1") == 0 || strcmp(env_wd, "true") == 0)) ? 1 : 0;
        }

        init_lock_hooks();

        static int s_stall_e_sec = 0;
        uint32_t cur_main_lr = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->lr : 0;
        if (cur_main_lr == 0x00466740) {
            s_stall_e_sec++;
        } else {
            s_stall_e_sec = 0;
        }

        // Print lock stats every second
        {
            auto print_lock_stats = [](LockMonitor& mon) {
                std::map<DWORD, LockThreadStats> copy;
                {
                    std::lock_guard<std::mutex> lk(mon.mtx);
                    copy = mon.stats_1s;
                    mon.stats_1s.clear();
                }
                if (copy.empty()) return;
                DWORD cur_owner = mon.owner_tid.load();
                char role[64];
                fprintf(stderr, "  [%s] cur_owner=%lu [%s]\n", mon.name, (unsigned long)cur_owner, get_thread_role_name(cur_owner, role, sizeof(role)));
                for (const auto& kv : copy) {
                    char tr[64];
                    fprintf(stderr, "    thread %5lu [%-18s]: acq=%4u/s, max_hold=%6lluus, max_wait=%6lluus\n",
                            (unsigned long)kv.first, get_thread_role_name(kv.first, tr, sizeof(tr)),
                            kv.second.acq_count, (unsigned long long)kv.second.max_hold_us, (unsigned long long)kv.second.max_wait_us);
                }
            };
            fprintf(stderr, "\n=== [LOCK-STATS @ %ds] ===\n", sec);
            print_lock_stats(s_si_lock_mon);
            print_lock_stats(s_reg_lock_mon);
            fprintf(stderr, "==========================\n");
        }

        if (s_stall_e_sec > 0) {
            DWORD si_owner = s_si_lock_mon.owner_tid.load();
            DWORD reg_owner = s_reg_lock_mon.owner_tid.load();
            char r1[64], r2[64];
            uint32_t current_pass = 0;
            if (s_spurs_inst_ptr) {
                current_pass = *(uint32_t*)((uint8_t*)s_spurs_inst_ptr + 0x40 + 6*64 + 64 + 4 + 4);
            }
            fprintf(stderr, "[STAGE-E-STALL t=%ds stall=%ds] main_lr=0x00466740, si_owner=%lu [%s], reg_owner=%lu [%s], kernel_pass=%u\n",
                    sec, s_stall_e_sec,
                    (unsigned long)si_owner, get_thread_role_name(si_owner, r1, sizeof(r1)),
                    (unsigned long)reg_owner, get_thread_role_name(reg_owner, r2, sizeof(r2)),
                    current_pass);
        }

        static int s_dump_7b1c_sec = 0;
        if (vm_base && g_ppu_main_ctx && (cur_main_lr == 0x00467B1C || (cur_main_lr >= 0x00466E28 && cur_main_lr <= 0x00467000))) {
            if (s_dump_7b1c_sec < 20) {
                s_dump_7b1c_sec++;
                uint64_t r31 = g_ppu_main_ctx->gpr[31];
                uint64_t r5  = g_ppu_main_ctx->gpr[5];
                uint64_t r4  = g_ppu_main_ctx->gpr[4];
                uint32_t mgr_holder = (uint32_t)r31;
                uint32_t mgr_ea = (mgr_holder && mgr_holder < 0xF0000000u) ? __builtin_bswap32(*(const uint32_t*)(vm_base + mgr_holder)) : 0;
                fprintf(stderr, "\n>>> [0x00467B1C-WAIT #%d @ %ds] target_r5=%llu (0x%llX) r4(qid)=%llu r31=0x%08X mgr_ea=0x%08X\n",
                        s_dump_7b1c_sec, sec, (unsigned long long)r5, (unsigned long long)r5, (unsigned long long)r4, mgr_holder, mgr_ea);
                if (mgr_ea && mgr_ea < 0xF0000000u) {
                    fprintf(stderr, ">>> [0x00467B1C 32-WORDS @ 0x%08X]:\n", mgr_ea);
                    for (int w = 0; w < 32; w += 4) {
                        fprintf(stderr, "  +0x%02X: %08X %08X %08X %08X\n",
                                w * 4,
                                __builtin_bswap32(*(const uint32_t*)(vm_base + mgr_ea + (w + 0) * 4)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + mgr_ea + (w + 1) * 4)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + mgr_ea + (w + 2) * 4)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + mgr_ea + (w + 3) * 4)));
                    }
                    uint32_t q_ea = mgr_ea + ((uint32_t)r4 << 4) + 0x40;
                    uint16_t target_hdr = __builtin_bswap16(*(const uint16_t*)(vm_base + q_ea));
                    uint16_t l_vals[7];
                    uint16_t min_l = 0xFFFF;
                    int sentinel_cnt = 0;
                    for (int l = 0; l < 7; l++) {
                        l_vals[l] = __builtin_bswap16(*(const uint16_t*)(vm_base + q_ea + 2 + l * 2));
                        if (l_vals[l] < min_l) min_l = l_vals[l];
                        if (l_vals[l] == 0xFFFF) sentinel_cnt++;
                    }
                    fprintf(stderr, ">>> [0x00467B1C QUEUE-%llu COUNTERS @ 0x%08X] target=%u min_L=%u (sentinels=%d/7):",
                            (unsigned long long)r4, q_ea, target_hdr, min_l, sentinel_cnt);
                    for (int l = 0; l < 7; l++) {
                        fprintf(stderr, " L%d=%u", l, (unsigned)l_vals[l]);
                    }
                    fprintf(stderr, " => %s\n", (min_l >= target_hdr) ? "SATISFIED (EXIT LOOP)" : "WAITING (L0 < target)");
                }

                // TASK 1 & TASK 3 DETAILED AUDIT
                fprintf(stderr, "\n=== [TASK1-LANE-ACTIVITY #%d @ %ds] ===\n", s_dump_7b1c_sec, sec);
                std::map<uint32_t, uint32_t> pc_copy[6];
                {
                    std::lock_guard<std::mutex> lk(s_pc_hist_mtx);
                    for (int l = 0; l < 6; l++) {
                        pc_copy[l] = s_pc_hist_1s[l];
                        s_pc_hist_1s[l].clear();
                    }
                }
                for (int l = 0; l < 6; l++) {
                    spu_context* c = g_spurs_lane_ctx[l];
                    uint32_t wid = g_spurs_lane_active_wid[l];
                    FILETIME wc, we, wk, wu;
                    uint64_t user_ms = 0, kern_ms = 0;
                    if (g_spurs_lane_thread[l] && GetThreadTimes(g_spurs_lane_thread[l], &wc, &we, &wk, &wu)) {
                        user_ms = (((uint64_t)wu.dwHighDateTime << 32) | wu.dwLowDateTime) / 10000;
                        kern_ms = (((uint64_t)wk.dwHighDateTime << 32) | wk.dwLowDateTime) / 10000;
                    }
                    fprintf(stderr, "Lane %d: wid=%u (%s), HostCPU=%llu ms (user %llu ms, kern %llu ms)\n",
                            l, wid, (wid == 255) ? "IDLE/KERNEL" : "RUNNING", user_ms + kern_ms, user_ms, kern_ms);
                    std::vector<std::pair<uint32_t, uint32_t>> sorted_pcs(pc_copy[l].begin(), pc_copy[l].end());
                    std::sort(sorted_pcs.begin(), sorted_pcs.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                    fprintf(stderr, "  PC Hist (1 kHz, top %zu):", std::min<size_t>(8, sorted_pcs.size()));
                    for (size_t i = 0; i < 8 && i < sorted_pcs.size(); i++) {
                        fprintf(stderr, " 0x%05X:%u", sorted_pcs[i].first, sorted_pcs[i].second);
                    }
                    fprintf(stderr, "\n");
                    uint32_t ev_stat = c ? c->event_status : 0;
                    uint32_t ev_mask = c ? c->event_mask : 0;
                    uint32_t cur_pc  = c ? ((uint32_t)c->pc & SPU_LS_MASK) : 0;
                    fprintf(stderr, "  Atomics: GETLLAR=%llu, PUTLLC_OK=%llu, PUTLLC_FAIL=%llu, PUTLLUC=%llu, LR_Events=%llu\n",
                            (unsigned long long)g_lane_atomic[l].getllar,
                            (unsigned long long)g_lane_atomic[l].putllc_ok,
                            (unsigned long long)g_lane_atomic[l].putllc_fail,
                            (unsigned long long)g_lane_atomic[l].putlluc,
                            (unsigned long long)g_lane_atomic[l].lr_events);
                    fprintf(stderr, "  Events: status=0x%X mask=0x%X cur_pc=0x%05X\n", ev_stat, ev_mask, cur_pc);
                    const char* wait_desc = "computing / polling";
                    if (cur_pc == 0x23A0 || cur_pc == 0x2DA8) wait_desc = "waiting for interrupt / timer";
                    else if (c && c->resv_valid) wait_desc = "holding reservation";
                    fprintf(stderr, "  Wait State: %s\n", wait_desc);
                }
                fprintf(stderr, "=======================================\n");

                fprintf(stderr, "=== [TASK3-POLL-SELECT LAST 5 CALLS] ===\n");
                uint32_t pidx = g_spurs_poll_hist_idx;
                for (uint32_t i = 0; i < 5 && i < pidx; i++) {
                    uint32_t slot = (pidx >= 5) ? (pidx - 5 + i) % 5 : i;
                    const auto& p = g_spurs_poll_hist[slot];
                    fprintf(stderr, "  [%llums] spu=%u cur=%u (rdy=%u sig=%d act=%u cont=%u pri=%u) -> RETURN %u (0x%02X)\n",
                            (unsigned long long)p.t_ms, p.spu, p.cur, p.cur_ready, p.cur_sig, p.cur_active, p.cur_maxcont, p.cur_pri, p.sel, p.sel);
                    fprintf(stderr, "    Evaluated wids:\n");
                    for (int k = 0; k < 6; k++) {
                        fprintf(stderr, "      wid=%-2u: ready=%u sig=%d active=%u maxcont=%u pri=%u\n",
                                p.w[k].wid, p.w[k].ready, p.w[k].sig, p.w[k].active, p.w[k].maxcont, p.w[k].pri);
                    }
                }
                fprintf(stderr, "========================================\n\n");

            }
        }

        static int s_dump_done = 0;
        if (s_gow_watchdog) {
            if (s_stall_e_sec == 3 && !(s_dump_done & 1)) {
                s_dump_done |= 1;
                fprintf(stderr, "\n>>> [WATCHDOG DUMP #1 @ %ds] main stalled at 0x00466740 for 3s! Dumping symbolized stacks...\n", sec);
                dump_all_threads_stacks(sec);
            } else if (s_stall_e_sec == 13 && !(s_dump_done & 2)) {
                s_dump_done |= 2;
                fprintf(stderr, "\n>>> [WATCHDOG DUMP #2 @ %ds] main stalled at 0x00466740 for 13s (10s later)! Dumping symbolized stacks...\n", sec);
                dump_all_threads_stacks(sec);
            }
        }

        // Detect new threads every second
        for (uint32_t tid = 2; tid <= PPU_THREAD_MAX; tid++) {
            ppu_thread_info* t = &g_ppu_threads[tid - 1];
            if (t->state != PPU_THREAD_STATE_FREE && !s_seen_thread[tid]) {
                s_seen_thread[tid] = true;
                fprintf(stderr, "[NEW-THREAD] tid=%u name=\"%s\" entry=0x%08llX lr=0x%08X\n",
                        tid, t->name[0] ? t->name : "unnamed",
                        (unsigned long long)t->entry_addr, (uint32_t)t->ctx.lr);
            }
        }

        // 1. Log rates per second (for first 60s)
        if (sec <= 60) {
            std::lock_guard<std::mutex> lock(s_task4_mutex);
            fprintf(stderr, "\n=== [TASK4-SPURS-RATES @ %ds] ===\n", sec);
            for (auto& kv : s_task4_spurs_calls) {
                fprintf(stderr, "  %-36s : %4u calls/s (total %u)\n",
                        kv.first.c_str(), kv.second.count_1s, kv.second.total);
                kv.second.count_1s = 0;
            }
            fprintf(stderr, "=================================\n");
        }

        // 2. Periodic Workload Status every 10 seconds
        if (sec % 10 == 0) {
            uint32_t spurs_ea = 0x7001AD00; // Big Spurs instance
            struct WkldInfo {
                const char* name;
                uint32_t wid;
                uint32_t ea;
            };
            WkldInfo wks[] = {
                {"SpursTaskset0",  0,  0x70024880},
                {"SpursTaskset1",  1,  0x70027200},
                {"SpursTaskset2",  2,  0x70029B80},
                {"SpursTaskset3",  3,  0x74E65100},
                {"BrBDblBufWrkld", 8,  0x009FDF80},
                {"RenJobSet",      9,  0x71600580},
                {"RenJobSet10",    10, 0x71632600},
                {"RenJobSet11",    11, 0x71664680},
                {"RenJobSet12",    12, 0x71696700},
                {"RenderJobs",     13, 0x71965600},
                {"MeshProcJobs",   14, 0x71965B80},
                {"SpursWkld16",    16, 0x73D6B900},
                {"RsxSync18",      18, 0x73E83D80}
            };

            fprintf(stderr, "\n=== [TASK4-WORKLOAD-STATUS @ %ds] ===\n", sec);
            for (auto& w : wks) {
                uint8_t ready = 0;
                uint8_t state = 0;
                if (vm_base && spurs_ea) {
                    uint32_t ready_off = (w.wid < 16) ? w.wid : (0x10 + (w.wid - 16));
                    uint32_t state_off = (w.wid < 16) ? (0x80 + w.wid) : (0xD0 + (w.wid - 16));
                    ready = *(vm_base + spurs_ea + ready_off);
                    state = *(vm_base + spurs_ea + state_off);
                }
                uint32_t pm_entries = (w.wid < 32) ? g_spurs_pm_entry_count[w.wid] : 0;
                fprintf(stderr, "Workload '%s' (wid=%u, data_ea=0x%08X): state=%u readyCount=%u, pm_entries=%u%s\n",
                        w.name, w.wid, w.ea, (unsigned)state, (unsigned)ready, pm_entries,
                        (w.wid >= 9 && pm_entries > 0) ? " [WID-RUNNING]" : "");
            }
            fprintf(stderr, "======================================\n\n");
            fprintf(stderr, "=== [SPU-LANE-AUDIT @ %ds] ===\n", sec);
            for (int l = 0; l < 6; l++) {
                uint32_t active_wid = g_spurs_lane_active_wid[l];
                fprintf(stderr, "  Lane %d: wid=%u (%s), last 5 MFC commands:\n",
                        l, active_wid, (active_wid == 255) ? "IDLE/KERNEL" : "RUNNING");
                uint32_t hidx = g_spu_mfc_hist_idx[l];
                for (uint32_t i = 0; i < 5 && i < hidx; i++) {
                    uint32_t slot = (hidx >= 5) ? (hidx - 5 + i) % 5 : i;
                    const auto& m = g_spu_mfc_hist[l][slot];
                    fprintf(stderr, "    [%llums] cmd=0x%02X pc=0x%05X lsa=0x%05X ea=0x%08X size=%u tag=%u\n",
                            (unsigned long long)m.t_ms, m.cmd, m.pc, m.lsa, m.ea, m.size, m.tag);
                }
            }
            fprintf(stderr, "==============================\n");
        }

        // 3. Periodic Thread Audit every 10 seconds
        if (sec % 10 == 0) {
            fprintf(stderr, "\n=== [TASK4-THREAD-AUDIT @ %ds] ===\n", sec);
            uint32_t main_lr = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->lr : 0;
            uint32_t main_r3 = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->gpr[3] : 0;
            fprintf(stderr, "Thread tid=1 [main]: state=RUNNING entry=0x00010000 lr=0x%08X r3=0x%08X last_hle=%s (obj=0x%llX)\n",
                    main_lr, main_r3,
                    g_ppu_thread_diags[1].last_hle_name ? g_ppu_thread_diags[1].last_hle_name : "(none)",
                    (unsigned long long)g_ppu_thread_diags[1].blocked_obj);
            fprintf(stderr, "  Last 5 HLE:");
            uint32_t hidx = g_ppu_thread_diags[1].hle_history_idx;
            for (uint32_t i = 0; i < 5 && i < hidx; i++) {
                uint32_t slot = (hidx >= 5) ? (hidx - 5 + i) % 5 : i;
                fprintf(stderr, " %s", g_ppu_thread_diags[1].hle_history_name[slot] ? g_ppu_thread_diags[1].hle_history_name[slot] : "?");
            }
            fprintf(stderr, "\n");

            if (vm_base) {
                uint32_t gcm_ctrl_ea = *(const uint32_t*)(vm_base + 0x009BE004);
                gcm_ctrl_ea = __builtin_bswap32(gcm_ctrl_ea);
                if (gcm_ctrl_ea && gcm_ctrl_ea < 0xFFFFFFF0u) {
                    uint32_t gcm_get = *(const uint32_t*)(vm_base + gcm_ctrl_ea + 0x04);
                    uint32_t gcm_put = *(const uint32_t*)(vm_base + gcm_ctrl_ea);
                    fprintf(stderr, "  GCM FIFO ctrl (0x%08X): put=0x%08X get=0x%08X\n",
                            gcm_ctrl_ea, __builtin_bswap32(gcm_put), __builtin_bswap32(gcm_get));
                }
            }

            for (uint32_t tid = 2; tid <= PPU_THREAD_MAX; tid++) {
                ppu_thread_info* t = &g_ppu_threads[tid - 1];
                if (t->state == PPU_THREAD_STATE_FREE) continue;
                const char* state_str = (t->state == PPU_THREAD_STATE_RUNNING) ? "RUNNING" : "WAITING";
                fprintf(stderr, "Thread tid=%u [%s]: state=%s entry=0x%08llX lr=0x%08X last_hle=%s (obj=0x%llX)\n",
                        tid, t->name[0] ? t->name : "unnamed", state_str,
                        (unsigned long long)t->entry_addr, (uint32_t)t->ctx.lr,
                        g_ppu_thread_diags[tid].last_hle_name ? g_ppu_thread_diags[tid].last_hle_name : "(none)",
                        (unsigned long long)g_ppu_thread_diags[tid].blocked_obj);
                fprintf(stderr, "  Last 5 HLE:");
                uint32_t thidx = g_ppu_thread_diags[tid].hle_history_idx;
                for (uint32_t i = 0; i < 5 && i < thidx; i++) {
                    uint32_t slot = (thidx >= 5) ? (thidx - 5 + i) % 5 : i;
                    fprintf(stderr, " %s", g_ppu_thread_diags[tid].hle_history_name[slot] ? g_ppu_thread_diags[tid].hle_history_name[slot] : "?");
                }
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "===================================\n\n");
        }
        fflush(stderr);
    }
}

static void task4_log_spurs_call(uint32_t tid, const char* name, ppu_context* ctx)
{
    if (!s_task4_monitor_started.exchange(true)) {
        std::thread(task4_monitor_thread_func).detach();
    }

    if (!name) return;

    double wall = (double)ps3_qpc_us() / 1e6;
    if (wall <= 60.0) {
        uint32_t r3 = ctx ? (uint32_t)ctx->gpr[3] : 0;
        uint32_t r4 = ctx ? (uint32_t)ctx->gpr[4] : 0;
        uint32_t r5 = ctx ? (uint32_t)ctx->gpr[5] : 0;
        uint32_t r6 = ctx ? (uint32_t)ctx->gpr[6] : 0;
        uint32_t lr = ctx ? (uint32_t)ctx->lr : 0;
        static uint32_t s_spurs_log_count = 0;
        if (++s_spurs_log_count <= 250 || (s_spurs_log_count % 100 == 0)) {
            fprintf(stderr, "[TASK4-SPURS-CALL #%u @ %.3fs] %s tid=%u r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X lr=0x%08X\n",
                    s_spurs_log_count, wall, name, tid, r3, r4, r5, r6, lr);
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_task4_mutex);
        s_task4_spurs_calls[name].count_1s++;
        s_task4_spurs_calls[name].total++;
    }
}

extern "C" {
    extern uint32_t cellGcmGetCurrentDisplayBufferId(void);

    GOW_WEAK uint32_t cellGcm_current_display_buffer_id(void) {
        return cellGcmGetCurrentDisplayBufferId();
    }

    GOW_WEAK int cellGcm_get_display_buffer(uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*) {
        return 0;
    }

    GOW_WEAK void ppu_hle_dump_summary_table(void) {}

    GOW_WEAK int ppu_diag_get_thread_status(uint32_t tid, uint32_t* out_lr, char* out_name, size_t name_sz, int* out_state) {
        if (tid == 0 || tid > PPU_THREAD_MAX) return 0;
        if (tid == 1) {
            if (out_lr) *out_lr = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->lr : 0;
            if (out_name && name_sz > 0) {
                strncpy(out_name, "main", name_sz - 1);
                out_name[name_sz - 1] = 0;
            }
            if (out_state) *out_state = PPU_THREAD_STATE_RUNNING;
            return 1;
        }
        ppu_thread_info* t = &g_ppu_threads[tid - 1];
        if (t->state == PPU_THREAD_STATE_FREE) return 0;
        if (out_lr) *out_lr = (uint32_t)t->ctx.lr;
        if (out_name && name_sz > 0) {
            strncpy(out_name, t->name, name_sz - 1);
            out_name[name_sz - 1] = 0;
        }
        if (out_state) *out_state = t->state;
        return 1;
    }

    GOW_WEAK uint32_t ppu_diag_get_spurs_pc(uint32_t) {
        return 0;
    }

    GOW_WEAK void cellGcm_get_and_reset_spu_dma_stats_5s(uint64_t* c, uint64_t* b, uint64_t* sw, uint64_t* pw) {
        if (c) *c = 0; if (b) *b = 0; if (sw) *sw = 0; if (pw) *pw = 0;
    }

    GOW_WEAK void cellGcm_get_and_reset_fifo_stats_5s(uint64_t* c, uint64_t* w, double* m) {
        if (c) *c = 0; if (w) *w = 0; if (m) *m = 0.0;
    }

    GOW_WEAK int cellGcm_get_distinct_color_a_count(void) {
        return 0;
    }

    GOW_WEAK int cellGcm_get_distinct_color_a(int, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*) {
        return 0;
    }

    GOW_WEAK void cellAudio_print_stats_5s(char* b, size_t n) {
        if (b && n) b[0] = 0;
    }

    GOW_WEAK int cellAudio_get_ring_nonzero_summary(char* b, size_t n) {
        if (b && n) b[0] = 0;
        return 0;
    }

    GOW_WEAK void gow_print_pump_callers(void) {}

    GOW_WEAK void cellGcm_invoke_vblank_handler_direct(void) {}

    GOW_WEAK GcmFifoBreakdown g_gcm_fifo_breakdown = {0};
    GOW_WEAK volatile uint64_t g_ppu_gcm_pump_calls_by_tid[64] = {0};
    GOW_WEAK ppu_thread_diag_t g_ppu_thread_diags[64] = {0};
    GOW_WEAK spurs_wid_diag_t g_spurs_wid_diags[32] = {0};
    GOW_WEAK volatile uint64_t g_vblank_handler_invocations = 0;
    GOW_WEAK volatile uint64_t g_vblank_ticks_count = 0;

    GOW_WEAK void ppu_diag_set_wait(uint32_t, int, uint64_t) {}
    GOW_WEAK void ppu_diag_clear_wait(uint32_t) {}
    static volatile int g_rem_zero_active = 0;
    GOW_WEAK void gow_notify_rem_zero(int active) {
        g_rem_zero_active = active;
    }

    struct HleStatEntry {
        uint32_t nid;
        const char* name;
        uint64_t count;
    };
    static HleStatEntry g_hle_after_rem0_table[512];
    static uint32_t g_hle_after_rem0_count = 0;

    GOW_WEAK void gow_print_top25_hle_after_rem0(void) {
        if (g_hle_after_rem0_count == 0) {
            fprintf(stderr, "[TOP25-HLE-AFTER-REM0] No HLE calls recorded after rem=0 yet.\n");
            return;
        }
        // sort by count
        for (uint32_t i = 0; i < g_hle_after_rem0_count; i++) {
            for (uint32_t j = i + 1; j < g_hle_after_rem0_count; j++) {
                if (g_hle_after_rem0_table[j].count > g_hle_after_rem0_table[i].count) {
                    HleStatEntry tmp = g_hle_after_rem0_table[i];
                    g_hle_after_rem0_table[i] = g_hle_after_rem0_table[j];
                    g_hle_after_rem0_table[j] = tmp;
                }
            }
        }
        fprintf(stderr, "\n=== [TOP 25 HLE CALLS AFTER REM=0] ===\n");
        for (uint32_t i = 0; i < g_hle_after_rem0_count && i < 25; i++) {
            fprintf(stderr, "  #%02u: %-32s (0x%08X) -> %llu calls\n",
                    i + 1, g_hle_after_rem0_table[i].name ? g_hle_after_rem0_table[i].name : "unknown",
                    g_hle_after_rem0_table[i].nid, (unsigned long long)g_hle_after_rem0_table[i].count);
        }
        fprintf(stderr, "=======================================\n\n");
    }

    GOW_WEAK void ppu_diag_enter_hle(uint32_t tid, uint32_t nid, const char* name) {
        if (!s_task4_monitor_started.exchange(true)) {
            std::thread(task4_monitor_thread_func).detach();
        }
        if (tid == 1) { gow_register_main_thread(); }
        if (tid < 64) {
            ppu_context* c = (tid == 1) ? g_ppu_main_ctx : (tid > 0 && tid <= PPU_THREAD_MAX ? &g_ppu_threads[tid - 1].ctx : nullptr);
            uint64_t arg0 = c ? c->gpr[3] : 0;
            g_ppu_thread_diags[tid].last_hle_nid = nid;
            g_ppu_thread_diags[tid].last_hle_name = name;
            g_ppu_thread_diags[tid].blocked_obj = arg0;
            g_ppu_thread_diags[tid].last_hle_name = name;
            uint32_t idx = g_ppu_thread_diags[tid].hle_history_idx % 5;
            g_ppu_thread_diags[tid].hle_history_nid[idx] = nid;
            g_ppu_thread_diags[tid].hle_history_name[idx] = name;
            g_ppu_thread_diags[tid].hle_history_idx++;
        if (nid == 0xC0158D8Bu) { // cellSpursAddWorkloadWithAttribute
            static uint32_t s_reg_seq = 0;
            uint32_t reg_id = s_reg_seq++;
            uint64_t t_now = ps3_qpc_us();
            uint32_t pass_num = 0;
            if (s_spurs_inst_ptr) {
                pass_num = *(uint32_t*)((uint8_t*)s_spurs_inst_ptr + 0x40 + 6*64 + 64 + 4 + 4);
            }
            fprintf(stderr, "[REGISTRATION-ENTER] call #%u at %.3fs, kernel_pass=%u\n",
                    reg_id, (double)t_now / 1e6, pass_num);
            fflush(stderr);
        }
        if (name && (strncmp(name, "cellSpurs", 9) == 0 ||
                     strncmp(name, "_cellSpurs", 10) == 0)) {
            ppu_context* c = (tid == 1) ? g_ppu_main_ctx : (tid > 0 && tid <= PPU_THREAD_MAX ? &g_ppu_threads[tid - 1].ctx : nullptr);
            task4_log_spurs_call(tid, name, c);
        }
        }
        if (g_rem_zero_active) {
            uint32_t slot = 0xFFFFFFFFu;
            for (uint32_t i = 0; i < g_hle_after_rem0_count; i++) {
                if (g_hle_after_rem0_table[i].nid == nid) { slot = i; break; }
            }
            if (slot == 0xFFFFFFFFu && g_hle_after_rem0_count < 512) {
                slot = g_hle_after_rem0_count++;
                g_hle_after_rem0_table[slot].nid = nid;
                g_hle_after_rem0_table[slot].name = name;
                g_hle_after_rem0_table[slot].count = 0;
            }
            if (slot != 0xFFFFFFFFu) {
                g_hle_after_rem0_table[slot].count++;
            }
        }
        if (name) {
            if (strncmp(name, "cellSail", 8) == 0 ||
                strncmp(name, "cellPamf", 8) == 0 ||
                strncmp(name, "cellVdec", 8) == 0 ||
                strncmp(name, "cellDmux", 8) == 0 ||
                strcmp(name, "cellAudioAddData") == 0 ||
                strcmp(name, "cellGcmSetDisplayBuffer") == 0) {
                static uint64_t s_spec_count = 0;
                if (++s_spec_count <= 50 || (s_spec_count % 100 == 0)) {
                    extern unsigned long long ps3_qpc_us(void);
                    double wall = (double)ps3_qpc_us() / 1e6;
                    fprintf(stderr, "[SPECIAL-CALL #%llu @ %.3fs] %s (nid=0x%08X tid=%u)\n",
                            (unsigned long long)s_spec_count, wall, name, nid, tid);
                }
            }
        }
    }
    GOW_WEAK uint32_t ppu_diag_get_current_tid(void) {
        extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
        return g_active_ctx ? (uint32_t)g_active_ctx->thread_id : 0;
    }
    GOW_WEAK uint32_t ppu_diag_get_current_lr(void) {
        extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
        return g_active_ctx ? (uint32_t)g_active_ctx->lr : 0;
    }

    GOW_WEAK void ppu_gcm_pump_caller(uint32_t) {}

    GOW_WEAK void cellFiberPpuInitialize(void) {}
    GOW_WEAK void cellFiberPpuFinalize(void) {}
    GOW_WEAK void cellFiberPpuDeleteFiber(void) {}
    GOW_WEAK void cellFiberPpuSwitchFiber(void) {}
    GOW_WEAK void cellFiberPpuYieldFiber(void) {}
    GOW_WEAK void cellFiberPpuExitFiber(void) {}
    GOW_WEAK void cellFiberPpuGetCurrentFiber(void) {}
    GOW_WEAK void cellFiberPpuGetFiberState(void) {}
    GOW_WEAK void cellFiberPpuAttributeInitialize(void) {}
    GOW_WEAK void cellFiberPpuSleep(void) {}
    GOW_WEAK void cellFiberPpuWakeup(void) {}

    extern void gow_psn_stubs_register(void);

    void spu_begin_image(int image_id);
    void spu_register_function(uint32_t addr, void (*fn)(spu_context*));
    void spu_wws_job_pm_spu_func_00000040(spu_context* ctx);
    void spu_wws_job_pm_spu_func_00002358(spu_context* ctx);
    void spu_indirect_branch(spu_context* ctx);

    static void gow_wws_job_pm_interrupt_handler(spu_context* ctx)
    {
        static int s_logged = 0;
        if (s_logged++ < 16) {
            fprintf(stderr, "[wws-irq] SPU img=2 interrupt 0x00A2C taken (stallstat=0x%X srr0=0x%05X loadJobState=%02X%02X%02X%02X storeJobNum=%02X)\n",
                    ctx->list_stall_stat, ctx->srr0,
                    ctx->ls[0x12A0], ctx->ls[0x12A1], ctx->ls[0x12A2], ctx->ls[0x12A3], ctx->ls[0x12F0]);
        }

        uint16_t loadJobState = ((uint16_t)ctx->ls[0x12A0] << 8) | ctx->ls[0x12A1];
        int8_t storeJobNum = (int8_t)ctx->ls[0x12F0];
        if (loadJobState == 0 && storeJobNum < 0) {
            static int s_preempt_count = 0;
            if (++s_preempt_count <= 50) {
                fprintf(stderr, "[wws-irq] PREEMPT YIELD: SPU img=2 lane idle -> exitToKernel (0x9C0) srr0=0x%05X\n", ctx->srr0);
            }
            ctx->pc = 0x9C0u;
            g_spu_trampoline_fn = spu_indirect_branch;
            return;
        }

        memcpy(ctx->ls + 0x1590, &ctx->gpr[78], 16);
        memset(&ctx->gpr[78], 0, sizeof(ctx->gpr[78]));
        spu_wws_job_pm_spu_func_00000040(ctx);
    }

    static void gow_wws_job_pm_idle_spin_handler(spu_context* ctx)
    {
        uint16_t loadJobState = ((uint16_t)ctx->ls[0x12A0] << 8) | ctx->ls[0x12A1];
        int8_t storeJobNum = (int8_t)ctx->ls[0x12F0];

        if (loadJobState == 0 && storeJobNum < 0) {
            const uint8_t* q = ctx->ls + 0x1C4;
            const uint8_t* k = ctx->ls + 0x1DC;
            uint32_t spurs_ea = ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 8) | q[3];
            uint32_t cur_wid = ((uint32_t)k[0] << 24) | ((uint32_t)k[1] << 16) | ((uint32_t)k[2] << 8) | k[3];

            if (spurs_ea && vm_base) {
                uint32_t enabled = ((uint32_t)vm_base[spurs_ea + 0xB0] << 24) |
                                   ((uint32_t)vm_base[spurs_ea + 0xB1] << 16) |
                                   ((uint32_t)vm_base[spurs_ea + 0xB2] << 8) |
                                   vm_base[spurs_ea + 0xB3];

                bool other_waiting = false;
                for (uint32_t w = 0; w < 32; w++) {
                    if (w == cur_wid) continue;
                    if (!(enabled & (0x80000000u >> w))) continue;
                    uint8_t state = vm_base[spurs_ea + (w < 16 ? 0x80 + w : 0xD0 + (w - 16))];
                    if (state != 2) continue;
                    uint8_t ready = vm_base[spurs_ea + (w < 16 ? w : 0x10 + (w - 16))];
                    if (ready > 0) {
                        other_waiting = true;
                        break;
                    }
                }

                uint8_t cur_ready = vm_base[spurs_ea + (cur_wid < 16 ? cur_wid : 0x10 + (cur_wid - 16))];

                if (other_waiting || cur_ready == 0) {
                    static int s_spin_yield = 0;
                    if (++s_spin_yield <= 50 || (s_spin_yield % 500 == 0)) {
                        fprintf(stderr, "[wws-idle] IDLE YIELD: SPU img=2 wid=%u lane idle (other_waiting=%d cur_ready=%u) -> exitToKernel (0x9C0)\n",
                                cur_wid, other_waiting ? 1 : 0, cur_ready);
                    }
                    ctx->pc = 0x9C0u;
                    g_spu_trampoline_fn = spu_indirect_branch;
                    return;
                }
            }
        }

        spu_wws_job_pm_spu_func_00002358(ctx);
    }

    void gow_register_spu_overrides(void)
    {
        static bool s_registered = false;
        if (s_registered) return;
        s_registered = true;

        if (getenv("GOW_REVERT_DDE3F83") || getenv("GOW_NO_SPU_OVERRIDES")) {
            fprintf(stderr, "[ascension] SPU img=2 overrides DISABLED by env (GOW_REVERT_DDE3F83/GOW_NO_SPU_OVERRIDES)\n");
            return;
        }

        spu_begin_image(2);
        spu_register_function(0x00A2Cu, gow_wws_job_pm_interrupt_handler);
        spu_register_function(0x00040u, gow_wws_job_pm_interrupt_handler);
        spu_register_function(0x02358u, gow_wws_job_pm_idle_spin_handler);
        spu_begin_image(0);
        fprintf(stderr, "[ascension] Registered SPU img=2 overrides: 0x00A2C, 0x00040, 0x02358\n");
    }

    void ps3_load_prx_modules(void) {
        gow_psn_stubs_register();
        gow_register_spu_overrides();
    }

}
