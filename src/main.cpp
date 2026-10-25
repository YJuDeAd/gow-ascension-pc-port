/*
 * main.cpp — Native x86-64 PPU boot harness for God of War: Ascension (PS3).
 *
 * Links lifted PPU code, runtime loader, HLE bridge, and PSN/system stubs into
 * a native Windows executable that loads EBOOT.ELF, registers recompiled functions
 * and NID handlers, and executes guest game code natively on host x86-64 hardware.
 */

#include "ppu_recomp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <timeapi.h>
#endif

#include "ps3_diagnostics.h"

extern "C" {
uint32_t ppu_load_elf(const char* path);
void     ppu_recomp_register(void);
void     ppu_hle_init(void);
void     ppu_sysprx_register(void);
void     ppu_fs_register(void);
void     ascension_hle_register(void);   /* src/compat/ascension_hle.cpp */
void     ascension_post_syscall_init(void); /* src/compat/ascension_hle.cpp */
void     gow_hle_register_extra(void);   /* alias */
void     gow_psn_stubs_register(void);   /* src/compat/psn_stubs.cpp */
void     gow_print_flip_reconciliation(void); /* src/compat/ascension_hle.cpp */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);
extern const char* ppu_vfs_root;

/* Optional hook: load real system PRX modules if linked */
void     ps3_load_prx_modules(void) __attribute__((weak));
void     ps3_load_prx_modules(void) {}
}

#ifdef _WIN32
extern "C" uint32_t    g_last_hle_nid;
extern "C" const char* g_last_hle_name;
extern "C" thread_local ppu_context* g_active_ctx;

static LONG WINAPI gow_crash_filter(EXCEPTION_POINTERS* ep)
{
    EXCEPTION_RECORD* er = ep->ExceptionRecord;
    fprintf(stderr, "\n[CRASH] code=0x%08lX rip=%p\n",
            (unsigned long)er->ExceptionCode, er->ExceptionAddress);
    fprintf(stderr, "[CRASH] last HLE NID 0x%08X (%s)\n",
            g_last_hle_nid, g_last_hle_name ? g_last_hle_name : "");
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        fprintf(stderr, "[CRASH] %s fault address 0x%llX\n",
                er->ExceptionInformation[0] ? "write" : "read",
                (unsigned long long)er->ExceptionInformation[1]);
    if (g_active_ctx)
        fprintf(stderr, "[CRASH] guest ctr=0x%08X lr=0x%08X r3=0x%08X\n",
                (uint32_t)g_active_ctx->ctr, (uint32_t)g_active_ctx->lr, (uint32_t)g_active_ctx->gpr[3]);

    HMODULE mod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)er->ExceptionAddress, &mod);
    char mod_name[MAX_PATH] = "unknown";
    if (mod) GetModuleFileNameA(mod, mod_name, sizeof(mod_name));
    fprintf(stderr, "[CRASH] module=%p (%s) rva=0x%llX\n",
            (void*)mod, mod_name, (unsigned long long)((char*)er->ExceptionAddress - (char*)mod));

    void* frames[24];
    USHORT n = RtlCaptureStackBackTrace(0, 24, frames, NULL);
    for (USHORT i = 0; i < n; i++) {
        HMODULE m = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &m);
        if (m == mod)
            fprintf(stderr, "[CRASH]   #%-2u rva=0x%llX\n", i,
                    (unsigned long long)((char*)frames[i] - (char*)m));
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void gow_abort_handler(int)
{
    fprintf(stderr, "\n[ABORT] SIGABRT raised; last HLE NID 0x%08X (%s)\n",
            g_last_hle_nid, g_last_hle_name ? g_last_hle_name : "");
    void* frames[32];
    USHORT n = RtlCaptureStackBackTrace(0, 32, frames, NULL);
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&gow_abort_handler, &self);
    for (USHORT i = 0; i < n; i++) {
        HMODULE m = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &m);
        if (m == self)
            fprintf(stderr, "[ABORT]   #%-2u rva=0x%llX\n", i,
                    (unsigned long long)((char*)frames[i] - (char*)m));
    }
    fflush(stderr);
    _exit(3);
}
#endif

/* Derive VFS root (<root>/PS3_GAME/USRDIR/EBOOT.ELF -> <root>) */
static char s_vfs_root[1024];
static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }
#ifdef _WIN32
    /* Zero-copy asset streaming: check if mounted ISO on E: exists */
    DWORD attr = GetFileAttributesA("E:\\PS3_GAME\\USRDIR");
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        ppu_vfs_root = "E:";
        return;
    }
#endif
    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* Host symbols needed by runtime */
extern "C" uint8_t* vm_base = nullptr;
extern "C" uint32_t ppu_vm_size;
extern "C" void lv2_init_syscalls(void);

typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" ps3_guest_caller_fn g_ps3_guest_caller;
extern "C" uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" void cellGcmTickVBlank(void);
extern "C" void cellGcmTickFlip(void);
extern "C" void ppu_gcm_pump(void);
extern "C" int  cellGcm_take_flip_pending(void);
extern "C" void cellGcm_request_tick(void);

static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                                 uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

#ifdef _WIN32
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
extern "C" void cellGcm_rsx_process_fifo(void);
extern "C" int  cellGcm_take_flip_pending_synced(void);
extern "C" void cellGcmTickFlip(void);

extern "C" int   rsx_live_draw_enabled(void);
extern "C" int   rsx_live_draw_init(void* hwnd, uint32_t w, uint32_t h,
                                    const uint8_t* (*guest_ptr)(void*, uint32_t, uint32_t, uint32_t),
                                    void* user);
extern "C" int   rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" int   rsx_null_backend_pump_messages(void);
extern "C" void* rsx_null_backend_get_hwnd(void);
extern "C" void  rsx_null_backend_suppress_present(int on);
extern "C" uint32_t cellGcmResolveLocated(int local, uint32_t offset);
extern "C" uint32_t cellGcmResolveIO(uint32_t offset);

static const uint8_t* gow_live_guest_ptr(void* user, uint32_t location,
                                         uint32_t offset, uint32_t min_bytes)
{
    (void)user;
    if (!vm_base) return nullptr;
    uint32_t ea;
    if (location == 0) {
        ea = cellGcmResolveLocated(1, offset);
    } else {
        ea = cellGcmResolveIO(offset);
        if (!ea) ea = cellGcmResolveLocated(0, offset);
    }
    const bool ok = ea && ea != 0xFFFFFFFFu &&
                    (uint64_t)ea + min_bytes <= 0x100000000ull;
    if (!ok) return nullptr;
    return (const uint8_t*)vm_base + ea;
}

extern "C" void     rsx_live_draw_present(uint32_t buffer_id);
extern "C" uint32_t rsx_live_draw_get_frames(void);
extern "C" uint32_t rsx_live_draw_get_last_draws(void);
extern "C" int      cellGcm_take_flip_pending(void);
extern "C" uint32_t cellGcm_get_current_display_buffer(void);
extern "C" uint32_t cellGcm_current_display_buffer_id(void);
extern "C" int      cellGcm_take_flip_pending_synced(void);
extern "C" uint32_t cellGcm_flip_request_count(void);
extern "C" void     cellGcm_fifo_kick_wait(unsigned ms);
extern "C" uint32_t vm_read32(uint64_t addr);
#define PS3_HLE_INFLIGHT_MAX 64
extern "C" const char* g_hle_inflight[PS3_HLE_INFLIGHT_MAX];
extern "C" void ppu_hle_dump_summary_table(void);
extern "C" void gow_dump_sync_stats(void);
extern "C" void gow_fios_diag_tick(void);
extern "C" void gow_get_last_fs_op(int* out_fd, char* out_path, size_t path_sz, int64_t* out_off, uint64_t* out_sz, int64_t* out_res, char* out_op, size_t op_sz);

static HANDLE s_hwnd_ready_event = NULL;
static void*  s_shared_hwnd = NULL;
static volatile LONG s_window_closed = 0;

extern "C" volatile uint64_t g_vblank_ticks_count;
extern "C" volatile uint64_t g_vblank_handler_invocations;

extern "C" void cellGcm_get_and_reset_fifo_stats_5s(uint64_t* out_calls, uint64_t* out_words, double* out_max_ms);
extern "C" void cellGcm_get_and_reset_spu_dma_stats_5s(uint64_t* calls, uint64_t* bytes, uint64_t* spu_words, uint64_t* ppu_words);
extern "C" int  cellGcm_get_display_buffer(uint32_t buffer_id, uint32_t* out_offset, uint32_t* out_pitch, uint32_t* out_width, uint32_t* out_height);
extern "C" int  cellGcm_get_distinct_color_a_count(void);
extern "C" int  cellGcm_get_distinct_color_a(int index, uint32_t* out_offset, uint32_t* out_count, uint32_t* out_w, uint32_t* out_h, uint32_t* out_pitch, uint32_t* out_fmt);
extern "C" void cellAudio_print_stats_5s(char* out_buf, size_t max_len);
extern "C" int  cellAudio_get_ring_nonzero_summary(char* out_buf, size_t max_len);
extern "C" void rsx_live_draw_present_rgba(const uint8_t* rgba, uint32_t w, uint32_t h);

struct AudioLwcondStats {
    uint32_t creator_tid;
    uint32_t creator_lr;
    uint32_t creator_cia;
    uint64_t wait_count;
    uint32_t last_waiter_tid;
    uint32_t last_waiter_lr;
    uint64_t signal_count;
    uint32_t last_signaler_tid;
    uint32_t last_signaler_lr;
    uint32_t last_signaler_cia;
};
extern "C" void gow_get_audio_lwcond_stats(AudioLwcondStats* out20, AudioLwcondStats* out50);
extern "C" volatile uint64_t g_ppu_gcm_pump_calls_by_tid[64];
extern "C" volatile uint64_t g_ready_count_store_by_tid[64];

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

extern "C" GcmFifoBreakdown g_gcm_fifo_breakdown;
extern "C" uint32_t cellGcm_get_vblank_handler_opd(void);
extern "C" void cellGcm_invoke_vblank_handler_direct(void);
extern "C" void ppu_resv_register(ppu_context*);

static HANDLE s_vblank_tick_event = NULL;
extern "C" void gow_print_pump_callers(void);
uint32_t vm_read32(uint64_t addr);

static volatile uint64_t g_presents_count = 0;
static volatile uint64_t g_presents_by_buf[8] = {0};
static volatile uint64_t g_flips_rejected_count = 0;
static volatile uint64_t g_fifo_drain_us_total = 0;
static volatile uint64_t g_present_us_total = 0;
static volatile uint32_t g_last_seen_flip_req = 0;

static DWORD WINAPI gow_telemetry_5s_thread(LPVOID)
{
    auto start_time = std::chrono::steady_clock::now();
    auto last_time = start_time;
    uint64_t last_vblank_ticks = 0;
    uint64_t last_vblank_invocations = 0;
    uint64_t last_pump_calls[64] = {0};
    uint32_t last_flip_words = 0;
    uint64_t last_presents = 0;
    uint64_t last_rejected = 0;
    uint64_t last_drain_us = 0;
    uint64_t last_present_us = 0;

    while (!s_window_closed) {
        Sleep(5000);
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_time).count();
        double wall_sec = std::chrono::duration<double>(now - start_time).count();
        last_time = now;

        uint64_t cur_vblank_ticks = g_vblank_ticks_count;
        uint64_t cur_vblank_invocations = g_vblank_handler_invocations;
        double vblank_tick_rate = (dt > 0.0) ? (double)(cur_vblank_ticks - last_vblank_ticks) / dt : 0.0;
        double vblank_inv_rate  = (dt > 0.0) ? (double)(cur_vblank_invocations - last_vblank_invocations) / dt : 0.0;
        last_vblank_ticks = cur_vblank_ticks;
        last_vblank_invocations = cur_vblank_invocations;

        uint32_t cur_flip_words = cellGcm_flip_request_count();
        double flip_words_rate = (dt > 0.0) ? (double)(cur_flip_words - last_flip_words) / dt : 0.0;
        last_flip_words = cur_flip_words;

        uint64_t cur_presents = g_presents_count;
        double presents_rate = (dt > 0.0) ? (double)(cur_presents - last_presents) / dt : 0.0;
        last_presents = cur_presents;

        uint64_t cur_rejected = g_flips_rejected_count;
        double rejected_rate = (dt > 0.0) ? (double)(cur_rejected - last_rejected) / dt : 0.0;
        last_rejected = cur_rejected;

        uint64_t cur_drain_us = g_fifo_drain_us_total;
        double drain_us_per_s = (dt > 0.0) ? (double)(cur_drain_us - last_drain_us) / dt : 0.0;
        last_drain_us = cur_drain_us;

        uint64_t cur_present_us = g_present_us_total;
        double present_us_per_s = (dt > 0.0) ? (double)(cur_present_us - last_present_us) / dt : 0.0;
        last_present_us = cur_present_us;

        uint32_t dl_head = 0, dl_tail = 0, cur_ref = 0;
        if (vm_base) {
            dl_head = vm_read32(0x009711D4u);
            dl_tail = vm_read32(0x009711D8u);
            uint32_t ctrl_ea = vm_read32(0x009BE004u);
            if (ctrl_ea >= 0x10000u) {
                cur_ref = vm_read32(ctrl_ea + 0x08);
            }
        }

        char buf_presents_str[128] = {0};
        snprintf(buf_presents_str, sizeof(buf_presents_str), "buf0=%llu, buf1=%llu",
                 (unsigned long long)g_presents_by_buf[0], (unsigned long long)g_presents_by_buf[1]);

        char pump_buf[512] = {0};
        int offset = 0;
        offset += snprintf(pump_buf + offset, sizeof(pump_buf) - offset, "[");
        bool first = true;
        for (uint32_t tid = 1; tid < 64; tid++) {
            uint64_t cur_calls = g_ppu_gcm_pump_calls_by_tid[tid];
            uint64_t prev_calls = last_pump_calls[tid];
            last_pump_calls[tid] = cur_calls;
            if (cur_calls > 0 || prev_calls > 0) {
                double rate = (dt > 0.0) ? (double)(cur_calls - prev_calls) / dt : 0.0;
                offset += snprintf(pump_buf + offset, sizeof(pump_buf) - offset,
                                  "%stid%u=%.1f", first ? "" : ", ", tid, rate);
                first = false;
            }
        }
        if (first) {
            offset += snprintf(pump_buf + offset, sizeof(pump_buf) - offset, "none");
        }
        offset += snprintf(pump_buf + offset, sizeof(pump_buf) - offset, "]");

        fprintf(stderr, "[TELEMETRY 5s] wall=%.1fs | dl_head=%u dl_tail=%u ref=%u | ticks=%.1f/s | vblank_handler=%.1f/s | flip_words=%.1f/s | presents=%.1f/s [%s] | rejected_flips=%llu (%.1f/s) | fifo_drain=%.0fus/s (%.1fms/s) | present_time=%.0fus/s (%.1fms/s) | ppu_gcm_pump/s: %s\n",
                wall_sec, dl_head, dl_tail, cur_ref, vblank_tick_rate, vblank_inv_rate, flip_words_rate, presents_rate,
                buf_presents_str, (unsigned long long)cur_rejected, rejected_rate, drain_us_per_s, drain_us_per_s / 1000.0, present_us_per_s, present_us_per_s / 1000.0, pump_buf);
        fflush(stderr);

        static uint64_t last_spurs_disp[32] = {0};
        static uint64_t last_spurs_started[32] = {0};
        char spurs_deltas[1024] = {0};
        int spurs_off = 0;
        for (uint32_t wid = 0; wid < 32; wid++) {
            uint64_t cur_d = g_spurs_wid_diags[wid].dispatched_count;
            uint64_t cur_s = g_spurs_wid_diags[wid].started_count;
            uint64_t diff_d = cur_d - last_spurs_disp[wid];
            uint64_t diff_s = cur_s - last_spurs_started[wid];
            last_spurs_disp[wid] = cur_d;
            last_spurs_started[wid] = cur_s;
            if (diff_d > 0 || diff_s > 0 || cur_d > 0) {
                spurs_off += snprintf(spurs_deltas + spurs_off, sizeof(spurs_deltas) - spurs_off,
                                      "w%u(+%llu/+%llu) ", wid, (unsigned long long)diff_d, (unsigned long long)diff_s);
            }
        }
        if (spurs_off > 0) {
            fprintf(stderr, "[SPURS-DELTAS 5s] %s\n", spurs_deltas);
        }

        static uint64_t last_rcs[64] = {0};
        char rcs_buf[512] = {0};
        int rcs_off = 0;
        rcs_off += snprintf(rcs_buf + rcs_off, sizeof(rcs_buf) - rcs_off, "[");
        bool first_rcs = true;
        for (uint32_t tid = 1; tid < 64; tid++) {
            uint64_t cur_rc = g_ready_count_store_by_tid[tid];
            uint64_t diff = cur_rc - last_rcs[tid];
            last_rcs[tid] = cur_rc;
            if (diff > 0 || cur_rc > 0) {
                double rate = (dt > 0.0) ? (double)diff / dt : 0.0;
                rcs_off += snprintf(rcs_buf + rcs_off, sizeof(rcs_buf) - rcs_off,
                                    "%stid%u=%.1f", first_rcs ? "" : ", ", tid, rate);
                first_rcs = false;
            }
        }
        if (first_rcs) rcs_off += snprintf(rcs_buf + rcs_off, sizeof(rcs_buf) - rcs_off, "none");
        rcs_off += snprintf(rcs_buf + rcs_off, sizeof(rcs_buf) - rcs_off, "]");
        fprintf(stderr, "[READY-COUNT-STORE 5s] calls/s: %s\n", rcs_buf);

        uint64_t spu_calls = 0, spu_bytes = 0, spu_words = 0, ppu_words = 0;
        cellGcm_get_and_reset_spu_dma_stats_5s(&spu_calls, &spu_bytes, &spu_words, &ppu_words);
        fprintf(stderr, "[SPU-DMA 5s] gcm_note_spu_put: calls=%llu bytes=%llu | FIFO words/s: spu=%.1f ppu=%.1f\n",
                (unsigned long long)spu_calls, (unsigned long long)spu_bytes,
                (dt > 0.0) ? (double)spu_words / dt : 0.0,
                (dt > 0.0) ? (double)ppu_words / dt : 0.0);

        LARGE_INTEGER qfc;
        QueryPerformanceFrequency(&qfc);
        double qpc_to_ms = 1000.0 / (double)qfc.QuadPart;

        static GcmFifoBreakdown last_breakdown = {0};
        uint64_t d_total   = g_gcm_fifo_breakdown.total_qpc - last_breakdown.total_qpc;
        uint64_t d_parked  = g_gcm_fifo_breakdown.parked_qpc - last_breakdown.parked_qpc;
        uint64_t d_decode  = g_gcm_fifo_breakdown.decode_qpc - last_breakdown.decode_qpc;
        uint64_t d_method  = g_gcm_fifo_breakdown.method_qpc - last_breakdown.method_qpc;
        uint64_t d_fence   = g_gcm_fifo_breakdown.fence_pub_qpc - last_breakdown.fence_pub_qpc;
        uint64_t d_lock    = g_gcm_fifo_breakdown.lock_wait_qpc - last_breakdown.lock_wait_qpc;
        uint64_t d_calls   = g_gcm_fifo_breakdown.calls - last_breakdown.calls;
        last_breakdown = g_gcm_fifo_breakdown;

        double ms_total  = (dt > 0.0) ? ((double)d_total  * qpc_to_ms) / dt : 0.0;
        double ms_parked = (dt > 0.0) ? ((double)d_parked * qpc_to_ms) / dt : 0.0;
        double ms_decode = (dt > 0.0) ? ((double)d_decode * qpc_to_ms) / dt : 0.0;
        double ms_method = (dt > 0.0) ? ((double)d_method * qpc_to_ms) / dt : 0.0;
        double ms_fence  = (dt > 0.0) ? ((double)d_fence  * qpc_to_ms) / dt : 0.0;
        double ms_lock   = (dt > 0.0) ? ((double)d_lock   * qpc_to_ms) / dt : 0.0;
        double calls_rate = (dt > 0.0) ? (double)d_calls / dt : 0.0;

        char callers_buf[256] = {0};
        int c_off = 0;
        for (int i = 0; i < 8; i++) {
            if (g_gcm_fifo_breakdown.callers[i].tid) {
                c_off += snprintf(callers_buf + c_off, sizeof(callers_buf) - c_off, "htid%u=%llu ",
                                  g_gcm_fifo_breakdown.callers[i].tid,
                                  (unsigned long long)g_gcm_fifo_breakdown.callers[i].count);
            }
        }
        fprintf(stderr, "[FIFO-BREAKDOWN 5s] total=%.1fms/s (parked=%.1f, decode=%.1f, method=%.1f, fence=%.1f, lock=%.1f) calls=%.1f/s | %s\n",
                ms_total, ms_parked, ms_decode, ms_method, ms_fence, ms_lock, calls_rate, callers_buf);

        uint64_t f_calls = 0, f_words = 0;
        double f_max_ms = 0.0;
        cellGcm_get_and_reset_fifo_stats_5s(&f_calls, &f_words, &f_max_ms);
        fprintf(stderr, "[FIFO-STATS 5s] calls=%.1f/s | words=%.1f/s | max_call_ms=%.2fms\n",
                (dt > 0.0) ? (double)f_calls / dt : 0.0,
                (dt > 0.0) ? (double)f_words / dt : 0.0,
                f_max_ms);

        // TASK 1 (b) Stalled request logging & FIOS worker states
        if (vm_base) {
            uint32_t rem_b = vm_read32(0x009976E0u);
            uint32_t state_b = vm_read32(0x00997578u);
            uint32_t req_b = vm_read32(0x00997580u + 0x104);
            int fs_fd = -1; char fs_path[192] = {0}; int64_t fs_off = 0; uint64_t fs_sz = 0; int64_t fs_res = 0; char fs_op[16] = {0};
            gow_get_last_fs_op(&fs_fd, fs_path, sizeof(fs_path), &fs_off, &fs_sz, &fs_res, fs_op, sizeof(fs_op));

            fprintf(stderr, "[FIOS-STALL 5s] rem=%u state=%u req=0x%08X issuer=tid_1 | last_fs=[%s fd=%d path='%s' off=%lld sz=%llu res=%lld]\n",
                    rem_b, state_b, req_b, fs_op[0] ? fs_op : "none", fs_fd, fs_path, (long long)fs_off, (unsigned long long)fs_sz, (long long)fs_res);
            fprintf(stderr, "  FIOS workers:\n");
            for (uint32_t tid = 2; tid <= 8; tid++) {
                char tname[64] = {0}; int tstate = 0; uint32_t tlr = 0;
                ppu_diag_get_thread_status(tid, &tlr, tname, sizeof(tname), &tstate);
                int blk = g_ppu_thread_diags[tid].blocked_type;
                uint64_t obj = g_ppu_thread_diags[tid].blocked_obj;
                const char* hle_name = g_ppu_thread_diags[tid].last_hle_name;
                const char* inflight = (tid < PS3_HLE_INFLIGHT_MAX) ? g_hle_inflight[tid] : nullptr;
                char blk_desc[128] = "RUNNING";
                if (tstate == 2 /* FINISHED */) snprintf(blk_desc, sizeof(blk_desc), "FINISHED");
                else if (blk == PPU_WAIT_LWCOND) snprintf(blk_desc, sizeof(blk_desc), "BLOCKED in lwcond(0x%08llX)", (unsigned long long)obj);
                else if (blk == PPU_WAIT_LWMUTEX) snprintf(blk_desc, sizeof(blk_desc), "BLOCKED in lwmutex(0x%08llX)", (unsigned long long)obj);
                else if (inflight) snprintf(blk_desc, sizeof(blk_desc), "IN-FLIGHT HLE (%s)", inflight);
                fprintf(stderr, "    tid=%u [%-19s] lr=0x%08X last_hle=0x%08X (%-24s) state=%s\n",
                        tid, tname[0] ? tname : "unnamed", tlr, g_ppu_thread_diags[tid].last_hle_nid, hle_name ? hle_name : "none", blk_desc);
            }
        }

        // TASK 1 (c) GOW_SYNC_STATS=1
        static int s_sync_stats_env = -1;
        if (s_sync_stats_env < 0) {
            const char* e = getenv("GOW_SYNC_STATS");
            s_sync_stats_env = (e && atoi(e) != 0) ? 1 : 0;
        }
        if (s_sync_stats_env) {
            gow_dump_sync_stats();
        }

        uint32_t b0 = 0, b1 = 0, b2 = 0;
        cellGcm_get_display_buffer(0, &b0, nullptr, nullptr, nullptr);
        cellGcm_get_display_buffer(1, &b1, nullptr, nullptr, nullptr);
        cellGcm_get_display_buffer(2, &b2, nullptr, nullptr, nullptr);
        int n_color_a = cellGcm_get_distinct_color_a_count();
        fprintf(stderr, "[SURF-OFFSETS 5s] Display buffers: [buf0=0x%08X, buf1=0x%08X, buf2=0x%08X] | Distinct ColorA offsets (%d): [",
                b0, b1, b2, n_color_a);
        for (int ca = 0; ca < n_color_a; ca++) {
            uint32_t off = 0, cnt = 0, w = 0, h = 0, pitch = 0, fmt = 0;
            cellGcm_get_distinct_color_a(ca, &off, &cnt, &w, &h, &pitch, &fmt);
            fprintf(stderr, "0x%08X(cnt=%u,%ux%u,p=%u)%s", off, cnt, w, h, pitch, (ca + 1 < n_color_a) ? ", " : "");
        }
        fprintf(stderr, "]\n");

        char audio_calls_buf[1024];
        cellAudio_print_stats_5s(audio_calls_buf, sizeof(audio_calls_buf));
        char audio_ring_buf[512];
        cellAudio_get_ring_nonzero_summary(audio_ring_buf, sizeof(audio_ring_buf));
        fprintf(stderr, "[AUDIO-CALLS 5s] %s\n", audio_calls_buf);
        fprintf(stderr, "[AUDIO-RINGS 5s] %s\n", audio_ring_buf);

        AudioLwcondStats c20 = {0}, c50 = {0};
        gow_get_audio_lwcond_stats(&c20, &c50);
        fprintf(stderr, "[AUDIO-LWCOND 5s] 0x009FDD20: creator=(tid=%u lr=0x%08X) waits=%llu (last_tid=%u lr=0x%08X) signals=%llu (last_signaler_tid=%u lr=0x%08X) | 0x009FDD50: creator=(tid=%u lr=0x%08X) waits=%llu (last_tid=%u lr=0x%08X) signals=%llu (last_signaler_tid=%u lr=0x%08X)\n",
                c20.creator_tid, c20.creator_lr, (unsigned long long)c20.wait_count, c20.last_waiter_tid, c20.last_waiter_lr,
                (unsigned long long)c20.signal_count, c20.last_signaler_tid, c20.last_signaler_lr,
                c50.creator_tid, c50.creator_lr, (unsigned long long)c50.wait_count, c50.last_waiter_tid, c50.last_waiter_lr,
                (unsigned long long)c50.signal_count, c50.last_signaler_tid, c50.last_signaler_lr);

        static int s_dump_t = 0;
        if (++s_dump_t % 2 == 0) {
            gow_print_pump_callers();
        }
    }
    return 0;
}

static DWORD WINAPI gow_watchdog_thread(LPVOID)
{
    while (!s_window_closed) {
        Sleep(3000);
        fprintf(stderr, "\n=== [WATCHDOG T=3s] Guest PPU Threads ===\n");
        // Guest PPU Threads (tid=1..63)
        for (uint32_t tid = 1; tid < 64; tid++) {
            uint32_t lr = 0;
            char tname[64] = {0};
            int state = 0;
            if (!ppu_diag_get_thread_status(tid, &lr, tname, sizeof(tname), &state))
                continue;

            uint32_t nid = g_ppu_thread_diags[tid].last_hle_nid;
            const char* name = g_ppu_thread_diags[tid].last_hle_name;
            int blk = g_ppu_thread_diags[tid].blocked_type;
            uint64_t obj = g_ppu_thread_diags[tid].blocked_obj;
            const char* inflight = (tid < PS3_HLE_INFLIGHT_MAX) ? g_hle_inflight[tid] : nullptr;

            char blk_str[128];
            if (state == 2 /* FINISHED */) snprintf(blk_str, sizeof(blk_str), "FINISHED");
            else if (blk == PPU_WAIT_LWCOND) snprintf(blk_str, sizeof(blk_str), "BLOCKED in lwcond(0x%08llX)", (unsigned long long)obj);
            else if (blk == PPU_WAIT_LWMUTEX) snprintf(blk_str, sizeof(blk_str), "BLOCKED in lwmutex(0x%08llX)", (unsigned long long)obj);
            else if (blk == PPU_WAIT_MUTEX) snprintf(blk_str, sizeof(blk_str), "BLOCKED in mutex(id=%llu)", (unsigned long long)obj);
            else if (blk == PPU_WAIT_COND) snprintf(blk_str, sizeof(blk_str), "BLOCKED in cond(id=%llu)", (unsigned long long)obj);
            else if (blk == PPU_WAIT_EVENT_QUEUE) snprintf(blk_str, sizeof(blk_str), "BLOCKED in event_queue(id=%llu)", (unsigned long long)obj);
            else if (blk == PPU_WAIT_SEMAPHORE) snprintf(blk_str, sizeof(blk_str), "BLOCKED in semaphore(id=%llu)", (unsigned long long)obj);
            else if (blk == PPU_WAIT_USLEEP) snprintf(blk_str, sizeof(blk_str), "SLEEPING in usleep(%lluus)", (unsigned long long)obj);
            else if (inflight) snprintf(blk_str, sizeof(blk_str), "IN-FLIGHT HLE (%s)", inflight);
            else snprintf(blk_str, sizeof(blk_str), "RUNNING");

            fprintf(stderr, "  tid=%-2u [%-15s] lr=0x%08X  last_hle=0x%08X (%-28s)  state=%s\n",
                    tid, tname[0] ? tname : "unnamed", lr, nid, name ? name : "none", blk_str);
        }

        fprintf(stderr, "=== [WATCHDOG T=3s] SPURS Workloads (WID 0..31) ===\n");
        for (uint32_t wid = 0; wid < 32; wid++) {
            uint64_t disp = g_spurs_wid_diags[wid].dispatched_count;
            uint64_t started = g_spurs_wid_diags[wid].started_count;
            if (disp == 0 && started == 0) continue;

            uint32_t pc = ppu_diag_get_spurs_pc(wid);
            int blk = g_spurs_wid_diags[wid].blocked_type;
            uint32_t det = g_spurs_wid_diags[wid].blocked_detail;

            char blk_str[128];
            if (blk == SPURS_WAIT_CHANNEL) snprintf(blk_str, sizeof(blk_str), "BLOCKED channel_read (ch=%u)", det);
            else if (blk == SPURS_WAIT_DMA) snprintf(blk_str, sizeof(blk_str), "BLOCKED DMA_wait (tag=0x%X)", det);
            else if (blk == SPURS_WAIT_EVENT) snprintf(blk_str, sizeof(blk_str), "BLOCKED event_wait");
            else if (started > 0) snprintf(blk_str, sizeof(blk_str), "RUNNING / IDLE (active)");
            else snprintf(blk_str, sizeof(blk_str), "DISPATCHED (not started)");

            fprintf(stderr, "  wid=%-2u dispatched=%-6llu started=%-6llu last_pc=0x%05X  status=%s\n",
                    wid, (unsigned long long)disp, (unsigned long long)started, pc, blk_str);
        }
        fprintf(stderr, "====================================================\n\n");
        ppu_hle_dump_summary_table();
        gow_print_flip_reconciliation();
        gow_dump_sync_stats();
        fflush(stderr);
    }
    return 0;
}

static DWORD WINAPI gow_predicate_probe_thread(LPVOID)
{
    // Print 32 words around predicate 0x009FDCF8 once per second for 20 s
    for (int sec = 1; sec <= 20; sec++) {
        Sleep(1000);
        if (s_window_closed) break;
        if (!vm_base) continue;
        uint32_t base_ea = 0x009FDCF8;
        fprintf(stderr, "[AUDIO-PRED-DUMP t=%ds EA=0x%08X] 32 words:\n", sec, base_ea);
        for (int row = 0; row < 4; row++) {
            uint32_t row_ea = base_ea + row * 32;
            fprintf(stderr, "  %08X: ", row_ea);
            for (int w = 0; w < 8; w++) {
                fprintf(stderr, "%08X ", vm_read32(row_ea + w * 4));
            }
            fprintf(stderr, "\n");
        }
        fflush(stderr);
    }
    return 0;
}

static DWORD WINAPI window_message_thread(LPVOID)
{
    uint32_t rsx_w = 1280, rsx_h = 720;
    if (const char* e = getenv("GOW_RSX_W")) rsx_w = (uint32_t)strtoul(e, 0, 0);
    if (const char* e = getenv("GOW_RSX_H")) rsx_h = (uint32_t)strtoul(e, 0, 0);

    const int live = rsx_live_draw_enabled();
    int ok = 0;
    if (live) {
        ok = (rsx_null_backend_init(rsx_w, rsx_h, "God of War: Ascension (ps3recomp)") == 0);
        if (ok) {
            s_shared_hwnd = rsx_null_backend_get_hwnd();
        }
    } else {
        ok = (rsx_d3d12_backend_init(rsx_w, rsx_h, "God of War: Ascension (ps3recomp)") == 0);
    }

    if (!ok) {
        fprintf(stderr, "[window] window creation failed\n");
    }

    SetEvent(s_hwnd_ready_event);

    while (!s_window_closed) {
        int r = (live ? rsx_null_backend_pump_messages()
                      : rsx_d3d12_backend_pump_messages());
        if (r != 0) {
            s_window_closed = 1;
            break;
        }
        Sleep(4);
    }
    return 0;
}

static DWORD WINAPI gow_vblank_tick_thread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const double period_s = 1.0 / 60.0;
    const int64_t period_qpc = (int64_t)(freq.QuadPart * period_s);

    timeBeginPeriod(1);

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

    HANDLE hTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!hTimer) {
        hTimer = CreateWaitableTimerW(NULL, TRUE, NULL);
    }

    LARGE_INTEGER qnow, qnext;
    QueryPerformanceCounter(&qnow);
    qnext.QuadPart = qnow.QuadPart + period_qpc;

    while (!s_window_closed) {
        QueryPerformanceCounter(&qnow);
        int fired = 0;
        while (qnow.QuadPart >= qnext.QuadPart && fired < 240) {
            cellGcm_request_tick();
            cellGcmTickFlip();
            cellGcmTickVBlank();
            g_vblank_ticks_count++;
            if (s_vblank_tick_event) {
                SetEvent(s_vblank_tick_event);
            }
            qnext.QuadPart += period_qpc;
            fired++;
        }
        if (fired >= 240) {
            qnext.QuadPart = qnow.QuadPart + period_qpc;
        }

        QueryPerformanceCounter(&qnow);
        int64_t remaining_qpc = qnext.QuadPart - qnow.QuadPart;
        if (remaining_qpc > 0) {
            int64_t due_100ns = -((remaining_qpc * 10000000LL) / freq.QuadPart);
            if (due_100ns < -10000LL && hTimer) {
                LARGE_INTEGER due;
                due.QuadPart = due_100ns;
                if (SetWaitableTimer(hTimer, &due, 0, NULL, NULL, FALSE)) {
                    WaitForSingleObject(hTimer, 2);
                } else {
                    Sleep(1);
                }
            } else {
                Sleep(1);
            }
        } else {
            SwitchToThread();
        }
    }

    if (hTimer) CloseHandle(hTimer);
    timeEndPeriod(1);
    return 0;
}

static DWORD WINAPI gow_target_dump_thread(LPVOID)
{
    Sleep(2000);
    for (int sec = 1; sec <= 20 && !s_window_closed; sec++) {
        Sleep(1000);
        if (!vm_base) continue;
        uint32_t base_ea = 0x00950480u;
        fprintf(stderr, "=== [MEM-DUMP 0x%08X sec=%d/20] ===\n", base_ea, sec);
        for (int row = 0; row < 8; row++) {
            uint32_t ea = base_ea + row * 32;
            fprintf(stderr, "  %08X: %08X %08X %08X %08X  %08X %08X %08X %08X\n",
                    ea,
                    vm_read32(ea + 0),  vm_read32(ea + 4),  vm_read32(ea + 8),  vm_read32(ea + 12),
                    vm_read32(ea + 16), vm_read32(ea + 20), vm_read32(ea + 24), vm_read32(ea + 28));
        }
        fflush(stderr);
    }
    return 0;
}

static DWORD WINAPI gow_dedicated_vblank_thread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    ppu_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.thread_id = 45;
    ctx.gpr[1] = 0xCFFC0000u;
    ctx.gpr[13] = 0x0FEF0000u;
    ppu_resv_register(&ctx);
    g_active_ctx = &ctx;

    fprintf(stderr, "[vblank-thread] Dedicated host thread running for VBlank delivery (tid=45)\n");
    while (!s_window_closed) {
        DWORD wr = WaitForSingleObject(s_vblank_tick_event, 50);
        if (wr == WAIT_OBJECT_0) {
            g_vblank_handler_invocations++;
            ppu_gcm_pump();
        }
    }
    return 0;
}

static DWORD WINAPI rsx_vblank_thread(LPVOID)
{
    uint32_t rsx_w = 1280, rsx_h = 720;
    if (const char* e = getenv("GOW_RSX_W")) rsx_w = (uint32_t)strtoul(e, 0, 0);
    if (const char* e = getenv("GOW_RSX_H")) rsx_h = (uint32_t)strtoul(e, 0, 0);

    WaitForSingleObject(s_hwnd_ready_event, INFINITE);

    const int live = rsx_live_draw_enabled();
    int rsx_ok = 0;
    if (live && s_shared_hwnd) {
        const int r = rsx_live_draw_init(s_shared_hwnd, rsx_w, rsx_h,
                                         gow_live_guest_ptr, nullptr);
        if (r == 0) {
            rsx_null_backend_suppress_present(1);
            rsx_live_draw_present(cellGcm_current_display_buffer_id());
            fprintf(stderr, "[rsx] live-draw engine up (D3D12); GDI present suppressed\n");
            rsx_ok = 1;
        } else {
            fprintf(stderr, "[rsx] live-draw init failed (%d)\n", r);
        }
    } else if (!live) {
        rsx_ok = 1;
    }

    fprintf(stderr, "[rsx] backend init %s\n", rsx_ok ? "OK" : "FAILED (running headless)");

    LARGE_INTEGER qfreq;
    QueryPerformanceFrequency(&qfreq);
    const double qpc_to_us = 1000000.0 / (double)qfreq.QuadPart;

    ULONGLONG next_tick = GetTickCount64();
    ULONGLONG last_stats = GetTickCount64();

    auto gow_present_current_frame = [&](void) {
        uint32_t buf = cellGcm_current_display_buffer_id();
        if (buf < 8) g_presents_by_buf[buf]++;
        g_presents_count++;

        static int s_present_guest = -1;
        if (s_present_guest < 0) {
            const char* e = getenv("GOW_PRESENT_GUEST");
            s_present_guest = (e && atoi(e) != 0) ? 1 : 0;
        }

        if (s_present_guest && live) {
            uint32_t dboff = 0, dbpitch = 0, dbw = 0, dbh = 0;
            if (cellGcm_get_display_buffer(buf, &dboff, &dbpitch, &dbw, &dbh)) {
                const uint8_t* ptr = gow_live_guest_ptr(nullptr, 0, dboff, dbw * dbh * 4);
                if (ptr && dbw == 1280 && dbh == 720) {
                    static uint8_t s_rgba[1280 * 720 * 4];
                    for (uint32_t y = 0; y < 720; y++) {
                        const uint8_t* src_row = ptr + y * dbpitch;
                        uint8_t* dst_row = s_rgba + y * 1280 * 4;
                        for (uint32_t x = 0; x < 1280; x++) {
                            dst_row[x * 4 + 0] = src_row[x * 4 + 1]; // R
                            dst_row[x * 4 + 1] = src_row[x * 4 + 2]; // G
                            dst_row[x * 4 + 2] = src_row[x * 4 + 3]; // B
                            dst_row[x * 4 + 3] = 255;                // A
                        }
                    }
                    rsx_live_draw_present_rgba(s_rgba, 1280, 720);
                    return;
                }
            }
        }
        if (live) rsx_live_draw_present(buf);
        else      rsx_d3d12_backend_present();
    };

    while (!s_window_closed) {
        cellGcm_fifo_kick_wait(4);
        if (rsx_ok) {
            cellGcm_rsx_process_fifo();
            static unsigned last_flip = ~0u;
            const unsigned cur_flip = cellGcm_current_display_buffer_id();
            if (cellGcm_take_flip_pending() || cur_flip != last_flip) {
                last_flip = cur_flip;
                gow_present_current_frame();
            }
        }
        ULONGLONG now = GetTickCount64();

        if (now - last_stats >= 1000) {
            last_stats = now;
            if (live) {
                fprintf(stderr, "[live] frames=%u last_draws=%u\n",
                        rsx_live_draw_get_frames(),
                        rsx_live_draw_get_last_draws());
            }
            float audio_peak = 0.0f;
            if (vm_base) {
                const float* audio_ptr = (const float*)(vm_base + 0x58000000u);
                for (int i = 0; i < 4096; i++) {
                    float s = audio_ptr[i];
                    if (s < 0.0f) s = -s;
                    if (s > audio_peak && s <= 1.0f) audio_peak = s;
                }
            }
            fprintf(stderr, "[audio] port0 peak=%.4f\n", audio_peak);

            if (vm_base) {
                extern uint32_t ppu_hle_inject_base;
                uint32_t ctrl_ea = ppu_hle_inject_base + 0x2000u;
                uint32_t put = vm_read32(ctrl_ea + 0);
                uint32_t get = vm_read32(ctrl_ea + 4);
                uint32_t ea = cellGcmResolveIO(get);
                if (!ea) ea = cellGcmResolveLocated(0, get);

                fprintf(stderr, "[GCM-DUMP 1s] ctrl->get=0x%08X ctrl->put=0x%08X (resolved EA=0x%08X)\n", get, put, ea);
                if (ea && ea < 0x100000000ull - 128) {
                    uint32_t w[32];
                    for (int i = 0; i < 32; i++) w[i] = vm_read32(ea + i * 4);
                    fprintf(stderr, "[GCM-DUMP] words 00..07: %08X %08X %08X %08X  %08X %08X %08X %08X\n",
                            w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
                    fprintf(stderr, "[GCM-DUMP] words 08..15: %08X %08X %08X %08X  %08X %08X %08X %08X\n",
                            w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15]);
                    fprintf(stderr, "[GCM-DUMP] words 16..23: %08X %08X %08X %08X  %08X %08X %08X %08X\n",
                            w[16], w[17], w[18], w[19], w[20], w[21], w[22], w[23]);
                    fprintf(stderr, "[GCM-DUMP] words 24..31: %08X %08X %08X %08X  %08X %08X %08X %08X\n",
                            w[24], w[25], w[26], w[27], w[28], w[29], w[30], w[31]);
                } else {
                    fprintf(stderr, "[GCM-DUMP] get offset 0x%08X is not mapped to guest memory\n", get);
                }

                // Command hole at IO 0x132040 (EA 0x11132040)
                uint32_t hole_ea = cellGcmResolveIO(0x00132040u);
                if (!hole_ea) hole_ea = 0x11132040u;
                if (hole_ea < 0x100000000ull - 32) {
                    uint32_t hw[8];
                    for (int i = 0; i < 8; i++) hw[i] = vm_read32(hole_ea + i * 4);
                    fprintf(stderr, "[GCM-HOLE 0x132040 (EA 0x%08X)]: %08X %08X %08X %08X  %08X %08X %08X %08X\n",
                            hole_ea, hw[0], hw[1], hw[2], hw[3], hw[4], hw[5], hw[6], hw[7]);
                }
                gow_fios_diag_tick();
            }
            fflush(stderr);
        }
    }
    return 0;
}
#endif

#define VM_SIZE    0x100010000ull /* full 32-bit guest space + 64K guard */
#define STACK_TOP  0x0FF00000u   /* main-thread stack */

#ifdef _WIN32
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char** argv)
{
    printf("=================================================================\n");
    printf(" God of War: Ascension (PS3) — Native x86-64 Static Recompilation\n");
    printf("=================================================================\n\n");

    const char* elf_path = "vfs/PS3_GAME/USRDIR/EBOOT.ELF";
    if (argc >= 2) {
        elf_path = argv[1];
    }

#ifdef _WIN32
    timeBeginPeriod(1);
    SetUnhandledExceptionFilter(gow_crash_filter);
    signal(SIGABRT, gow_abort_handler);
    setvbuf(stdout, NULL, _IONBF, 0);

    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;
#else
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif

    if (!vm_base) {
        fprintf(stderr, "[FATAL] Guest VM allocation failed (out of memory?)\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(elf_path);
    if (!entry) {
        fprintf(stderr, "[FATAL] Failed to load ELF image: %s\n", elf_path);
        return 1;
    }

    derive_vfs_root(elf_path);
    printf("[boot] VFS root configured to: %s\n", ppu_vfs_root);

    printf("[boot] Registering lifted PPU function table...\n");
    ppu_recomp_register();

    ps3_load_prx_modules();

    printf("[boot] Initializing HLE subsystem...\n");
    ppu_hle_init();
    ascension_hle_register();
    gow_psn_stubs_register();
    ppu_sysprx_register();
    ppu_fs_register();

    printf("[boot] Initializing LV2 kernel syscalls...\n");
    lv2_init_syscalls();
    ascension_post_syscall_init();

    g_ps3_guest_caller = harness_guest_caller;

#ifdef _WIN32
    s_hwnd_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    CreateThread(NULL, 1024 * 1024, window_message_thread, NULL, 0, NULL);
    CreateThread(NULL, 4u * 1024 * 1024, rsx_vblank_thread, NULL, 0, NULL);
    CreateThread(NULL, 1024 * 1024, gow_vblank_tick_thread, NULL, 0, NULL);
    CreateThread(NULL, 1024 * 1024, gow_watchdog_thread, NULL, 0, NULL);
    CreateThread(NULL, 1024 * 1024, gow_telemetry_5s_thread, NULL, 0, NULL);
    const char* vbt = getenv("GOW_VBLANK_THREAD");
    int enable_vbt = (vbt == NULL) ? 1 : (atoi(vbt) != 0);
    if (enable_vbt) {
        s_vblank_tick_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        CreateThread(NULL, 1024 * 1024, gow_dedicated_vblank_thread, NULL, 0, NULL);
    }
    CreateThread(NULL, 1024 * 1024, gow_target_dump_thread, NULL, 0, NULL);
    CreateThread(NULL, 1024 * 1024, gow_predicate_probe_thread, NULL, 0, NULL);
#endif

    printf("\n[boot] Dispatching entry OPD 0x%08X (stack top 0x%08X)...\n\n", entry, STACK_TOP);
    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] Execution finished with exit code %d\n", rc);
    return rc;
}
