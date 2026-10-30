/*
 * upstream_compat.cpp — Fallback definitions for compiling against upstream ps3recomp baseline.
 *
 * Uses weak symbols so that when building against an upstream ps3recomp commit without
 * port-specific telemetry or diagnostics, the symbols resolve cleanly.
 */

#include "ps3_diagnostics.h"
#include "sys_ppu_thread.h"
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
    hook_iat(self, "getenv", (void*)my_getenv, (void**)&g_real_getenv);
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

static void task4_monitor_thread_func();

extern "C" void gow_register_main_thread(void)
{
    init_lock_hooks();
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
                {"MeshProcJobs",   14, 0x71965B80}
            };

            fprintf(stderr, "\n=== [TASK4-WORKLOAD-STATUS @ %ds] ===\n", sec);
            for (auto& w : wks) {
                uint8_t ready = 0;
                if (vm_base && spurs_ea) {
                    uint32_t ready_off = (w.wid < 16) ? (0x90 + w.wid) : (0xE0 + (w.wid - 16));
                    ready = *(vm_base + spurs_ea + ready_off);
                }
                uint32_t pm_entries = (w.wid < 32) ? g_spurs_pm_entry_count[w.wid] : 0;
                fprintf(stderr, "Workload '%s' (wid=%u, data_ea=0x%08X): readyCount=%u, pm_entries=%u%s\n",
                        w.name, w.wid, w.ea, (unsigned)ready, pm_entries,
                        (w.wid >= 9 && pm_entries > 0) ? " [WID-RUNNING]" : "");
            }
            fprintf(stderr, "======================================\n\n");
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
        } // if (s_gow_watchdog)
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
    void ps3_load_prx_modules(void) {
        gow_psn_stubs_register();
    }

}
