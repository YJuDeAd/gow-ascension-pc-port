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
volatile uint32_t g_spurs_pm_entry_count[32] = {0};
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

static void task4_monitor_thread_func()
{
    static bool s_seen_thread[PPU_THREAD_MAX + 1] = {false};

    for (int sec = 1; sec <= 300; sec++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

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

        // Boot & MFC Telemetry
        if (sec == 5 || sec == 10 || sec == 15) {
            uint32_t lr = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->lr : 0;
            uint64_t r28 = g_ppu_main_ctx ? g_ppu_main_ctx->gpr[28] : 0;
            uint64_t r30 = g_ppu_main_ctx ? g_ppu_main_ctx->gpr[30] : 0;
            uint32_t val_r28 = 0, bit29 = 0;
            uint32_t tmo_r30 = 0;
            if (vm_base && r28 > 0 && r28 < 0xFF000000ull) {
                val_r28 = *(const uint32_t*)(vm_base + (uint32_t)r28 + 0x64);
                bit29 = (val_r28 >> 2) & 1; // PowerPC bit 29
            }
            if (vm_base && r30 > 0 && r30 < 0xFF000000ull) {
                tmo_r30 = *(const uint32_t*)(vm_base + (uint32_t)r30 + 0x2B50);
            }
            extern volatile unsigned long long g_mfc_list_stall_count;
            extern volatile unsigned long long g_mfc_list_resume_count;
            fprintf(stderr, "[BOOT-DIAG @ %ds] lr=0x%08X r28=0x%llX [r28+0x64]=0x%08X (bit29=%u) r30=0x%llX [r30+0x2B50]=0x%08X (%d) | MFC stalls=%llu resumes=%llu\n",
                    sec, lr, (unsigned long long)r28, val_r28, bit29,
                    (unsigned long long)r30, tmo_r30, (int)tmo_r30,
                    g_mfc_list_stall_count, g_mfc_list_resume_count);
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

    void spu_begin_image(int image_id);
    void spu_register_function(uint32_t addr, void (*fn)(spu_context*));
    void spu_indirect_branch(spu_context* ctx);
    void spu_wws_job_pm_spu_func_00000040(spu_context* ctx);

    static void gow_wws_job_pm_3fec0_handler(spu_context* ctx)
    {
        if (ctx->image_id != 2) return;
        // Dynamic shim at 0x3FEC0 loaded into high LS by SPURS; return to link in r79
        ctx->pc = ctx->gpr[79]._u32[0] & SPU_LS_MASK;
        if (ctx->host_depth == 0) {
            g_spu_trampoline_fn = spu_indirect_branch;
        }
    }

    static void gow_wws_job_pm_a2c_handler(spu_context* ctx)
    {
        if (ctx->image_id != 2) return;

        uint32_t saved_srr0 = ctx->srr0;
        u128 saved_gprs[128];
        memcpy(saved_gprs, ctx->gpr, sizeof(saved_gprs));

        static int s_irq_count = 0;
        s_irq_count++;
        if (s_irq_count <= 16) {
            fprintf(stderr, "[wws-irq] SPU img=2 IRQ #%d at 0x00A2C: r0=0x%08X r1=0x%08X srr0=0x%05X stallstat=0x%X loadJobState=%02X%02X%02X%02X\n",
                    s_irq_count,
                    ctx->gpr[0]._u32[0], ctx->gpr[1]._u32[0],
                    ctx->srr0, ctx->list_stall_stat,
                    ctx->ls[0x12A0], ctx->ls[0x12A1], ctx->ls[0x12A2], ctx->ls[0x12A3]);
        }

        // Vector 0x00A2C: save r78 to 0x1590, zero r78
        memcpy(ctx->ls + 0x1590, &ctx->gpr[78], 16);
        memset(&ctx->gpr[78], 0, sizeof(ctx->gpr[78]));

        // Call lifted interrupt handler at 0x40 (which executes 0x40->0x80->0x3FEC0->0x1990->0xD0->iret)
        spu_wws_job_pm_spu_func_00000040(ctx);

        // Verify registers after round-trip
        int diff_count = 0;
        for (int r = 0; r < 128; r++) {
            if (memcmp(&saved_gprs[r], &ctx->gpr[r], sizeof(u128)) != 0) {
                diff_count++;
                if (s_irq_count <= 4) {
                    fprintf(stderr, "  [wws-irq-diff] r%d modified: before=0x%08X after=0x%08X\n",
                            r, saved_gprs[r]._u32[0], ctx->gpr[r]._u32[0]);
                }
            }
        }
        if (s_irq_count <= 16) {
            fprintf(stderr, "[wws-irq] SPU img=2 IRQ #%d complete: pc=0x%05X (srr0=0x%05X) diff_regs=%d loadJobState=%02X%02X%02X%02X\n",
                    s_irq_count, ctx->pc, saved_srr0, diff_count,
                    ctx->ls[0x12A0], ctx->ls[0x12A1], ctx->ls[0x12A2], ctx->ls[0x12A3]);
        }
    }

    void gow_register_spu_overrides(void)
    {
        static int s_registered = 0;
        if (s_registered) return;
        s_registered = 1;

        spu_begin_image(2);
        spu_register_function(0x3FEC0u, gow_wws_job_pm_3fec0_handler);
        spu_register_function(0x00A2Cu, gow_wws_job_pm_a2c_handler);
        spu_begin_image(0);

        fprintf(stderr, "[ascension] Registered SPU img=2 overrides: 0x3FEC0 shim return, IRQ 0x00A2C\n");
    }

    __attribute__((constructor(101))) static void gow_spu_overrides_ctor(void)
    {
        gow_register_spu_overrides();
    }

    extern void gow_psn_stubs_register(void);
    void ps3_load_prx_modules(void) {
        gow_register_spu_overrides();
        gow_psn_stubs_register();
        if (!s_task4_monitor_started.exchange(true)) {
            std::thread(task4_monitor_thread_func).detach();
        }
    }

}
