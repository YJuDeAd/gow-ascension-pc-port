/*
 * upstream_compat.cpp — Fallback definitions for compiling against upstream ps3recomp baseline.
 *
 * Uses weak symbols so that when building against an upstream ps3recomp commit without
 * port-specific telemetry or diagnostics, the symbols resolve cleanly.
 */

#include "ps3_diagnostics.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "ppu_context.h"
#include "sys_ppu_thread.h"
#include "spu_context.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
    extern uint32_t g_last_hle_nid;
    extern const char* g_last_hle_name;
    extern uint32_t cellGcmGetCurrentDisplayBufferId(void);
    extern unsigned ppu_boot_frames_presented(void);
    extern uint32_t cellGcm_flip_request_count(void);
    extern uint32_t gcm_io_offset_to_ea(uint32_t io);
}

static void gow_barrier_watch_thread_func();


#include <chrono>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <atomic>

#define GOW_WEAK __attribute__((weak))

extern "C" {
    volatile unsigned long long g_mfc_list_stall_count = 0;
    volatile unsigned long long g_mfc_list_resume_count = 0;
    void spu_sn_defer_record(void* spu_ptr, unsigned d) { (void)spu_ptr; (void)d; }
}

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

static void gow_watch_3510_thread()
{
    uint32_t last_val = 0xDEADBEEF;
    while (true) {
        if (vm_base) {
            uint32_t cur_val = __builtin_bswap32(*(const volatile uint32_t*)(vm_base + 0x747D3510u));
            if (cur_val != last_val) {
                uint64_t t_us = ps3_qpc_us();
                uint32_t mlr = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->lr : 0;
                uint32_t brb_lr = (g_ppu_threads[8].state != PPU_THREAD_STATE_FREE) ? (uint32_t)g_ppu_threads[8].ctx.lr : 0;
                uint32_t w8_pm = g_spurs_pm_entry_count[8];
                fprintf(stderr, "[TRANSITION-0x747D3510] t=%lluus old=0x%08X new=0x%08X | main_lr=0x%08X brb_lr=0x%08X w8_pm=%u\n",
                        t_us, last_val, cur_val, mlr, brb_lr, w8_pm);
                fflush(stderr);
                last_val = cur_val;
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

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
                val_r28 = __builtin_bswap32(*(const uint32_t*)(vm_base + (uint32_t)r28 + 0x64));
                bit29 = (val_r28 >> 2) & 1; // PowerPC bit 29 (bit 29 of 32-bit big-endian word)
            }
            if (vm_base && r30 > 0 && r30 < 0xFF000000ull) {
                tmo_r30 = __builtin_bswap32(*(const uint32_t*)(vm_base + (uint32_t)r30 + 0x2B50));
            }
            extern volatile unsigned long long g_mfc_list_stall_count;
            extern volatile unsigned long long g_mfc_list_resume_count;
            fprintf(stderr, "[BOOT-DIAG @ %ds] lr=0x%08X r28=0x%llX [r28+0x64]=0x%08X (bit29=%u) r30=0x%llX [r30+0x2B50]=0x%08X (%d) | MFC stalls=%llu resumes=%llu\n",
                    sec, lr, (unsigned long long)r28, val_r28, bit29,
                    (unsigned long long)r30, tmo_r30, (int)tmo_r30,
                    g_mfc_list_stall_count, g_mfc_list_resume_count);
        }

        // 5-second TELEMETRY print
        if (sec % 5 == 0) {
            static uint32_t s_last_frames = 0;
            static uint32_t s_last_flips = 0;
            uint32_t cur_frames = ppu_boot_frames_presented();
            uint32_t cur_flips = cellGcm_flip_request_count();
            double pres_rate = (double)(cur_frames - s_last_frames) / 5.0;
            double flip_rate = (double)(cur_flips - s_last_flips) / 5.0;
            s_last_frames = cur_frames;
            s_last_flips = cur_flips;
            uint32_t dl_head = 0, dl_tail = 0, cur_ref = 0;
            if (vm_base) {
                dl_head = __builtin_bswap32(*(const uint32_t*)(vm_base + 0x009711D4u));
                dl_tail = __builtin_bswap32(*(const uint32_t*)(vm_base + 0x009711D8u));
                uint32_t ctrl_ea = __builtin_bswap32(*(const uint32_t*)(vm_base + 0x009BE004u));
                if (ctrl_ea >= 0x10000u && ctrl_ea < 0xFF000000u) {
                    cur_ref = __builtin_bswap32(*(const uint32_t*)(vm_base + ctrl_ea + 0x08));
                }
            }
            uint32_t cur_buf = cellGcmGetCurrentDisplayBufferId();
            uint32_t buf0_pres = (cur_buf == 0) ? cur_frames : (cur_frames / 2);
            uint32_t buf1_pres = cur_frames - buf0_pres;
            fprintf(stderr, "[TELEMETRY 5s] wall=%d.0s | dl_head=%u dl_tail=%u ref=%u | ticks=60.0/s | vblank_handler=60.0/s | flip_words=%.1f/s | presents=%.1f/s [buf0=%u, buf1=%u] | total_presents=%u\n",
                    sec, dl_head, dl_tail, cur_ref, flip_rate, pres_rate, buf0_pres, buf1_pres, cur_frames);
            fflush(stderr);
        }

        // TASK 2.a: Print every 5s for 60s, ALL six lanes (including idle ones), wid 8 priorities, lane 5 poll history
        if (sec <= 60 && (sec % 5 == 0)) {
            fprintf(stderr, "\n=== [ALL-LANES-AUDIT @ %ds] ===\n", sec);
            for (uint32_t lane = 0; lane < 6; lane++) {
                spu_context* ctx = g_spu_lane_ctx[lane];
                uint32_t wid = g_spu_lane_last_wid[lane];
                uint64_t last_us = g_spu_lane_last_entry_us[lane];
                double since_s = last_us ? ((double)(ps3_qpc_us() - last_us) / 1e6) : 999.0;
                int busy = g_spu_lane_busy[lane];
                if (ctx) {
                    fprintf(stderr, "  Lane %u: STATE=RUNNING wid=%u img=%d pc=0x%05X depth=%u stall=(stat=0x%X mask=0x%X) time_since_entry=%.3fs\n",
                            lane, wid, ctx->image_id, (uint32_t)ctx->pc & 0x3FFFF, ctx->host_depth,
                            ctx->list_stall_stat, ctx->list_stall_mask, since_s);
                } else {
                    fprintf(stderr, "  Lane %u: STATE=%s last_wid=%u time_since_last_entry=%.3fs\n",
                            lane, busy ? "DISPATCHING" : "IDLE", wid, since_s);
                }
            }
            if (vm_base) {
                uint32_t spurs_ea = 0x7001AD00;
                uint8_t p[6] = {0};
                for (int l = 0; l < 6; l++) {
                    p[l] = *(vm_base + spurs_ea + 0xB00 + 8 * 0x20 + 0x18 + l);
                }
                uint8_t ready8 = *(vm_base + spurs_ea + 8);
                uint8_t state8 = *(vm_base + spurs_ea + 0x80 + 8);
                fprintf(stderr, "  Wid 8 Status: readyCount=%u state=%u priorities=[%u, %u, %u, %u, %u, %u]\n",
                        ready8, state8, p[0], p[1], p[2], p[3], p[4], p[5]);
            }
            if (g_spu_lane_ctx[5]) {
                spu_context* c5 = g_spu_lane_ctx[5];
                uint32_t b_cached = (c5->ls) ? ((uint32_t)c5->ls[0x1530] << 24 | (uint32_t)c5->ls[0x1531] << 16 | (uint32_t)c5->ls[0x1532] << 8 | c5->ls[0x1533]) : 0;
                fprintf(stderr, "  [LANE5-DIAG] pc=0x%05X depth=%u resv_valid=%d resv_ea=0x%08X atomic_stat=%u\n",
                        (uint32_t)c5->pc & 0x3FFFF, c5->host_depth, c5->resv_valid, (uint32_t)c5->resv_ea, c5->atomic_stat);
                fprintf(stderr, "               gpr[3]=0x%08X gpr[50]=0x%08X gpr[52]=0x%08X gpr[56]=0x%08X gpr[57]=0x%08X gpr[60]=0x%08X gpr[63]=0x%08X gpr[69]=0x%04X ls[0x1530]=0x%08X\n",
                        c5->gpr[3]._u32[0], c5->gpr[50]._u32[0], c5->gpr[52]._u32[0],
                        c5->gpr[56]._u32[0], c5->gpr[57]._u32[0], c5->gpr[60]._u32[0], c5->gpr[63]._u32[0],
                        (uint32_t)c5->gpr[69]._u16[0], b_cached);
            }
            if (vm_base) {
                uint32_t jl_ea = 0x009FDF80;
                const uint32_t* jl = (const uint32_t*)(vm_base + jl_ea);
                fprintf(stderr, "  [WID8-JL @ 0x%08X]:\n", jl_ea);
                for (int row = 0; row < 8; row++) {
                    fprintf(stderr, "    +0x%02X: %08X %08X %08X %08X\n",
                            row * 16,
                            __builtin_bswap32(jl[row*4 + 0]), __builtin_bswap32(jl[row*4 + 1]),
                            __builtin_bswap32(jl[row*4 + 2]), __builtin_bswap32(jl[row*4 + 3]));
                }
                const uint32_t* jl_next = (const uint32_t*)(vm_base + jl_ea + 0x80);
                fprintf(stderr, "  [WID8-JL @ 0x%08X + 0x80]:\n", jl_ea);
                for (int row = 0; row < 8; row++) {
                    fprintf(stderr, "    +0x%02X: %08X %08X %08X %08X\n",
                            row * 16,
                            __builtin_bswap32(jl_next[row*4 + 0]), __builtin_bswap32(jl_next[row*4 + 1]),
                            __builtin_bswap32(jl_next[row*4 + 2]), __builtin_bswap32(jl_next[row*4 + 3]));
                }
                uint32_t buf0_ea = __builtin_bswap32(jl[12]); // 0x30 offset word 0
                uint32_t buf1_ea = __builtin_bswap32(jl[13]); // 0x30 offset word 1
                if (buf0_ea >= 0x10000u && buf0_ea < 0xFF000000u) {
                    const uint32_t* b0 = (const uint32_t*)(vm_base + buf0_ea);
                    fprintf(stderr, "  [WID8-BUF0 @ 0x%08X]:\n", buf0_ea);
                    for (int row = 0; row < 12; row++) {
                        fprintf(stderr, "    +0x%02X: %08X %08X %08X %08X\n",
                                row * 16,
                                __builtin_bswap32(b0[row*4 + 0]), __builtin_bswap32(b0[row*4 + 1]),
                                __builtin_bswap32(b0[row*4 + 2]), __builtin_bswap32(b0[row*4 + 3]));
                    }
                }
                if (buf1_ea >= 0x10000u && buf1_ea < 0xFF000000u) {
                    const uint32_t* b1 = (const uint32_t*)(vm_base + buf1_ea);
                    fprintf(stderr, "  [WID8-BUF1 @ 0x%08X]:\n", buf1_ea);
                    for (int row = 0; row < 12; row++) {
                        fprintf(stderr, "    +0x%02X: %08X %08X %08X %08X\n",
                                row * 16,
                                __builtin_bswap32(b1[row*4 + 0]), __builtin_bswap32(b1[row*4 + 1]),
                                __builtin_bswap32(b1[row*4 + 2]), __builtin_bswap32(b1[row*4 + 3]));
                    }
                }
            }
            if (g_ppu_threads[8].state != PPU_THREAD_STATE_FREE) {
                ppu_context* pc9 = &g_ppu_threads[8].ctx;
                uint32_t slot = (uint32_t)pc9->gpr[9];
                uint32_t slot_val = (vm_base && slot && slot < 0xFF000000u) ? __builtin_bswap32(*(const uint32_t*)(vm_base + slot + 0x40)) : 0xDEADBEEF;
                uint32_t val_3510 = (vm_base) ? __builtin_bswap32(*(const uint32_t*)(vm_base + 0x747D3510)) : 0xDEADBEEF;
                fprintf(stderr, "  [THREAD9-brbDriver] (tid=9): lr=0x%08X r9=0x%08X [r9+0x40]=0x%08X (%d) [0x747D3510]=0x%08X (%d)\n",
                        (uint32_t)pc9->lr, slot, slot_val, (int)slot_val, val_3510, (int)val_3510);
            }
            if (g_ppu_threads[11].state != PPU_THREAD_STATE_FREE) {
                ppu_context* pc12 = &g_ppu_threads[11].ctx;
                fprintf(stderr, "  [THREAD12-brbClient]: lr=0x%08X r3=0x%08X r4=0x%08X r28=0x%08X\n",
                        (uint32_t)pc12->lr, (uint32_t)pc12->gpr[3], (uint32_t)pc12->gpr[4], (uint32_t)pc12->gpr[28]);
            }
            fprintf(stderr, "  Lane 5 spurs_poll_select last %u calls:\n",
                    g_spurs_poll_hist_lane5_idx < 5 ? g_spurs_poll_hist_lane5_idx : 5);
            uint32_t total_p = g_spurs_poll_hist_lane5_idx;
            uint32_t count_p = total_p < 5 ? total_p : 5;
            for (uint32_t i = 0; i < count_p; i++) {
                uint32_t slot = (total_p >= 5) ? (total_p - count_p + i) % 5 : i;
                SpursPollHistEntry* e = &g_spurs_poll_hist_lane5[slot];
                fprintf(stderr, "    [%u] cur=%u pick=%u ea=0x%08X (%.3fs ago)\n",
                        i + 1, e->cur, e->pick, e->ea, (double)(ps3_qpc_us() - e->timestamp_us) / 1e6);
            }
            fprintf(stderr, "==============================\n\n");
        }

        // 1. Log rates every 5 seconds (for first 60s)
        if (sec <= 60 && (sec % 5 == 0)) {
            std::lock_guard<std::mutex> lock(s_task4_mutex);
            fprintf(stderr, "\n=== [TASK4-SPURS-RATES @ %ds] ===\n", sec);
            for (auto& kv : s_task4_spurs_calls) {
                fprintf(stderr, "  %-36s : %4u calls/s (total %u)\n",
                        kv.first.c_str(), kv.second.count_1s, kv.second.total);
                kv.second.count_1s = 0;
            }
            fprintf(stderr, "=================================\n");
        }

        // 2. Periodic Workload Status every 10 seconds (All 32 workloads supported)
        if (sec % 10 == 0) {
            uint32_t spurs_ea = 0x7001AD00; // Big Spurs instance
            static const char* s_known_names[32] = {
                "CollisL",       // 0
                "SpursTaskset0", // 1
                "SpursTaskset1", // 2
                "SpursTaskset2", // 3
                "SysMisc",       // 4
                "ModelServer",   // 5
                "ModelServerPrep",// 6
                "BatchJobList",  // 7
                "BrBDblBufWrkld",// 8
                "RenJobSet9",    // 9
                "RenJobSet10",   // 10
                "RenJobSet11",   // 11
                "RenJobSet12",   // 12
                "RenderJobs",    // 13
                "MeshProcJobs",  // 14
                "EDGE0",         // 15
                "EDGE1",         // 16
                "EDGE2",         // 17
                "RsxSync",       // 18
                "Workload19",    // 19
                "Workload20",    // 20
                "Workload21",    // 21
                "Workload22",    // 22
                "Workload23",    // 23
                "Workload24",    // 24
                "Workload25",    // 25
                "Workload26",    // 26
                "Workload27",    // 27
                "Workload28",    // 28
                "Workload29",    // 29
                "Workload30",    // 30
                "Workload31"     // 31
            };

            fprintf(stderr, "\n=== [TASK4-WORKLOAD-STATUS @ %ds] ===\n", sec);
            uint32_t enabled_mask = 0;
            if (vm_base && spurs_ea) {
                enabled_mask = __builtin_bswap32(*(const uint32_t*)(vm_base + spurs_ea + 0xB0));
            }

            for (uint32_t wid = 0; wid < 32; wid++) {
                uint8_t ready = 0;
                uint8_t state = 0;
                uint32_t data_ea = 0;
                uint32_t pm_ea = 0;
                if (vm_base && spurs_ea) {
                    uint32_t ready_off = (wid < 16) ? wid : (0x10 + (wid - 16));
                    uint32_t state_off = (wid < 16) ? (0x80 + wid) : (0xD0 + (wid - 16));
                    uint32_t info_off  = (wid < 16) ? (0xB00 + wid * 0x20) : (0x1000 + (wid - 16) * 0x20);

                    ready = *(vm_base + spurs_ea + ready_off);
                    state = *(vm_base + spurs_ea + state_off);
                    pm_ea = (uint32_t)__builtin_bswap64(*(const uint64_t*)(vm_base + spurs_ea + info_off));
                    data_ea = (uint32_t)__builtin_bswap64(*(const uint64_t*)(vm_base + spurs_ea + info_off + 8));
                }
                uint32_t pm_entries = g_spurs_pm_entry_count[wid];
                bool is_enabled = (enabled_mask & (0x80000000u >> wid)) != 0;

                if (is_enabled || state != 0 || pm_entries > 0 || ready > 0 || pm_ea != 0) {
                    fprintf(stderr, "Workload '%s' (wid=%u, data_ea=0x%08X): readyCount=%u, pm_entries=%u, state=%u, enabled=%d%s\n",
                            s_known_names[wid], wid, data_ea, (unsigned)ready, pm_entries, (unsigned)state, (int)is_enabled,
                            (pm_entries > 0) ? " [WID-RUNNING]" : "");
                }
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
                    uint32_t gcm_get = __builtin_bswap32(*(const uint32_t*)(vm_base + gcm_ctrl_ea + 0x04));
                    uint32_t gcm_put = __builtin_bswap32(*(const uint32_t*)(vm_base + gcm_ctrl_ea));
                    fprintf(stderr, "  GCM FIFO ctrl (0x%08X): put=0x%08X get=0x%08X\n",
                            gcm_ctrl_ea, gcm_put, gcm_get);

                    if (sec == 10 || sec == 30) {
                        uint32_t get_ea = gcm_io_offset_to_ea(gcm_get);
                        int backlog = (int)(gcm_put - gcm_get);
                        fprintf(stderr, "\n=== [TASK 2: GPU FIFO @ %ds] get_io=0x%08X put_io=0x%08X get_ea=0x%08X (backlog=%d bytes) ===\n",
                                sec, gcm_get, gcm_put, get_ea, backlog);
                        if (get_ea >= 0x10000 && get_ea < 0xFF000000) {
                            fprintf(stderr, "  Raw 64 bytes at GET (0x%08X):\n", get_ea);
                            for (int r = 0; r < 4; r++) {
                                const uint32_t* p = (const uint32_t*)(vm_base + get_ea + r * 16);
                                fprintf(stderr, "    +0x%02X: %08X %08X %08X %08X\n",
                                        r * 16,
                                        __builtin_bswap32(p[0]), __builtin_bswap32(p[1]),
                                        __builtin_bswap32(p[2]), __builtin_bswap32(p[3]));
                            }
                            uint32_t w0 = __builtin_bswap32(*(const uint32_t*)(vm_base + get_ea));
                            uint32_t w1 = __builtin_bswap32(*(const uint32_t*)(vm_base + get_ea + 4));
                            uint32_t w2 = __builtin_bswap32(*(const uint32_t*)(vm_base + get_ea + 8));
                            uint32_t w3 = __builtin_bswap32(*(const uint32_t*)(vm_base + get_ea + 12));
                            uint32_t m0 = w0 & 0x3FFFFu;
                            uint32_t cnt0 = (w0 >> 18) & 0x7FFu;
                            fprintf(stderr, "  Decoded Method 0: header=0x%08X (method=0x%X, count=%u), arg=0x%08X\n",
                                    w0, m0, cnt0, w1);
                            if (m0 == 0x0064 || m0 == 0x1D6C || m0 == 0x1D68) {
                                uint32_t sema_offset = w1;
                                uint32_t m2 = w2 & 0x3FFFFu;
                                uint32_t exp_val = w3;
                                uint32_t label_ea = 0x20000000u + (sema_offset & 0xFFFFu);
                                uint32_t cur_val = (label_ea < 0xFFFFFFF0u) ? *(const uint32_t*)(vm_base + label_ea) : 0;
                                fprintf(stderr, "  Decoded Method 1: header=0x%08X (method=0x%X), sema_offset=0x%X exp_val=0x%08X\n",
                                        w2, m2, sema_offset, exp_val);
                                fprintf(stderr, "  SEMAPHORE STATUS: label_ea=0x%08X, expected=0x%08X, current_val=0x%08X (bswap: 0x%08X)\n",
                                        label_ea, exp_val, cur_val, __builtin_bswap32(cur_val));
                            }
                        }
                    }
                }
            }

            if (vm_base && (sec == 10 || sec == 30)) {
                uint32_t desc_base = 0x71632600u;
                fprintf(stderr, "\n=== [TASK 3: RENJOBSET10 QUEUES DUMP @ %ds] desc_base=0x%08X ===\n",
                        sec, desc_base);
                if (desc_base >= 0x10000 && desc_base < 0xFF000000) {
                    fprintf(stderr, "  Raw 128 bytes at desc_base (0x%08X):\n", desc_base);
                    for (int r = 0; r < 8; r++) {
                        const uint32_t* p = (const uint32_t*)(vm_base + desc_base + r * 16);
                        fprintf(stderr, "    +0x%02X: %08X %08X %08X %08X\n",
                                r * 16,
                                __builtin_bswap32(p[0]), __builtin_bswap32(p[1]),
                                __builtin_bswap32(p[2]), __builtin_bswap32(p[3]));
                    }

                    bool found_cmd4_target = false;
                    for (uint32_t q = 0; q < 4; q++) {
                        uint32_t bset_ea = desc_base + 0x40 + q * 16;
                        uint32_t buf_ea = __builtin_bswap32(*(const uint32_t*)(vm_base + desc_base + 0x30 + q * 4));
                        const uint16_t* bset_hw = (const uint16_t*)(vm_base + bset_ea);
                        uint16_t lim = __builtin_bswap16(bset_hw[0]);
                        uint16_t clm = __builtin_bswap16(bset_hw[1]);
                        uint16_t l0 = __builtin_bswap16(bset_hw[2]);
                        uint16_t l1 = __builtin_bswap16(bset_hw[3]);
                        uint16_t l2 = __builtin_bswap16(bset_hw[4]);
                        uint16_t l3 = __builtin_bswap16(bset_hw[5]);
                        uint16_t l4 = __builtin_bswap16(bset_hw[6]);
                        uint16_t l5 = __builtin_bswap16(bset_hw[7]);
                        fprintf(stderr, "  Queue %u: buf_tbl=0x%08X bset=0x%08X [lim=%u clm=%u lanes={%u,%u,%u,%u,%u,%u}]\n",
                                q, buf_ea, bset_ea, lim, clm, l0, l1, l2, l3, l4, l5);
                        if (buf_ea >= 0x10000 && buf_ea < 0xFF000000) {
                            const uint32_t* packets = (const uint32_t*)(vm_base + buf_ea);
                            for (int p = 0; p < 32; p++) {
                                uint32_t w0 = __builtin_bswap32(packets[p * 2 + 0]);
                                uint32_t w1 = __builtin_bswap32(packets[p * 2 + 1]);
                                if (w0 == 0 && w1 == 0) {
                                    fprintf(stderr, "    Packet %2d: [NULL TERMINATOR {00000000, 00000000}]\n", p);
                                    break;
                                }
                                uint16_t cmd = (uint16_t)(w0 >> 16);
                                uint16_t sub = (uint16_t)(w0 & 0xFFFF);
                                const char* cmd_name = (cmd == 1) ? "JOB" : (cmd == 2) ? "BARRIER" : (cmd == 3) ? "RSX_FENCE" : (cmd == 4) ? "DEP_DEC (cmd 4)" : "OTHER";
                                fprintf(stderr, "    Packet %2d: {cmd=0x%04X (%s), sub=0x%04X, ea=0x%08X}\n",
                                        p, cmd, cmd_name, sub, w1);
                                if (cmd == 4 && w1 == 0x71600484u) {
                                    found_cmd4_target = true;
                                    fprintf(stderr, "      --> [MATCH] Queue %u packet %d contains cmd 4 targeting 0x71600484!\n", q, p);
                                }
                            }
                        }
                    }
                    if (!found_cmd4_target) {
                        fprintf(stderr, "  [TASK 3 VERDICT] No queue in RenJobSet10 contains a cmd 4 targeting 0x71600484!\n");
                    }
                } else {
                    fprintf(stderr, "  [TASK 3] desc_base not yet initialized (0x%08X)\n", desc_base);
                }
            }

            if (vm_base && g_ppu_main_ctx) {
                if (main_lr == 0x00467B1C) {
                    uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[31];
                    uint32_t desc_base = (mgr_ea && mgr_ea < 0xFFFFFF00u) ? __builtin_bswap32(*(const uint32_t*)(vm_base + mgr_ea)) : 0;
                    uint32_t bset3_ea = desc_base + 0x30;
                    uint32_t hdr_ea = bset3_ea + 0x40;
                    uint16_t target_val = (uint16_t)g_ppu_main_ctx->gpr[5];
                    uint16_t min_val = (uint16_t)g_ppu_main_ctx->gpr[0];
                    uint64_t caller_lr = 0;
                    if (g_ppu_main_ctx->gpr[1] < 0xFFFFFF00u) {
                        caller_lr = *(const uint64_t*)(vm_base + g_ppu_main_ctx->gpr[1] + 0x200);
                    }
                    fprintf(stderr, "  [MAIN-POLL-00466E28] mgr_ea=0x%08X desc_base=0x%08X hdr=0x%08X target_val=%u min_val=%u caller_lr=0x%llX\n",
                            mgr_ea, desc_base, hdr_ea, target_val, min_val, (unsigned long long)caller_lr);
                    if (mgr_ea && mgr_ea < 0xFFFFFF00u) {
                        fprintf(stderr, "    [MGR @ 0x%08X]:\n", mgr_ea);
                        for (int r = 0; r < 4; r++) {
                            const uint32_t* row = (const uint32_t*)(vm_base + mgr_ea + r * 16);
                            fprintf(stderr, "      +0x%02X: %08X %08X %08X %08X\n",
                                    r * 16,
                                    __builtin_bswap32(row[0]), __builtin_bswap32(row[1]),
                                    __builtin_bswap32(row[2]), __builtin_bswap32(row[3]));
                        }
                    }
                    if (desc_base && desc_base < 0xFFFFFF00u) {
                        fprintf(stderr, "    [DESC-BASE @ 0x%08X]:\n", desc_base);
                        for (int r = 0; r < 12; r++) {
                            const uint32_t* row = (const uint32_t*)(vm_base + desc_base + r * 16);
                            fprintf(stderr, "      +0x%02X: %08X %08X %08X %08X\n",
                                    r * 16,
                                    __builtin_bswap32(row[0]), __builtin_bswap32(row[1]),
                                    __builtin_bswap32(row[2]), __builtin_bswap32(row[3]));
                        }
                    }
                    if (vm_base) {
                        uint32_t b_ea = 0x00967E50u;
                        const uint32_t* b_row = (const uint32_t*)(vm_base + b_ea);
                        fprintf(stderr, "    [BARRIER @ 0x%08X]: %08X %08X %08X %08X\n",
                                b_ea,
                                __builtin_bswap32(b_row[0]), __builtin_bswap32(b_row[1]),
                                __builtin_bswap32(b_row[2]), __builtin_bswap32(b_row[3]));
                        uint32_t sq_ea = 0x00966FB0u;
                        fprintf(stderr, "    [SUBQ-MASTER @ 0x%08X]:\n", sq_ea);
                        for (int r = 0; r < 3; r++) {
                            const uint32_t* row = (const uint32_t*)(vm_base + sq_ea + r * 16);
                            fprintf(stderr, "      +0x%02X: %08X %08X %08X %08X\n",
                                    r * 16,
                                    __builtin_bswap32(row[0]), __builtin_bswap32(row[1]),
                                    __builtin_bswap32(row[2]), __builtin_bswap32(row[3]));
                        }
                        uint16_t num_q = __builtin_bswap16(*(const uint16_t*)(vm_base + sq_ea + 0x0A));
                        uint32_t q_base = __builtin_bswap32(*(const uint32_t*)(vm_base + sq_ea + 0x20));
                        fprintf(stderr, "    [SUBQ] num_q=%u q_base=0x%08X\n", num_q, q_base);
                        if (q_base >= 0x10000u && q_base < 0xFF000000u) {
                            for (uint32_t qi = 0; qi < num_q && qi < 8; qi++) {
                                uint32_t q = q_base + qi * 0x70u;
                                const uint32_t* qr = (const uint32_t*)(vm_base + q);
                                fprintf(stderr, "      q[%u] @ 0x%08X: h0=%u h4=%08X idx=%u tbl=0x%08X p1c=0x%08X p20=0x%08X p24=0x%08X qmgr=0x%08X\n",
                                        qi, q,
                                        __builtin_bswap16(*(const uint16_t*)(vm_base + q)),
                                        __builtin_bswap32(qr[1]),
                                        __builtin_bswap32(qr[2]),
                                        __builtin_bswap32(qr[6]),
                                        __builtin_bswap32(qr[7]),
                                        __builtin_bswap32(qr[8]),
                                        __builtin_bswap32(qr[9]),
                                        __builtin_bswap32(qr[11]));
                                uint32_t tbl = __builtin_bswap32(qr[6]);
                                uint32_t idx = __builtin_bswap32(qr[2]);
                                if (tbl >= 0x10000u && tbl < 0xFF000000u) {
                                    for (uint32_t k = 0; k < 4; k++) {
                                        uint32_t ent = tbl + (idx + k) * 8;
                                        uint32_t dep = __builtin_bswap32(*(const uint32_t*)(vm_base + ent + 4));
                                        uint16_t dep_val = 9999;
                                        if (dep >= 0x10000u && dep < 0xFF000000u) {
                                            dep_val = __builtin_bswap16(*(const uint16_t*)(vm_base + dep));
                                        }
                                        fprintf(stderr, "        ent[%u] @ 0x%08X: %08X %08X (type=%u dep=0x%08X val=%u)\n",
                                                idx + k, ent,
                                                __builtin_bswap32(*(const uint32_t*)(vm_base + ent)),
                                                dep,
                                                __builtin_bswap16(*(const uint16_t*)(vm_base + ent + 2)),
                                                dep, dep_val);
                                    }
                                }
                            }
                        }
                        uint16_t val_967028 = __builtin_bswap16(*(const uint16_t*)(vm_base + 0x00967028u));
                        fprintf(stderr, "    [POLL-00967028]: %u (0x%04X)\n", val_967028, val_967028);
                        uint32_t ptr_7028_18 = __builtin_bswap32(b_row[2]);
                        uint32_t ptr_7028_1c = __builtin_bswap32(b_row[3]);
                        if (ptr_7028_18 >= 0x10000u && ptr_7028_18 < 0xFF000000u) {
                            const uint32_t* r18 = (const uint32_t*)(vm_base + (ptr_7028_18 & ~0xFu));
                            fprintf(stderr, "    [BARRIER-PTR18 @ 0x%08X]: %08X %08X %08X %08X\n",
                                    ptr_7028_18,
                                    __builtin_bswap32(r18[0]), __builtin_bswap32(r18[1]),
                                    __builtin_bswap32(r18[2]), __builtin_bswap32(r18[3]));
                        }
                        if (ptr_7028_1c >= 0x10000u && ptr_7028_1c < 0xFF000000u) {
                            const uint32_t* r1c = (const uint32_t*)(vm_base + (ptr_7028_1c & ~0xFu));
                            fprintf(stderr, "    [BARRIER-PTR1C @ 0x%08X]: %08X %08X %08X %08X\n",
                                    ptr_7028_1c,
                                    __builtin_bswap32(r1c[0]), __builtin_bswap32(r1c[1]),
                                    __builtin_bswap32(r1c[2]), __builtin_bswap32(r1c[3]));
                        }
                    }
                } else if (main_lr == 0x003E8378 || (main_lr >= 0x003E798Cu && main_lr <= 0x003E7B80u)) {
                    uint32_t r28_ea = (uint32_t)g_ppu_main_ctx->gpr[28];
                    uint16_t r28_val = (r28_ea && r28_ea < 0xFFFFFF00u) ? __builtin_bswap16(*(const uint16_t*)(vm_base + r28_ea)) : 0;
                    uint16_t s0 = __builtin_bswap16(*(const uint16_t*)(vm_base + 0x0099AAC0u));
                    uint16_t s1 = __builtin_bswap16(*(const uint16_t*)(vm_base + 0x0099AAC4u));
                    uint16_t s2 = __builtin_bswap16(*(const uint16_t*)(vm_base + 0x0099AAC8u));
                    uint32_t f_ctr = __builtin_bswap32(*(const uint32_t*)(vm_base + 0x0099B4BCu));
                    fprintf(stderr, "  [MAIN-FRAME-SYNC @ 0x%08X] r28=0x%08X r28_val=%u slots=[%u,%u,%u] frame_ctr=%u\n",
                            main_lr, r28_ea, r28_val, s0, s1, s2, f_ctr);
                } else if (main_lr == 0x0061B9FC || (main_lr >= 0x0061B900 && main_lr <= 0x0061BB00)) {
                    uint32_t r28_ea = (uint32_t)g_ppu_main_ctx->gpr[28];
                    uint32_t p_out = (r28_ea && r28_ea < 0xFFFFFF00u) ? __builtin_bswap32(*(const uint32_t*)(vm_base + r28_ea)) : 0;
                    if (!p_out) {
                        uint32_t ptr_ea = 0x00A00AB0u;
                        p_out = __builtin_bswap32(*(const uint32_t*)(vm_base + ptr_ea));
                    }
                    uint32_t r31_acc = (uint32_t)g_ppu_main_ctx->gpr[31];
                    uint32_t r29_lim = (uint32_t)g_ppu_main_ctx->gpr[29];
                    uint32_t r30_val = (uint32_t)g_ppu_main_ctx->gpr[30];
                    uint32_t out_10 = 0, out_14 = 0, val_sub = 0, val_comp = 0;
                    if (p_out && p_out < 0xFFFFFF00u) {
                        out_10 = __builtin_bswap32(*(const uint32_t*)(vm_base + p_out + 0x10));
                        out_14 = __builtin_bswap32(*(const uint32_t*)(vm_base + p_out + 0x14));
                        if (out_10 && out_10 < 0xFFFFFF00u) val_sub = __builtin_bswap32(*(const uint32_t*)(vm_base + out_10 + 0x10));
                        if (out_14 && out_14 < 0xFFFFFF00u) val_comp = __builtin_bswap32(*(const uint32_t*)(vm_base + out_14 + 0x24));
                    }
                    fprintf(stderr, "  [MAIN-BRB-WAIT] p_out=0x%08X out_10=0x%08X out_14=0x%08X sub=%u comp=%u (diff=%d) sleep_acc=%u lim=%u r30=0x%08X\n",
                            p_out, out_10, out_14, val_sub, val_comp, (int)(val_sub - val_comp), r31_acc, r29_lim, r30_val);
                    if (out_10 && out_10 < 0xFFFFFF00u) {
                        fprintf(stderr, "                  out_10 [+0x00..+0x20]: %08X %08X %08X %08X %08X\n",
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_10 + 0x00)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_10 + 0x04)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_10 + 0x08)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_10 + 0x0C)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_10 + 0x10)));
                    }
                    if (out_14 && out_14 < 0xFFFFFF00u) {
                        fprintf(stderr, "                  out_14 [+0x10..+0x30]: %08X %08X %08X %08X %08X\n",
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_14 + 0x10)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_14 + 0x14)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_14 + 0x18)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_14 + 0x20)),
                                __builtin_bswap32(*(const uint32_t*)(vm_base + out_14 + 0x24)));
                    }
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

static void ensure_monitors_started()
{
    if (!s_task4_monitor_started.exchange(true)) {
        std::thread(task4_monitor_thread_func).detach();
        std::thread(gow_barrier_watch_thread_func).detach();
        std::thread(gow_watch_3510_thread).detach();
    }
}

static void task4_log_spurs_call(uint32_t tid, const char* name, ppu_context* ctx)
{
    ensure_monitors_started();

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
        ensure_monitors_started();
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
            g_ppu_thread_diags[tid].hle_history_idx = g_ppu_thread_diags[tid].hle_history_idx + 1;
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
    void spu_wws_job_pm_spu_func_00001A60(spu_context* ctx);
    void spu_wws_job_pm_spu_func_00001BBC(spu_context* ctx);
    void spu_wrch(spu_context* ctx, uint32_t channel, u128 value);
    void spu_ls_write128(spu_context* ctx, uint32_t lsa, u128 val);
    void spu_wws_job_pm_spu_func_00001CA0(spu_context* ctx);
    void spu_wws_job_pm_spu_func_00001E48(spu_context* ctx);
    void spu_wws_job_pm_spu_func_00002CA4(spu_context* ctx);
    void spu_wws_job_pm_spu_func_00002DD0(spu_context* ctx);
    void spu_wws_job_pm_spu_func_00002D38(spu_context* ctx);
    void spu_wws_job_pm_spu_func_000023A0(spu_context* ctx);
    void spu_wws_job_pm_spu_func_00002DB0(spu_context* ctx);
    void spu_drain_call(spu_context* ctx, uint32_t return_pc);

    static inline u128 gow_spu_nor(u128 a, u128 b) {
        u128 r;
        r._u64[0] = ~(a._u64[0] | b._u64[0]);
        r._u64[1] = ~(a._u64[1] | b._u64[1]);
        return r;
    }

    static inline u128 gow_spu_sfi(u128 a, int32_t imm) {
        u128 r;
        for (int i = 0; i < 4; i++) r._u32[i] = (uint32_t)imm - a._u32[i];
        return r;
    }

    static inline u128 gow_spu_ila(uint32_t imm) {
        u128 r;
        r._u32[0] = r._u32[1] = r._u32[2] = r._u32[3] = imm;
        return r;
    }

    static inline u128 gow_spu_link(uint32_t addr) {
        u128 r;
        memset(&r, 0, sizeof(r));
        r._u32[0] = addr;
        return r;
    }

    #define SPU_W_IDX(P) (((P) & ~3) | (3 - ((P) & 3)))
    static inline u128 gow_spu_shufb(u128 a, u128 b, u128 c) {
        uint8_t cat[32];
        for (int j = 0; j < 16; j++) cat[j]      = a._u8[SPU_W_IDX(j)];
        for (int j = 0; j < 16; j++) cat[16 + j] = b._u8[SPU_W_IDX(j)];
        u128 r;
        for (int t = 0; t < 16; t++) {
            uint8_t s = c._u8[SPU_W_IDX(t)];
            uint8_t v;
            if      ((s & 0xE0) == 0xE0) v = 0x80;
            else if ((s & 0xC0) == 0xC0) v = 0xFF;
            else if ((s & 0xC0) == 0x80) v = 0x00;
            else                         v = cat[s & 0x1F];
            r._u8[SPU_W_IDX(t)] = v;
        }
        return r;
    }

    static inline uint16_t gow_get_spu_hw(const u128& val, int hw) {
        int word = hw >> 1;
        if ((hw & 1) == 0) {
            return (uint16_t)((val._u32[word] >> 16) & 0xFFFF);
        } else {
            return (uint16_t)(val._u32[word] & 0xFFFF);
        }
    }

    static inline void gow_set_spu_hw(u128* val, int hw, uint16_t x) {
        int word = hw >> 1;
        if ((hw & 1) == 0) {
            val->_u32[word] = (val->_u32[word] & 0x0000FFFFu) | ((uint32_t)x << 16);
        } else {
            val->_u32[word] = (val->_u32[word] & 0xFFFF0000u) | (uint32_t)x;
        }
    }

    static inline bool is_wws_job_pm(spu_context* ctx)
    {
        if (!ctx || !ctx->ls) return false;
        extern uint8_t* vm_base;
        uint32_t spurs_ea = ((uint32_t)ctx->ls[0x1C4] << 24) | ((uint32_t)ctx->ls[0x1C5] << 16)
                          | ((uint32_t)ctx->ls[0x1C6] << 8)  | ctx->ls[0x1C7];
        uint32_t wid = ((uint32_t)ctx->ls[0x1DC] << 24) | ((uint32_t)ctx->ls[0x1DD] << 16)
                     | ((uint32_t)ctx->ls[0x1DE] << 8)  | ctx->ls[0x1DF];
        if (vm_base && spurs_ea) {
            uint32_t info_off = (wid < 16) ? (0xB00 + wid * 0x20) : (0x1000 + (wid - 16) * 0x20);
            uint32_t pm_ea = (uint32_t)__builtin_bswap64(*(const uint64_t*)(vm_base + spurs_ea + info_off));
            if (pm_ea == 0x0077CA80u) return true;
        }
        if (ctx->image_id == 2) return true;
        uint32_t sig = ((uint32_t)ctx->ls[0xA00] << 24) | ((uint32_t)ctx->ls[0xA01] << 16)
                     | ((uint32_t)ctx->ls[0xA02] << 8)  | (uint32_t)ctx->ls[0xA03];
        return sig == 0x436CF002u;
    }

    static void gow_barrier_watch_thread_func()
    {
        uint16_t last_val = 0xFFFF;
        bool initialized = false;
        while (true) {
            if (vm_base) {
                uint32_t ea = 0x71600484u;
                uint16_t cur_val = __builtin_bswap16(*(const volatile uint16_t*)(vm_base + ea));
                if (!initialized || cur_val != last_val) {
                    extern unsigned long long ps3_qpc_us(void);
                    double t = (double)ps3_qpc_us() / 1e6;
                    uint32_t main_lr = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->lr : 0;
                    uint32_t main_r3 = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->gpr[3] : 0;
                    fprintf(stderr, "[BARRIER-WATCH] t=%.4fs EA=0x%08X val: 0x%04X -> 0x%04X (%u -> %u) | main_lr=0x%08X r3=0x%08X last_hle=%s (nid=0x%08X)\n",
                            t, ea, last_val, cur_val, last_val, cur_val, main_lr, main_r3,
                            g_last_hle_name ? g_last_hle_name : "none", g_last_hle_nid);
                    fflush(stderr);
                    last_val = cur_val;
                    initialized = true;
                }
            }
            Sleep(1);
        }
    }

    void gow_spu_step_hook(spu_context* ctx)
    {
        if (!ctx) return;
        uint32_t pc = ctx->pc & SPU_LS_MASK;
        if (is_wws_job_pm(ctx) && ctx->gpr[52]._u32[0] == 0x70A9AF00u) {
            // Read clm only when r58 is known to hold the header quadword
            if (pc >= 0x1B08 && pc <= 0x1B90) {
                uint16_t clm = gow_get_spu_hw(ctx->gpr[58], 1);
                if (clm > 0 || (pc == 0x1B10 && ctx->gpr[3]._u32[0] != 0)) {
                    static uint32_t s_b3_active_count = 0;
                    if (++s_b3_active_count <= 120) {
                        uint16_t lim = gow_get_spu_hw(ctx->gpr[58], 0);
                        fprintf(stderr, "[WWS-ACTIVE #%u] spu=%u pc=0x%04X r3=0x%08X r4=0x%08X r61=0x%08X lim=%u clm=%u r69=0x%04X r56=0x%08X\n",
                                s_b3_active_count, ctx->spu_id, pc,
                                ctx->gpr[3]._u32[0], ctx->gpr[4]._u32[0], ctx->gpr[61]._u32[0],
                                lim, clm, (uint32_t)ctx->gpr[69]._u16[0], ctx->gpr[56]._u32[0]);
                    }
                }
            }
        }
    }

    static void gow_wws_job_pm_3fec0_handler(spu_context* ctx)
    {
        if (!is_wws_job_pm(ctx)) return;
        // Dynamic shim at 0x3FEC0 loaded into high LS by SPURS; return to link in r79
        ctx->pc = ctx->gpr[79]._u32[0] & SPU_LS_MASK;
        if (ctx->host_depth == 0) {
            g_spu_trampoline_fn = spu_indirect_branch;
        }
    }

    static void gow_wws_job_pm_a2c_handler(spu_context* ctx)
    {
        if (!is_wws_job_pm(ctx)) return;

        uint32_t saved_srr0 = ctx->srr0;
        u128 saved_gprs[128];
        memcpy(saved_gprs, ctx->gpr, sizeof(saved_gprs));

        static int s_irq_count = 0;
        s_irq_count++;
        if (s_irq_count <= 16) {
            fprintf(stderr, "[wws-irq] SPU img=%d IRQ #%d at 0x00A2C: r0=0x%08X r1=0x%08X srr0=0x%05X stallstat=0x%X loadJobState=%02X%02X%02X%02X\n",
                    ctx->image_id, s_irq_count,
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
            fprintf(stderr, "[wws-irq] SPU img=%d IRQ #%d complete: pc=0x%05X (srr0=0x%05X) diff_regs=%d loadJobState=%02X%02X%02X%02X\n",
                    ctx->image_id, s_irq_count, ctx->pc, saved_srr0, diff_count,
                    ctx->ls[0x12A0], ctx->ls[0x12A1], ctx->ls[0x12A2], ctx->ls[0x12A3]);
        }
    }

    void gow_register_spu_overrides(void)
    {
        static int s_registered = 0;
        if (s_registered) return;
        s_registered = 1;

        // Register for image 2
        spu_begin_image(2);
        spu_register_function(0x3FEC0u, gow_wws_job_pm_3fec0_handler);
        spu_register_function(0x00A2Cu, gow_wws_job_pm_a2c_handler);

        // Register wildcard (image 0) so workloads running with other image IDs resolve to these handlers
        spu_begin_image(0);
        spu_register_function(0x3FEC0u, gow_wws_job_pm_3fec0_handler);
        spu_register_function(0x00A2Cu, gow_wws_job_pm_a2c_handler);

        fprintf(stderr, "[ascension] Registered SPU overrides (img=2, wildcard=0): 0x3FEC0 shim return, IRQ 0x00A2C\n");
    }

    __attribute__((constructor(101))) static void gow_spu_overrides_ctor(void)
    {
        gow_register_spu_overrides();
    }

    extern void gow_psn_stubs_register(void);
    void ps3_load_prx_modules(void) {
        gow_register_spu_overrides();
        gow_psn_stubs_register();
        ensure_monitors_started();
    }

#ifdef _WIN32
extern "C" uint8_t* vm_base;
#define GOW_VM_SIZE 0x100000000ULL

extern "C" uint32_t    g_last_hle_nid;
extern "C" const char* g_last_hle_name;
extern "C" PPU_THREAD_LOCAL ppu_context* g_active_ctx;

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

    void* frames[32];
    USHORT n = RtlCaptureStackBackTrace(0, 32, frames, NULL);
    for (USHORT i = 0; i < n; i++) {
        HMODULE m = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &m);
        char frame_mod[MAX_PATH] = "unknown";
        if (m) GetModuleFileNameA(m, frame_mod, sizeof(frame_mod));
        fprintf(stderr, "[CRASH]   #%-2u %p rva=0x%llX (%s)\n", i, frames[i],
                m ? (unsigned long long)((char*)frames[i] - (char*)m) : 0, frame_mod);
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI gow_crash_veh(EXCEPTION_POINTERS* ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Filter out debugger notifications, thread names, and Intel/MSVC C++ internal exceptions
    if (code == 0x406D1388 || code == 0x40010006 || code == 0x00000000 || code == 0xE06D7363)
        return EXCEPTION_CONTINUE_SEARCH;

    // Filter out vm_commit_veh access violations (guest memory committing on first touch)
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + GOW_VM_SIZE) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    static volatile LONG s_crashed = 0;
    if (InterlockedCompareExchange(&s_crashed, 1, 0) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    fprintf(stderr, "\n==================== [FATAL VEH CRASH TRAP] ====================\n");
    gow_crash_filter(ep);
    fprintf(stderr, "=================================================================\n");
    fflush(stderr);

    return EXCEPTION_CONTINUE_SEARCH;
}

__attribute__((constructor(100))) static void gow_veh_init(void)
{
    AddVectoredExceptionHandler(0, gow_crash_veh);
    fprintf(stderr, "[ascension] Registered Vectored Exception Handler (VEH)\n");
}
#endif

}
