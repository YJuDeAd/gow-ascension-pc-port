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
            extern ppu_context* volatile g_ppu_main_ctx;
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
        if (tid < 64) {
            g_ppu_thread_diags[tid].last_hle_nid = nid;
            g_ppu_thread_diags[tid].last_hle_name = name;
            uint32_t idx = g_ppu_thread_diags[tid].hle_history_idx % 5;
            g_ppu_thread_diags[tid].hle_history_nid[idx] = nid;
            g_ppu_thread_diags[tid].hle_history_name[idx] = name;
            g_ppu_thread_diags[tid].hle_history_idx++;
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
    GOW_WEAK uint32_t ppu_diag_get_current_tid(void) { return 0; }

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
}
