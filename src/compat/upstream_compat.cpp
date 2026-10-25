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
            if (out_lr) *out_lr = 0;
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
    GOW_WEAK void ppu_diag_enter_hle(uint32_t, uint32_t, const char*) {}
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
