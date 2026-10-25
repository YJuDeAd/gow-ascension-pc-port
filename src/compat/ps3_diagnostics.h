/*
 * ps3_diagnostics.h — Project-wide diagnostics for PPU threads, SPURS workloads, and GCM
 */

#ifndef PS3_DIAGNOSTICS_H
#define PS3_DIAGNOSTICS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum PpuWaitType {
    PPU_WAIT_NONE = 0,
    PPU_WAIT_LWCOND,
    PPU_WAIT_LWMUTEX,
    PPU_WAIT_MUTEX,
    PPU_WAIT_COND,
    PPU_WAIT_EVENT_QUEUE,
    PPU_WAIT_SEMAPHORE,
    PPU_WAIT_USLEEP
};

typedef struct {
    volatile uint32_t last_hle_nid;
    const char* volatile last_hle_name;
    volatile int blocked_type;
    volatile uint64_t blocked_obj;
} ppu_thread_diag_t;

extern ppu_thread_diag_t g_ppu_thread_diags[64];

void ppu_diag_enter_hle(uint32_t tid, uint32_t nid, const char* name);
void ppu_diag_set_wait(uint32_t tid, int wait_type, uint64_t obj);
void ppu_diag_clear_wait(uint32_t tid);
uint32_t ppu_diag_get_current_tid(void);

enum SpursWaitType {
    SPURS_WAIT_NONE = 0,
    SPURS_WAIT_CHANNEL,
    SPURS_WAIT_DMA,
    SPURS_WAIT_EVENT,
    SPURS_WAIT_IDLE
};

struct spu_context;

typedef struct {
    volatile uint64_t dispatched_count;
    volatile uint64_t started_count;
    volatile uint32_t last_pc;
    volatile int blocked_type;
    volatile uint32_t blocked_detail;
    volatile int first_logged;
    struct spu_context* volatile active_ctx;
} spurs_wid_diag_t;

extern spurs_wid_diag_t g_spurs_wid_diags[32];

int ppu_diag_get_thread_status(uint32_t tid, uint32_t* out_lr, char* out_name, size_t name_sz, int* out_state);
uint32_t ppu_diag_get_spurs_pc(uint32_t wid);

#ifdef __cplusplus
}
#endif

#endif /* PS3_DIAGNOSTICS_H */
