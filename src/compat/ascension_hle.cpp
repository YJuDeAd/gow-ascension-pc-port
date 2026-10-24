/*
 * ascension_hle.cpp — Single-Player HLE Extensions and Shims for God of War: Ascension.
 *
 * Implements PS3 firmware imports required by God of War: Ascension that are either
 * missing from upstream ps3recomp runtime or require specific behavioral fixes.
 *
 * Key single-player systems addressed:
 *  1. sys_lwcond_create struct layout fix (32-bit EA into both words for guest FIOS compatibility).
 *  2. sys_ppu_thread_once initializer execution (runs engine initializer once under mutex).
 *  3. SPURS v2 taskset & task attribute forwarding (forwards to v1 runtime calls).
 *  4. cellNetCtlGetState returning IPObtained (3) so network polling loops exit cleanly.
 *  5. cellVideoOutGetDeviceInfo advertising valid 720p/1080p 60Hz display modes.
 *  6. cellFsReadWithOffset / cellFsWriteWithOffset filesystem helpers.
 *  7. cellSpursTasksetGetSpursAddress retrieval.
 */

#include "ppu_recomp.h"
#include "spu_context.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mutex>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include "ps3_diagnostics.h"

typedef int64_t (*gow_lv2_syscall_fn)(ppu_context* ctx);
struct gow_lv2_syscall_table {
    gow_lv2_syscall_fn handlers[1024];
};
extern "C" gow_lv2_syscall_table g_lv2_syscalls;

extern "C" {
void     ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
uint32_t vm_read32(uint64_t a);
void     vm_write32(uint64_t a, uint32_t v);
uint8_t  vm_read8(uint64_t a);
void     vm_write8(uint64_t a, uint8_t v);
uint16_t vm_read16(uint64_t a);
void     vm_write16(uint64_t a, uint16_t v);
uint64_t vm_read64(uint64_t a);
void     vm_write64(uint64_t a, uint64_t v);

uint64_t ppu_guest_call(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
void     ppu_gcm_pump(void);
extern "C" void ppu_gcm_pump_caller(const char* file, int line);
#define ppu_gcm_pump() ppu_gcm_pump_caller(__FILE__, __LINE__)

int32_t  cellGcmMapEaIoAddress(uint32_t ea, uint32_t io, uint32_t size);
int32_t  cellSpursCreateTaskset(void* spurs, void* taskset, uint64_t args,
                                const void* priority, uint32_t maxContention);
int32_t  cellSpursCreateTask(void* taskset, void* taskId, void* elf,
                             void* context, uint32_t sizeContext,
                             uint32_t lsPattern_ea, uint32_t argument_ea);

int32_t  cellFsLseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos);
int32_t  cellFsRead(int32_t fd, void* buf, uint64_t nbytes, uint64_t* nread);
int32_t  cellFsWrite(int32_t fd, const void* buf, uint64_t nbytes, uint64_t* nwritten);

void     spu_taskset_signal_task(uint32_t taskset_ea, uint32_t taskId);
int      spu_taskset_signal_parked_obj(uint32_t taskset_ea, uint32_t wait_obj);
int32_t  _cellSpursLFQueueInitialize(uint64_t owner_ea, uint64_t queue_ea, uint64_t buffer_ea,
                                     uint32_t size, uint32_t depth, uint32_t direction);
int32_t  cellSpursLFQueueAttachLv2EventQueue(uint64_t queue_ea);
int32_t  cellSpursEventFlagSet(void* eventFlag, uint16_t bits);
void     spurs_ef_set_from_spu(uint32_t flag_ea, uint16_t bits);
int      gow_inflate(uint8_t* out, uint32_t out_cap, const uint8_t* src, uint32_t src_len);

void     cellGame_init_from_paramsfo(const char* sfo_path);
extern const char* ppu_vfs_root;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_once (sysPrxForUser 0xA3E3BE68)
 * Runs the initialiser OPD exactly once on the calling thread, flips control word.
 * ------------------------------------------------------------------------- */
static void sys_ppu_thread_once(ppu_context* ctx)
{
    const uint32_t ctrl = (uint32_t)ctx->gpr[3];
    const uint32_t opd  = (uint32_t)ctx->gpr[4];
    static std::mutex once_lock;

    bool run = false;
    {
        std::lock_guard<std::mutex> g(once_lock);
        if (ctrl && vm_read32(ctrl) == 0) {
            vm_write32(ctrl, 1);
            run = true;
        }
    }
    if (run && opd) {
        fprintf(stderr, "[sysprx] sys_ppu_thread_once: running init opd=0x%08X\n", opd);
        ppu_guest_call(opd, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    ctx->gpr[3] = 0;   /* CELL_OK */
}

/* ---------------------------------------------------------------------------
 * sys_prx_register_library (sysPrxForUser 0x42B23552)
 * Registers static library export table with the loader.
 * ------------------------------------------------------------------------- */
static void sys_prx_register_library(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

/* ---------------------------------------------------------------------------
 * cellGcmMapEaIoAddressWithFlags (cellGcmSys 0x626E8518)
 * Forwards to cellGcmMapEaIoAddress.
 * ------------------------------------------------------------------------- */
static void cellGcmMapEaIoAddressWithFlags(ppu_context* ctx)
{
    const uint32_t ea = (uint32_t)ctx->gpr[3], io = (uint32_t)ctx->gpr[4];
    const uint32_t size = (uint32_t)ctx->gpr[5], flags = (uint32_t)ctx->gpr[6];
    fprintf(stderr, "[cellGcmSys] MapEaIoAddressWithFlags(ea=0x%08X, io=0x%08X, size=0x%X, flags=0x%X)\n",
            ea, io, size, flags);
    ctx->gpr[3] = (uint64_t)(int64_t)cellGcmMapEaIoAddress(ea, io, size);
}

/* ---------------------------------------------------------------------------
 * Real sys_lwmutex & sys_lwcond Implementation (PS3 LV2 Semantics)
 * ------------------------------------------------------------------------- */
#define SYS_SYNC_FIFO              0x00000001
#define SYS_SYNC_PRIORITY          0x00000002
#define SYS_SYNC_RECURSIVE         0x00000010
#define SYS_SYNC_NOT_RECURSIVE     0x00000020

#define CELL_OK                    0x00000000
#define CELL_EINVAL                ((int32_t)0x80010002)
#define CELL_ESRCH                 ((int32_t)0x80010005)
#define CELL_EDEADLK               ((int32_t)0x80010008)
#define CELL_EPERM                ((int32_t)0x80010009)
#define CELL_EBUSY                 ((int32_t)0x8001000A)
#define CELL_ETIMEDOUT             ((int32_t)0x8001000B)

#define LWM_OWNER  0x00
#define LWM_ATTR   0x08
#define LWM_RECUR  0x0C
#define LWM_SELF(ctx) ((uint32_t)(ctx)->thread_id ? (uint32_t)(ctx)->thread_id : 0x7FFFFFFEu)

struct GowSyncWaiter {
    uint32_t       thread_id;
    HANDLE         event;
    bool           woken;
    GowSyncWaiter* next;
    GowSyncWaiter* prev;
};

struct GowWaiterQueue {
    GowSyncWaiter* head;
    GowSyncWaiter* tail;
    uint32_t       count;

    void push_back(GowSyncWaiter* w) {
        w->next = nullptr;
        w->prev = tail;
        if (tail) tail->next = w;
        else head = w;
        tail = w;
        count++;
    }

    GowSyncWaiter* pop_front() {
        if (!head) return nullptr;
        GowSyncWaiter* w = head;
        head = w->next;
        if (head) head->prev = nullptr;
        else tail = nullptr;
        w->next = w->prev = nullptr;
        count--;
        return w;
    }

    bool remove(GowSyncWaiter* w) {
        if (!w) return false;
        if (w->prev) w->prev->next = w->next;
        else if (head == w) head = w->next;
        else return false;

        if (w->next) w->next->prev = w->prev;
        else if (tail == w) tail = w->prev;

        w->next = w->prev = nullptr;
        count--;
        return true;
    }
};

struct GowLwMutex {
    uint32_t       ea;
    bool           active;
    bool           recursive;
    uint32_t       owner_tid;
    uint32_t       recursion_count;
    GowWaiterQueue waiters;

    uint64_t       lock_count;
    uint64_t       unlock_count;
    uint64_t       trylock_count;
};

struct GowLwCond {
    uint32_t       ea;
    bool           active;
    uint32_t       lwmutex_ea;
    GowWaiterQueue waiters;

    uint64_t       wait_count;
    uint64_t       signal_count;
    uint64_t       signal_all_count;
    uint64_t       signal_to_count;
    uint64_t       wakeup_count;
    uint64_t       zero_waiter_signals;
    uint32_t       last_waiter_tid;
    uint32_t       last_signaler_tid;
};

#define GOW_SYNC_HASH_SIZE 2048
static GowLwMutex g_gow_mutexes[GOW_SYNC_HASH_SIZE];
static GowLwCond  g_gow_conds[GOW_SYNC_HASH_SIZE];
static CRITICAL_SECTION g_gow_sync_lock;
static bool g_gow_sync_lock_inited = false;

static inline void ensure_sync_lock_inited() {
    if (!g_gow_sync_lock_inited) {
        InitializeCriticalSectionAndSpinCount(&g_gow_sync_lock, 4000);
        g_gow_sync_lock_inited = true;
    }
}

static thread_local HANDLE t_sync_wait_event = NULL;
static inline HANDLE get_thread_sync_wait_event(void) {
    if (!t_sync_wait_event) {
        t_sync_wait_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    }
    return t_sync_wait_event;
}

static GowLwMutex* find_or_create_mutex(uint32_t ea) {
    if (!ea) return nullptr;
    uint32_t h = (ea * 2654435761u) & (GOW_SYNC_HASH_SIZE - 1);
    for (uint32_t i = 0; i < GOW_SYNC_HASH_SIZE; i++) {
        uint32_t idx = (h + i) & (GOW_SYNC_HASH_SIZE - 1);
        if (g_gow_mutexes[idx].ea == ea && g_gow_mutexes[idx].active) {
            return &g_gow_mutexes[idx];
        }
        if (!g_gow_mutexes[idx].active) {
            g_gow_mutexes[idx].ea = ea;
            g_gow_mutexes[idx].active = true;
            uint32_t attr = vm_read32(ea + LWM_ATTR);
            g_gow_mutexes[idx].recursive = (attr != SYS_SYNC_NOT_RECURSIVE);
            g_gow_mutexes[idx].owner_tid = 0;
            g_gow_mutexes[idx].recursion_count = 0;
            g_gow_mutexes[idx].waiters.head = nullptr;
            g_gow_mutexes[idx].waiters.tail = nullptr;
            g_gow_mutexes[idx].waiters.count = 0;
            g_gow_mutexes[idx].lock_count = 0;
            g_gow_mutexes[idx].unlock_count = 0;
            g_gow_mutexes[idx].trylock_count = 0;
            return &g_gow_mutexes[idx];
        }
    }
    return nullptr;
}

static GowLwCond* find_or_create_cond(uint32_t ea) {
    if (!ea) return nullptr;
    uint32_t h = (ea * 2654435761u) & (GOW_SYNC_HASH_SIZE - 1);
    for (uint32_t i = 0; i < GOW_SYNC_HASH_SIZE; i++) {
        uint32_t idx = (h + i) & (GOW_SYNC_HASH_SIZE - 1);
        if (g_gow_conds[idx].ea == ea && g_gow_conds[idx].active) {
            return &g_gow_conds[idx];
        }
        if (!g_gow_conds[idx].active) {
            g_gow_conds[idx].ea = ea;
            g_gow_conds[idx].active = true;
            g_gow_conds[idx].lwmutex_ea = vm_read32(ea + 0x00);
            g_gow_conds[idx].waiters.head = nullptr;
            g_gow_conds[idx].waiters.tail = nullptr;
            g_gow_conds[idx].waiters.count = 0;
            g_gow_conds[idx].wait_count = 0;
            g_gow_conds[idx].signal_count = 0;
            g_gow_conds[idx].signal_all_count = 0;
            g_gow_conds[idx].signal_to_count = 0;
            g_gow_conds[idx].wakeup_count = 0;
            g_gow_conds[idx].zero_waiter_signals = 0;
            g_gow_conds[idx].last_waiter_tid = 0;
            g_gow_conds[idx].last_signaler_tid = 0;
            return &g_gow_conds[idx];
        }
    }
    return nullptr;
}

static void gow_sys_lwmutex_create(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwm  = (uint32_t)ctx->gpr[3];
    uint32_t attr = (uint32_t)ctx->gpr[4];
    if (!lwm) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    if (lwm == 0x15E67BA0u || (lwm & 0xFFFFF000u) == 0x15E67000u) {
        fprintf(stderr, "[GOW-LWMUTEX-CREATE] tid=%u lwm=0x%08X attr=0x%08X lr=0x%08X\n",
                (uint32_t)ctx->thread_id, lwm, attr, (uint32_t)ctx->lr);
    }

    uint32_t protocol = attr ? vm_read32(attr + 0) : SYS_SYNC_FIFO;
    uint32_t rec_attr = attr ? vm_read32(attr + 4) : SYS_SYNC_RECURSIVE;
    bool is_recursive = (rec_attr != SYS_SYNC_NOT_RECURSIVE);

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwMutex* m = find_or_create_mutex(lwm);
    if (m) {
        m->ea = lwm;
        m->active = true;
        m->recursive = is_recursive;
        m->owner_tid = 0;
        m->recursion_count = 0;
        while (GowSyncWaiter* w = m->waiters.pop_front()) {
            w->woken = true;
            SetEvent(w->event);
        }
    }
    LeaveCriticalSection(&g_gow_sync_lock);

    static uint32_t s_next_q_id = 0x51000000u;
    vm_write32(lwm + 0x00, 0);                 /* owner */
    vm_write32(lwm + 0x04, 0);                 /* waiter */
    vm_write32(lwm + LWM_ATTR, rec_attr);      /* attribute */
    vm_write32(lwm + LWM_RECUR, 0);            /* recursive_count */
    vm_write32(lwm + 0x10, ++s_next_q_id);     /* sleep_queue */
    vm_write32(lwm + 0x14, 0);

    ctx->gpr[3] = CELL_OK;
}

static void gow_sys_lwmutex_destroy(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwm = (uint32_t)ctx->gpr[3];
    if (!lwm) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwMutex* m = find_or_create_mutex(lwm);
    if (m && (m->owner_tid != 0 || m->waiters.count > 0)) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EBUSY;
        return;
    }
    if (m) {
        m->active = false;
        m->ea = 0;
        m->owner_tid = 0;
        m->recursion_count = 0;
    }
    LeaveCriticalSection(&g_gow_sync_lock);

    vm_write32(lwm + 0x00, 0);
    vm_write32(lwm + 0x04, 0);
    vm_write32(lwm + 0x08, 0);
    vm_write32(lwm + 0x0C, 0);
    vm_write32(lwm + 0x10, 0);
    ctx->gpr[3] = CELL_OK;
}


static void gow_sys_lwmutex_lock(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwm = (uint32_t)ctx->gpr[3];
    uint64_t timeout_us = ctx->gpr[4];
    uint32_t self = LWM_SELF(ctx);
    uint32_t tid = (uint32_t)ctx->thread_id;
    if (!lwm) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    DWORD timeout_ms = INFINITE;
    if (timeout_us > 0) {
        uint64_t m = (timeout_us + 999) / 1000;
        timeout_ms = (m > 0xFFFFFFFEull) ? 0xFFFFFFFEu : (DWORD)m;
    }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwMutex* m = find_or_create_mutex(lwm);
    if (!m) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL;
        return;
    }
    m->lock_count++;

    // 1. Caller already holds lock
    if (m->owner_tid == self && m->recursion_count > 0) {
        if (!m->recursive) {
            LeaveCriticalSection(&g_gow_sync_lock);
            ctx->gpr[3] = (uint64_t)(int64_t)CELL_EDEADLK;
            return;
        }
        m->recursion_count++;
        vm_write32(lwm + LWM_RECUR, m->recursion_count);
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = CELL_OK;
        return;
    }

    // 2. Free lock
    if (m->owner_tid == 0) {
        m->owner_tid = self;
        m->recursion_count = 1;
        vm_write32(lwm + LWM_OWNER, self);
        vm_write32(lwm + LWM_RECUR, 1);
        vm_write32(lwm + 0x04, m->waiters.count);
        LeaveCriticalSection(&g_gow_sync_lock);
        if (lwm == 0x009976F0u && tid == 1) {
            static uint32_t s_spin_99 = 0;
            static uint32_t s_last_state = 0xFFFFFFFF;
            s_spin_99++;
            uint32_t state = vm_read32(0x00997578u);
            if ((state == 2 || state == 3) && (s_spin_99 % 32 == 0)) {
                Sleep(1);
            } else if (s_spin_99 % 500 == 0) {
                SwitchToThread();
            }
            uint32_t rem_bytes = vm_read32(0x009976E0u);
            uint32_t tot_bytes = vm_read32(0x009976E4u);
            uint32_t ring_ptr = vm_read32(0x0082B708u);
            uint32_t ring_sz = ring_ptr ? vm_read32(ring_ptr + 8) : 0;

            if (state != s_last_state || (s_spin_99 % 20000) == 1) {
                s_last_state = state;
                uint32_t flags = vm_read32(0x00997688u);
                uint32_t req = vm_read32(0x00997580u + 0x104);
                uint32_t sl = 0x009998ACu;
                uint32_t sl_104 = vm_read32(sl + 0x104);
                uint32_t sl_108 = vm_read32(sl + 0x108);
                uint32_t r10 = sl_104 ? vm_read32(sl_104 + 0x10) : 0;
                uint32_t r14 = sl_104 ? vm_read32(sl_104 + 0x14) : 0;
                uint32_t r18 = sl_104 ? vm_read32(sl_104 + 0x18) : 0;
                uint32_t c10 = (r10 && r10 < 0xE0000000u) ? vm_read32(r10 + 0x98) : 0;
                uint32_t c14 = (r14 && r14 < 0xE0000000u) ? vm_read32(r14 + 0x98) : 0;
                uint32_t c18 = (r18 && r18 < 0xE0000000u) ? vm_read32(r18 + 0x98) : 0;
                uint32_t op10 = (r10 && r10 < 0xE0000000u) ? vm_read32(r10 + 0x40) : 0;
                uint32_t op14 = (r14 && r14 < 0xE0000000u) ? vm_read32(r14 + 0x40) : 0;
                uint32_t op18 = (r18 && r18 < 0xE0000000u) ? vm_read32(r18 + 0x40) : 0;
                fprintf(stderr, "[SCENE-POLL #%u] state=%u req=0x%08X flags=0x%08X rem=%u tot=%u ring_sz=%u | sl_104=0x%08X sl_108=%u | r10=0x%08X(op=%u,c=%u) r14=0x%08X(op=%u,c=%u) r18=0x%08X(op=%u,c=%u)\n",
                        s_spin_99, state, req, flags, rem_bytes, tot_bytes, ring_sz,
                        sl_104, sl_108, r10, op10, c10, r14, op14, c14, r18, op18, c18);
            }
        }
        ctx->gpr[3] = CELL_OK;
        return;
    }

    // 3. Contended -> Block in FIFO queue
    GowSyncWaiter waiter;
    waiter.thread_id = tid;
    waiter.event = get_thread_sync_wait_event();
    waiter.woken = false;
    ResetEvent(waiter.event);

    m->waiters.push_back(&waiter);
    vm_write32(lwm + 0x04, m->waiters.count);

    ppu_diag_set_wait(tid, PPU_WAIT_LWMUTEX, lwm);
    LeaveCriticalSection(&g_gow_sync_lock);

    DWORD wr = WaitForSingleObject(waiter.event, timeout_ms);

    EnterCriticalSection(&g_gow_sync_lock);
    ppu_diag_clear_wait(tid);

    if (waiter.woken) {
        // Ownership transferred directly by unlocker
        vm_write32(lwm + 0x04, m->waiters.count);
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = CELL_OK;
        return;
    }

    // Timed out
    m->waiters.remove(&waiter);
    vm_write32(lwm + 0x04, m->waiters.count);
    LeaveCriticalSection(&g_gow_sync_lock);
    ctx->gpr[3] = (uint64_t)(int64_t)CELL_ETIMEDOUT;
}

static void gow_sys_lwmutex_trylock(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwm = (uint32_t)ctx->gpr[3];
    uint32_t self = LWM_SELF(ctx);
    if (!lwm) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwMutex* m = find_or_create_mutex(lwm);
    if (!m) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL;
        return;
    }
    m->trylock_count++;

    if (m->owner_tid == self && m->recursion_count > 0) {
        if (!m->recursive) {
            LeaveCriticalSection(&g_gow_sync_lock);
            ctx->gpr[3] = (uint64_t)(int64_t)CELL_EBUSY;
            return;
        }
        m->recursion_count++;
        vm_write32(lwm + LWM_RECUR, m->recursion_count);
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = CELL_OK;
        return;
    }

    if (m->owner_tid == 0) {
        m->owner_tid = self;
        m->recursion_count = 1;
        vm_write32(lwm + LWM_OWNER, self);
        vm_write32(lwm + LWM_RECUR, 1);
        vm_write32(lwm + 0x04, m->waiters.count);
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = CELL_OK;
        return;
    }

    LeaveCriticalSection(&g_gow_sync_lock);
    ctx->gpr[3] = (uint64_t)(int64_t)CELL_EBUSY;
}

static void gow_sys_lwmutex_unlock(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwm = (uint32_t)ctx->gpr[3];
    uint32_t self = LWM_SELF(ctx);
    if (!lwm) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwMutex* m = find_or_create_mutex(lwm);
    if (!m) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL;
        return;
    }
    m->unlock_count++;

    if (m->owner_tid != self || m->recursion_count == 0) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EPERM;
        return;
    }

    m->recursion_count--;
    if (m->recursion_count > 0) {
        vm_write32(lwm + LWM_RECUR, m->recursion_count);
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = CELL_OK;
        return;
    }

    // Fully unlocked: baton pass to next waiter
    GowSyncWaiter* next_w = m->waiters.pop_front();
    if (next_w) {
        m->owner_tid = next_w->thread_id ? next_w->thread_id : 0x7FFFFFFEu;
        m->recursion_count = 1;
        next_w->woken = true;
        vm_write32(lwm + LWM_OWNER, m->owner_tid);
        vm_write32(lwm + LWM_RECUR, 1);
        vm_write32(lwm + 0x04, m->waiters.count);
        SetEvent(next_w->event);
    } else {
        m->owner_tid = 0;
        m->recursion_count = 0;
        vm_write32(lwm + LWM_OWNER, 0);
        vm_write32(lwm + LWM_RECUR, 0);
        vm_write32(lwm + 0x04, 0);
    }

    LeaveCriticalSection(&g_gow_sync_lock);
    ctx->gpr[3] = CELL_OK;
}

static void gow_sys_lwmutex_reset_all(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    EnterCriticalSection(&g_gow_sync_lock);
    for (uint32_t i = 0; i < GOW_SYNC_HASH_SIZE; i++) {
        if (g_gow_mutexes[i].active) {
            g_gow_mutexes[i].owner_tid = 0;
            g_gow_mutexes[i].recursion_count = 0;
            while (GowSyncWaiter* w = g_gow_mutexes[i].waiters.pop_front()) {
                w->woken = true;
                SetEvent(w->event);
            }
        }
        if (g_gow_conds[i].active) {
            while (GowSyncWaiter* w = g_gow_conds[i].waiters.pop_front()) {
                w->woken = true;
                SetEvent(w->event);
            }
        }
    }
    LeaveCriticalSection(&g_gow_sync_lock);
    ctx->gpr[3] = CELL_OK;
}

static void gow_sys_lwcond_create(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwcond  = (uint32_t)ctx->gpr[3];
    uint32_t lwmutex = (uint32_t)ctx->gpr[4];
    if (!lwcond || !lwmutex) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    if (lwcond == 0x15E67BE0u || lwmutex == 0x15E67BA0u || (lwcond & 0xFFFFF000u) == 0x15E67000u) {
        fprintf(stderr, "[GOW-LWCOND-CREATE] tid=%u lwcond=0x%08X lwmutex=0x%08X lr=0x%08X\n",
                (uint32_t)ctx->thread_id, lwcond, lwmutex, (uint32_t)ctx->lr);
    }

    static uint32_t s_lwcond_id = 0x4C000000u;
    vm_write32(lwcond + 0x00, lwmutex);   /* 32-bit lwmutex pointer */
    vm_write32(lwcond + 0x04, ++s_lwcond_id);   /* lwcond_queue */

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwCond* c = find_or_create_cond(lwcond);
    if (c) {
        c->ea = lwcond;
        c->active = true;
        c->lwmutex_ea = lwmutex;
        while (GowSyncWaiter* w = c->waiters.pop_front()) {
            w->woken = true;
            SetEvent(w->event);
        }
    }
    LeaveCriticalSection(&g_gow_sync_lock);

    ctx->gpr[3] = CELL_OK;
}

static void gow_sys_lwcond_destroy(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwcond = (uint32_t)ctx->gpr[3];
    if (!lwcond) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwCond* c = find_or_create_cond(lwcond);
    if (c && c->waiters.count > 0) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EBUSY;
        return;
    }
    if (c) {
        c->active = false;
        c->ea = 0;
        c->lwmutex_ea = 0;
    }
    LeaveCriticalSection(&g_gow_sync_lock);

    vm_write32(lwcond + 0x00, 0);
    vm_write32(lwcond + 0x04, 0);
    ctx->gpr[3] = CELL_OK;
}

static void gow_sys_lwcond_signal(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwcond = (uint32_t)ctx->gpr[3];
    uint32_t tid = (uint32_t)ctx->thread_id;
    if (!lwcond) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwCond* c = find_or_create_cond(lwcond);
    if (c) {
        c->signal_count++;
        c->last_signaler_tid = tid;

        GowSyncWaiter* w = c->waiters.pop_front();
        if (w) {
            w->woken = true;
            SetEvent(w->event);
        } else {
            c->zero_waiter_signals++;
        }
    }
    LeaveCriticalSection(&g_gow_sync_lock);
    ctx->gpr[3] = CELL_OK;
}

static void gow_sys_lwcond_signal_all(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwcond = (uint32_t)ctx->gpr[3];
    uint32_t tid = (uint32_t)ctx->thread_id;
    if (!lwcond) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwCond* c = find_or_create_cond(lwcond);
    if (c) {
        c->signal_all_count++;
        c->last_signaler_tid = tid;

        if (c->waiters.count == 0) {
            c->zero_waiter_signals++;
        } else {
            while (GowSyncWaiter* w = c->waiters.pop_front()) {
                w->woken = true;
                SetEvent(w->event);
            }
        }
    }
    LeaveCriticalSection(&g_gow_sync_lock);
    ctx->gpr[3] = CELL_OK;
}

static void gow_sys_lwcond_signal_to(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwcond = (uint32_t)ctx->gpr[3];
    uint32_t target_tid = (uint32_t)ctx->gpr[4];
    uint32_t tid = (uint32_t)ctx->thread_id;
    if (!lwcond) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwCond* c = find_or_create_cond(lwcond);
    if (!c) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL;
        return;
    }
    c->signal_to_count++;
    c->last_signaler_tid = tid;

    GowSyncWaiter* found = nullptr;
    for (GowSyncWaiter* curr = c->waiters.head; curr != nullptr; curr = curr->next) {
        if (curr->thread_id == target_tid) {
            found = curr;
            break;
        }
    }

    if (!found) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_ESRCH;
        return;
    }

    c->waiters.remove(found);
    found->woken = true;
    SetEvent(found->event);
    LeaveCriticalSection(&g_gow_sync_lock);
    ctx->gpr[3] = CELL_OK;
}

static void gow_sys_lwcond_wait(ppu_context* ctx)
{
    ensure_sync_lock_inited();
    uint32_t lwcond = (uint32_t)ctx->gpr[3];
    uint64_t timeout_us = ctx->gpr[4];
    uint32_t self = LWM_SELF(ctx);
    uint32_t tid = (uint32_t)ctx->thread_id;
    if (!lwcond) { ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL; return; }

    static uint32_t s_cond_log_cnt = 0;
    if ((tid == 1 || lwcond == 0x15E67BE0u) && s_cond_log_cnt++ < 5) {
        uint32_t sp = (uint32_t)ctx->gpr[1];
        uint32_t caller_lr = vm_read32(sp + 0xB0);
        uint32_t caller_sp = vm_read32(sp);
        fprintf(stderr, "\n============================================================\n");
        fprintf(stderr, ">>> [GOW-LWCOND-WAIT] tid=%u lwcond=0x%08X timeout_us=%llu lr=0x%08X sp=0x%08X <<<\n",
                tid, lwcond, (unsigned long long)timeout_us, (uint32_t)ctx->lr, sp);
        fprintf(stderr, "  caller of wait: lr=0x%08X caller_sp=0x%08X\n", caller_lr, caller_sp);
        uint32_t s = sp;
        for (int f = 0; f < 10 && s >= 0x0F000000 && s < 0x0FF00000; f++) {
            uint32_t ns = vm_read32(s);
            uint32_t l1 = vm_read32(s + 0x10);
            uint32_t l2 = (ns >= 0x0F000000 && ns < 0x0FF00000) ? vm_read32(ns + 0x10) : 0;
            fprintf(stderr, "    frame[%d]: sp=0x%08X next_sp=0x%08X lr@+10=0x%08X lr@next+10=0x%08X\n",
                    f, s, ns, l1, l2);
            if (ns <= s) break;
            s = ns;
        }
        uint32_t base = lwcond & ~0x7Fu;
        fprintf(stderr, "  [DUMP 0x%08X..0x%08X]:\n", base, base + 0x80);
        for (uint32_t o = 0; o < 0x80; o += 0x10) {
            fprintf(stderr, "    +%02X: %08X %08X %08X %08X\n",
                    o, vm_read32(base+o), vm_read32(base+o+4), vm_read32(base+o+8), vm_read32(base+o+12));
        }
        fprintf(stderr, "============================================================\n\n");
    }

    DWORD timeout_ms = INFINITE;
    if (timeout_us > 0) {
        uint64_t m = (timeout_us + 999) / 1000;
        timeout_ms = (m > 0xFFFFFFFEull) ? 0xFFFFFFFEu : (DWORD)m;
    } else if (lwcond == 0x15E67BE0u) {
        timeout_ms = 50; /* Do not deadlock on un-signaled Edge cond */
    }

    EnterCriticalSection(&g_gow_sync_lock);
    GowLwCond* c = find_or_create_cond(lwcond);
    uint32_t lwm_ea = (c && c->lwmutex_ea) ? c->lwmutex_ea : vm_read32(lwcond + 0x00);
    if (!lwm_ea) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL;
        return;
    }
    if (c) c->lwmutex_ea = lwm_ea;
    GowLwMutex* m = find_or_create_mutex(lwm_ea);
    if (!m) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EINVAL;
        return;
    }

    // Verify caller holds the mutex
    if (m->owner_tid != self || m->recursion_count == 0) {
        LeaveCriticalSection(&g_gow_sync_lock);
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_EPERM;
        return;
    }

    // 1. Save recursion count
    uint32_t saved_recur = m->recursion_count;

    // 2. Fully release mutex to other threads
    GowSyncWaiter* next_m_waiter = m->waiters.pop_front();
    if (next_m_waiter) {
        m->owner_tid = next_m_waiter->thread_id ? next_m_waiter->thread_id : 0x7FFFFFFEu;
        m->recursion_count = 1;
        next_m_waiter->woken = true;
        vm_write32(lwm_ea + LWM_OWNER, m->owner_tid);
        vm_write32(lwm_ea + LWM_RECUR, 1);
        vm_write32(lwm_ea + 0x04, m->waiters.count);
        SetEvent(next_m_waiter->event);
    } else {
        m->owner_tid = 0;
        m->recursion_count = 0;
        vm_write32(lwm_ea + LWM_OWNER, 0);
        vm_write32(lwm_ea + LWM_RECUR, 0);
        vm_write32(lwm_ea + 0x04, 0);
    }

    // 3. Register on cond queue
    GowSyncWaiter cond_waiter;
    cond_waiter.thread_id = tid;
    cond_waiter.event = get_thread_sync_wait_event();
    cond_waiter.woken = false;
    ResetEvent(cond_waiter.event);

    if (c) {
        c->waiters.push_back(&cond_waiter);
        c->wait_count++;
        c->last_waiter_tid = tid;
    }

    ppu_diag_set_wait(tid, PPU_WAIT_LWCOND, lwcond);
    LeaveCriticalSection(&g_gow_sync_lock);

    // 4. Sleep on CV event
    DWORD wr = WaitForSingleObject(cond_waiter.event, timeout_ms);

    EnterCriticalSection(&g_gow_sync_lock);
    ppu_diag_clear_wait(tid);

    bool timed_out = false;
    if (cond_waiter.woken) {
        if (c) c->wakeup_count++;
    } else {
        if (c) c->waiters.remove(&cond_waiter);
        timed_out = true;
        if (lwcond == 0x15E67BE0u) {
            fprintf(stderr, ">>> [EDGE-COND] lwcond 0x15E67BE0 timed out (50ms) - returning CELL_OK to unblock tid=%u <<<\n", tid);
            timed_out = false;
        }
    }

    // 5. Must re-acquire the lwmutex before returning
    if (m->owner_tid == 0) {
        m->owner_tid = self;
        m->recursion_count = saved_recur;
        vm_write32(lwm_ea + LWM_OWNER, self);
        vm_write32(lwm_ea + LWM_RECUR, saved_recur);
        vm_write32(lwm_ea + 0x04, m->waiters.count);
    } else {
        GowSyncWaiter mutex_waiter;
        mutex_waiter.thread_id = tid;
        mutex_waiter.event = get_thread_sync_wait_event();
        mutex_waiter.woken = false;
        ResetEvent(mutex_waiter.event);

        m->waiters.push_back(&mutex_waiter);
        vm_write32(lwm_ea + 0x04, m->waiters.count);
        ppu_diag_set_wait(tid, PPU_WAIT_LWMUTEX, lwm_ea);
        LeaveCriticalSection(&g_gow_sync_lock);

        WaitForSingleObject(mutex_waiter.event, INFINITE);

        EnterCriticalSection(&g_gow_sync_lock);
        ppu_diag_clear_wait(tid);

        m->owner_tid = self;
        m->recursion_count = saved_recur;
        vm_write32(lwm_ea + LWM_OWNER, self);
        vm_write32(lwm_ea + LWM_RECUR, saved_recur);
        vm_write32(lwm_ea + 0x04, m->waiters.count);
    }

    LeaveCriticalSection(&g_gow_sync_lock);

    if (timed_out) {
        ctx->gpr[3] = (uint64_t)(int64_t)CELL_ETIMEDOUT;
    } else {
        ctx->gpr[3] = CELL_OK;
    }
}

extern "C" void gow_dump_sync_stats(void)
{
    ensure_sync_lock_inited();
    EnterCriticalSection(&g_gow_sync_lock);
    fprintf(stderr, "\n=== [SYNC-AUDIT T=3s] Condition Variables (sys_lwcond) ===\n");
    for (uint32_t i = 0; i < GOW_SYNC_HASH_SIZE; i++) {
        if (!g_gow_conds[i].active) continue;
        GowLwCond* c = &g_gow_conds[i];
        if (c->wait_count == 0 && c->signal_count == 0 && c->signal_all_count == 0 && c->waiters.count == 0) continue;
        uint32_t m_owner = c->lwmutex_ea ? vm_read32(c->lwmutex_ea + LWM_OWNER) : 0;
        uint32_t m_recur = c->lwmutex_ea ? vm_read32(c->lwmutex_ea + LWM_RECUR) : 0;
        fprintf(stderr, "  cond=0x%08X  mutex=0x%08X (owner=tid_%-2u recur=%u)  waits=%-8llu wakeups=%-8llu active_waiters=%u  signals=%-6llu sig_all=%-6llu sig_to=%-4llu zero_wait_sigs=%llu\n",
                c->ea, c->lwmutex_ea, m_owner, m_recur,
                (unsigned long long)c->wait_count, (unsigned long long)c->wakeup_count,
                c->waiters.count,
                (unsigned long long)c->signal_count, (unsigned long long)c->signal_all_count,
                (unsigned long long)c->signal_to_count,
                (unsigned long long)c->zero_waiter_signals);
        if (c->waiters.count > 0 && (c->signal_count + c->signal_all_count + c->signal_to_count == 0)) {
            fprintf(stderr, "    >>> [LWCOND-FLAG] cond 0x%08X has %u ACTIVE WAITERS and ZERO SIGNALS! (last_tid=%u) <<<\n",
                    c->ea, c->waiters.count, c->last_waiter_tid);
        }
    }
    fprintf(stderr, "=== [SYNC-AUDIT T=3s] Mutexes (sys_lwmutex) Top Active ===\n");
    for (uint32_t i = 0; i < GOW_SYNC_HASH_SIZE; i++) {
        if (!g_gow_mutexes[i].active) continue;
        GowLwMutex* m = &g_gow_mutexes[i];
        if (m->lock_count == 0 && m->unlock_count == 0 && m->trylock_count == 0) continue;
        uint32_t owner = vm_read32(m->ea + LWM_OWNER);
        uint32_t recur = vm_read32(m->ea + LWM_RECUR);
        fprintf(stderr, "  mutex=0x%08X  locks=%-10llu unlocks=%-10llu trylocks=%-6llu owner=tid_%-2u recur=%u waiters=%u\n",
                m->ea, (unsigned long long)m->lock_count, (unsigned long long)m->unlock_count,
                (unsigned long long)m->trylock_count, owner, recur, m->waiters.count);
    }
    fprintf(stderr, "==========================================================\n\n");
    fflush(stderr);
    LeaveCriticalSection(&g_gow_sync_lock);
}


extern "C" void gow_fios_diag_tick(void)
{
    static int s_tick_count = 0;
    s_tick_count++;

    uint32_t sync_head = vm_read32(0x008B25C0u);
    uint32_t pend_head = vm_read32(0x008B25B8u);
    uint32_t spin_mtx  = vm_read32(0x0081ACC8u);

    uint32_t sm_owner = spin_mtx ? vm_read32(spin_mtx + 0) : 0;
    uint32_t sm_recur = spin_mtx ? vm_read32(spin_mtx + 0x0C) : 0;

    fprintf(stderr, "[FIOS-DIAG #%d] sync_head=0x%08X pend_head=0x%08X spin_mutex=0x%08X (owner=tid_%u recur=%u)\n",
            s_tick_count, sync_head, pend_head, spin_mtx, sm_owner, sm_recur);

    if (sync_head && sync_head < 0xE0000000u) {
        uint32_t w[64];
        for (int i = 0; i < 64; i++) w[i] = vm_read32(sync_head + i * 4);
        fprintf(stderr, "  [FIOS-NODE 0x%08X] 64 words dump:\n", sync_head);
        for (int row = 0; row < 8; row++) {
            int base = row * 8;
            fprintf(stderr, "    +%03X: %08X %08X %08X %08X  %08X %08X %08X %08X\n",
                    base * 4,
                    w[base+0], w[base+1], w[base+2], w[base+3],
                    w[base+4], w[base+5], w[base+6], w[base+7]);
        }

        // Check fixed slots +0x10, +0x14, +0x18, +0x11C
        uint32_t fixed_slots[4] = { 0x10, 0x14, 0x18, 0x11C };
        for (int f = 0; f < 4; f++) {
            uint32_t req = vm_read32(sync_head + fixed_slots[f]);
            if (req && req < 0xE0000000u) {
                uint32_t comp = vm_read32(req + 0x98);
                uint32_t sched = vm_read32(req + 0x9C);
                uint32_t magic = vm_read32(req + 0xDC);
                uint32_t flags = vm_read32(req + 0x14);
                uint32_t buf = vm_read32(req + 0x20);
                uint64_t sz = vm_read64(req + 0x28);
                uint32_t op = vm_read32(req + 0x40);
                char path_str[64] = {0};
                uint32_t path_ea = vm_read32(req + 0x30);
                if (path_ea && path_ea < 0xE0000000u) {
                    for (int c = 0; c < 63; c++) {
                        char ch = (char)vm_read8(path_ea + c);
                        if (!ch) break;
                        path_str[c] = (ch >= 32 && ch < 127) ? ch : '?';
                    }
                }
                fprintf(stderr, "    [FIXED-REQ +0x%03X] req=0x%08X comp=%u sched=0x%08X magic=0x%08X op=%u flags=0x%08X buf=0x%08X sz=%llu path='%s'\n",
                        fixed_slots[f], req, comp, sched, magic, op, flags, buf, (unsigned long long)sz, path_str);
            }
        }

        // Check 64 slot array +0x1C .. +0x118
        int active_slots = 0;
        for (int s = 0; s < 64; s++) {
            uint32_t slot_ea = sync_head + 0x1C + s * 4;
            uint32_t req = vm_read32(slot_ea);
            if (req && req < 0xE0000000u) {
                active_slots++;
                uint32_t comp = vm_read32(req + 0x98);
                uint32_t sched = vm_read32(req + 0x9C);
                uint32_t magic = vm_read32(req + 0xDC);
                uint32_t flags = vm_read32(req + 0x14);
                uint32_t buf = vm_read32(req + 0x20);
                uint64_t sz = vm_read64(req + 0x28);
                uint32_t op = vm_read32(req + 0x40);
                char path_str[64] = {0};
                uint32_t path_ea = vm_read32(req + 0x30);
                if (path_ea && path_ea < 0xE0000000u) {
                    for (int c = 0; c < 63; c++) {
                        char ch = (char)vm_read8(path_ea + c);
                        if (!ch) break;
                        path_str[c] = (ch >= 32 && ch < 127) ? ch : '?';
                    }
                }
                fprintf(stderr, "    [SLOT-REQ #%02d] req=0x%08X comp=%u sched=0x%08X magic=0x%08X op=%u flags=0x%08X buf=0x%08X sz=%llu path='%s'\n",
                        s, req, comp, sched, magic, op, flags, buf, (unsigned long long)sz, path_str);
            }
        }
        fprintf(stderr, "    Total active slots in node: %d / 64\n", active_slots);
    }
    fflush(stderr);
}

/* ---------------------------------------------------------------------------
 * cellSpurs Taskset2 & Task2 APIs
 * ------------------------------------------------------------------------- */
#define TSA2_SIZE      512
#define TSA2_ARGS      0x08
#define TSA2_PRIORITY  0x10
#define TSA2_MAXCONT   0x18

extern "C" int32_t cellSpursInitializeWithAttribute(void* spurs, const void* attr);
static void cellSpursInitializeWithAttribute2(ppu_context* ctx)
{
    const uint32_t spurs = (uint32_t)ctx->gpr[3];
    const uint32_t attr = (uint32_t)ctx->gpr[4];
    fprintf(stderr, "[cellSpurs] InitializeWithAttribute2(spurs=0x%08X, attr=0x%08X)\n", spurs, attr);
    int32_t rc = cellSpursInitializeWithAttribute((void*)(uintptr_t)spurs, (const void*)(uintptr_t)attr);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

extern "C" int32_t cellSpursJoinTask(void* taskset, uint32_t taskId, int32_t* exitCode);

static void cellSpursJoinTask2(ppu_context* ctx)
{
    const uint32_t taskset = (uint32_t)ctx->gpr[3];
    const uint32_t taskId = (uint32_t)ctx->gpr[4];
    int32_t* exitCode = (int32_t*)(uintptr_t)ctx->gpr[5];
    ctx->gpr[3] = (uint64_t)(int64_t)cellSpursJoinTask((void*)(uintptr_t)taskset, taskId, exitCode);
}

static void cellSpursTryJoinTask2(ppu_context* ctx)
{
    const uint32_t taskset = (uint32_t)ctx->gpr[3];
    const uint32_t taskId = (uint32_t)ctx->gpr[4];
    int32_t* exitCode = (int32_t*)(uintptr_t)ctx->gpr[5];
    ctx->gpr[3] = (uint64_t)(int64_t)cellSpursJoinTask((void*)(uintptr_t)taskset, taskId, exitCode);
}

static void cellSpursGetWorkloadFlag(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

static void cellGamePatchCheck(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

static void cellSysmoduleSetMemcontainer(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

static void sys_prx_load_module(ppu_context* ctx)
{
    ctx->gpr[3] = 1;   /* fake module id */
}

static void sys_game_process_exitspawn2(ppu_context* ctx)
{
    fprintf(stderr, "[sysprx] sys_game_process_exitspawn2 called\n");
    ctx->gpr[3] = 0;
}

static void cellSpursTasksetAttribute2Initialize(ppu_context* ctx)
{
    const uint32_t attr = (uint32_t)ctx->gpr[3];
    const uint32_t revision = (uint32_t)ctx->gpr[4];
    if (!attr) { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x80410901; return; }
    for (uint32_t o = 0; o < TSA2_SIZE; o += 4) vm_write32(attr + o, 0);
    vm_write32(attr + 0x00, revision);
    vm_write32(attr + TSA2_PRIORITY + 0, 0x01010101);   /* priority[0..7] = 1 */
    vm_write32(attr + TSA2_PRIORITY + 4, 0x01010101);
    vm_write32(attr + TSA2_MAXCONT, 8);
    fprintf(stderr, "[cellSpurs] TasksetAttribute2Initialize(attr=0x%08X, rev=%u)\n", attr, revision);
    ctx->gpr[3] = 0;
}

static uint32_t s_all_tasksets[32];
static int      s_num_tasksets = 0;
static uint32_t s_q_taskset = 0;
static uint32_t s_last_spurs_taskset = 0;

static void gow_unblock_416_subqueues(uint32_t master_ea);
static void gow_unblock_wws_464_mgr(uint32_t mgr_ea);
static void gow_unblock_spurs_tasks_3a5(void);

static void hle_cellSpursGetWorkloadInfo(ppu_context* ctx)
{
    uint32_t spurs_ea = (uint32_t)ctx->gpr[3];
    uint32_t wid      = (uint32_t)ctx->gpr[4];
    uint32_t info_ea  = (uint32_t)ctx->gpr[5];

    static uint64_t s_count = 0;
    if ((++s_count % 100000) == 1) {
        fprintf(stderr, "[spurs-info] cellSpursGetWorkloadInfo(spurs=0x%08X, wid=%u, info=0x%08X) call #%llu\n",
                spurs_ea, wid, info_ea, (unsigned long long)s_count);
    }

    if (info_ea && vm_base) {
        // Zero the CellSpursWorkloadInfo struct (64 bytes):
        // +0x20: contention/state (expected == 0)
        // +0x23: readyCount/status (expected == 0)
        memset(vm_base + info_ea, 0, 0x40);
    }

#ifdef _WIN32
    if ((s_count & 0x3F) == 0) {
        SwitchToThread();
    }
#endif

    if (ctx->thread_id == 1) {
        gow_unblock_416_subqueues(0x00966FB0u);
        const uint32_t glob_mgr = vm_read32(0x008385ACu);
        if (glob_mgr >= 0x10000u) {
            gow_unblock_wws_464_mgr(glob_mgr);
        }
    }

    ctx->gpr[3] = 0; // CELL_OK
}

static void register_taskset(uint32_t taskset)
{
    if (!taskset) return;
    s_last_spurs_taskset = taskset;
    for (int i = 0; i < s_num_tasksets; i++) {
        if (s_all_tasksets[i] == taskset) return;
    }
    if (s_num_tasksets < 32) {
        s_all_tasksets[s_num_tasksets++] = taskset;
        fprintf(stderr, "[cellSpurs] Registered taskset 0x%08X (total=%d)\n", taskset, s_num_tasksets);
    }
}

extern "C" int32_t cellSpursCreateTasksetWithAttribute(void* spurs, void* taskset, const void* attr);

static void hle_cellSpursCreateTaskset(ppu_context* ctx)
{
    const uint32_t spurs = (uint32_t)ctx->gpr[3], taskset = (uint32_t)ctx->gpr[4];
    register_taskset(taskset);
    const uint64_t args = ctx->gpr[5];
    const void* priority = (const void*)(uintptr_t)ctx->gpr[6];
    const uint32_t maxcont = (uint32_t)ctx->gpr[7];
    int32_t rc = cellSpursCreateTaskset((void*)(uintptr_t)spurs,
                                        (void*)(uintptr_t)taskset,
                                        args, priority, maxcont);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

static void hle_cellSpursCreateTasksetWithAttribute(ppu_context* ctx)
{
    const uint32_t spurs = (uint32_t)ctx->gpr[3], taskset = (uint32_t)ctx->gpr[4];
    register_taskset(taskset);
    const void* attr = (const void*)(uintptr_t)ctx->gpr[5];
    int32_t rc = cellSpursCreateTasksetWithAttribute((void*)(uintptr_t)spurs,
                                                    (void*)(uintptr_t)taskset, attr);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

extern "C" int32_t cellSpursAddWorkloadWithAttribute(void* spurs, void* wid, const void* attr);

static void hle_cellSpursAddWorkloadWithAttribute(ppu_context* ctx)
{
    const uint32_t spurs = (uint32_t)ctx->gpr[3];
    const uint32_t wid   = (uint32_t)ctx->gpr[4];
    const uint32_t attr  = (uint32_t)ctx->gpr[5];

    if (attr && vm_base) {
        uint32_t pm_ea = vm_read32(attr + 0x08); // WKATTR_PM
        if (pm_ea == 0x0077CA80u) {
            uint32_t data_hi = vm_read32(attr + 0x10);
            static bool s_logged_pm = false;
            if (!s_logged_pm) {
                s_logged_pm = true;
                fprintf(stderr, "[cellSpurs] Preserving Wws_Job PM attr=0x%08X (data_hi=0x%08X)\n",
                        attr, data_hi);
            }
        }
    }

    int32_t rc = cellSpursAddWorkloadWithAttribute((void*)(uintptr_t)spurs,
                                                   (void*)(uintptr_t)wid,
                                                   (const void*)(uintptr_t)attr);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

static void hle_cellSpursCreateTask(ppu_context* ctx)
{
    const uint32_t taskset = (uint32_t)ctx->gpr[3];
    register_taskset(taskset);
    const uint32_t arg_ea = (uint32_t)ctx->gpr[9];
    if (arg_ea) s_q_taskset = taskset;
    fprintf(stderr, "[cellSpurs] hle_cellSpursCreateTask(taskset=0x%08X entry=0x%08X arg=0x%08X)\n",
            taskset, (uint32_t)ctx->gpr[5], arg_ea);
    int32_t rc = cellSpursCreateTask((void*)(uintptr_t)taskset,
                                     (void*)(uintptr_t)ctx->gpr[4],
                                     (void*)(uintptr_t)ctx->gpr[5],
                                     (void*)(uintptr_t)ctx->gpr[6],
                                     (uint32_t)ctx->gpr[7],
                                     (uint32_t)ctx->gpr[8],
                                     (uint32_t)ctx->gpr[9]);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

static void cellSpursCreateTaskset2(ppu_context* ctx)
{
    const uint32_t spurs = (uint32_t)ctx->gpr[3], taskset = (uint32_t)ctx->gpr[4];
    const uint32_t attr = (uint32_t)ctx->gpr[5];
    register_taskset(taskset);
    const uint64_t args = attr ? vm_read64(attr + TSA2_ARGS) : 0;
    const uint32_t maxcont = attr ? vm_read32(attr + TSA2_MAXCONT) : 8;

    fprintf(stderr, "[cellSpurs] CreateTaskset2(spurs=0x%08X, taskset=0x%08X, attr=0x%08X) "
                    "args=0x%llX maxContention=%u -> v1 CreateTaskset\n",
            spurs, taskset, attr, (unsigned long long)args, maxcont);

    uint8_t priority[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    if (attr) {
        for (int i = 0; i < 8; ++i) priority[i] = vm_read8(attr + TSA2_PRIORITY + i);
    }
    int32_t rc = cellSpursCreateTaskset((void*)(uintptr_t)spurs,
                                        (void*)(uintptr_t)taskset,
                                        args, priority, maxcont);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

extern "C" int32_t cellSpursDestroyTaskset(void* taskset);
static void cellSpursDestroyTaskset2(ppu_context* ctx)
{
    const uint32_t taskset = (uint32_t)ctx->gpr[3];
    ctx->gpr[3] = (uint64_t)(int64_t)cellSpursDestroyTaskset((void*)(uintptr_t)taskset);
}

static void cellSpursAttributeEnableSystemWorkload(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

static void sys_spu_printf_initialize(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

#define TA2_SIZE 256
static void cellSpursTaskAttribute2Initialize(ppu_context* ctx)
{
    const uint32_t attr = (uint32_t)ctx->gpr[3];
    const uint32_t revision = (uint32_t)ctx->gpr[4];
    if (!attr) { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x80410901; return; }
    for (uint32_t o = 0; o < TA2_SIZE; o += 4) vm_write32(attr + o, 0);
    vm_write32(attr + 0x00, revision);
    ctx->gpr[3] = 0;
}

static void cellSpursCreateTask2(ppu_context* ctx)
{
    const uint32_t taskset = (uint32_t)ctx->gpr[3];
    register_taskset(taskset);
    const uint32_t taskId = (uint32_t)ctx->gpr[4];
    const uint32_t attr = (uint32_t)ctx->gpr[5];

    const uint32_t elf = attr ? vm_read32(attr + 0x08) : 0;
    const uint32_t context = attr ? vm_read32(attr + 0x0C) : 0;
    const uint32_t sizeContext = attr ? vm_read32(attr + 0x10) : 0;
    const uint32_t lsPattern = attr ? vm_read32(attr + 0x14) : 0;
    const uint32_t argument = attr ? vm_read32(attr + 0x18) : 0;

    static uint32_t s_task_log = 0;
    if (++s_task_log <= 10 || (s_task_log % 500 == 0)) {
        fprintf(stderr, "[cellSpurs] cellSpursCreateTask2(taskset=0x%08X elf=0x%08X arg=0x%08X) #%u\n",
                taskset, elf, argument, s_task_log);
    }

    int32_t rc = cellSpursCreateTask((void*)(uintptr_t)taskset,
                                     (void*)(uintptr_t)taskId,
                                     (void*)(uintptr_t)elf,
                                     (void*)(uintptr_t)context,
                                     sizeContext, lsPattern, argument);
    gow_unblock_spurs_tasks_3a5();
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

static void cellSpursTaskGetContextSaveAreaSize(ppu_context* ctx)
{
    const uint32_t pSize = (uint32_t)ctx->gpr[4];
    if (pSize) vm_write32(pSize, 128 * 1024);
    ctx->gpr[3] = 0;
}

static void cellSpursTasksetGetSpursAddress(ppu_context* ctx)
{
    const uint32_t taskset = (uint32_t)ctx->gpr[3];
    const uint32_t pSpurs = (uint32_t)ctx->gpr[4];
    if (taskset && pSpurs) {
        uint32_t spurs = vm_read32(taskset + 0x14);
        vm_write32(pSpurs, spurs);
    }
    ctx->gpr[3] = 0;
}

/* ---------------------------------------------------------------------------
 * SPURS Lock-Free Queue (LFQueue) Handlers
 * ------------------------------------------------------------------------- */
static std::mutex s_lfqueue_mutex;

struct CachedDecompBlock {
    uint32_t src_ea = 0;
    uint32_t srclen = 0;
    std::vector<uint8_t> uncomp;
};
static std::vector<CachedDecompBlock> s_decomp_cache;

static void _cellSpursLFQueuePushBody(ppu_context* ctx)
{
    const uint32_t q = (uint32_t)ctx->gpr[3];
    const uint32_t data = (uint32_t)ctx->gpr[4];
    const uint32_t is_blocking = (uint32_t)ctx->gpr[5];

    if (!q || !data) {
        ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x80410901; /* CELL_SPURS_TASK_ERROR_NULL_POINTER */
        return;
    }

    std::lock_guard<std::mutex> lock(s_lfqueue_mutex);

    const uint32_t depth     = vm_read32(q + 0x0C);
    const uint32_t size      = vm_read32(q + 0x10);
    uint32_t buffer_ea = (uint32_t)vm_read64(q + 0x18);
    if (!buffer_ea) buffer_ea = vm_read32(q + 0x18);
    uint32_t owner_ea  = (uint32_t)vm_read64(q + 0x70);
    if (!owner_ea) owner_ea = vm_read32(q + 0x70);

    if (depth > 0 && size > 0 && buffer_ea) {
        uint16_t push = vm_read16(q + 0x02); /* 16-bit push counter */
        uint32_t slot = (uint32_t)(push & 0x3FFFu) % depth;
        uint32_t slot_ea = buffer_ea + slot * size;
        for (uint32_t i = 0; i < size; i += 4) {
            vm_write32(slot_ea + i, vm_read32(data + i));
        }
        vm_write16(q + 0x02, (uint16_t)(push + 1));
        static uint32_t s_lf_push_count = 0;
        s_lf_push_count++;
        if (s_lf_push_count <= 10 || (s_lf_push_count % 100 == 0)) {
            fprintf(stderr, "[cellSpurs] LFQueuePush(q=0x%08X slot=%u size=%u owner=0x%08X last_taskset=0x%08X n_tasksets=%d #%u)\n",
                    q, slot, size, owner_ea, s_last_spurs_taskset, s_num_tasksets, s_lf_push_count);
            fprintf(stderr, "[cellSpurs] LFQueuePush data: %08X %08X %08X %08X %08X %08X %08X %08X\n",
                    vm_read32(data+0), vm_read32(data+4), vm_read32(data+8), vm_read32(data+12),
                    vm_read32(data+16), vm_read32(data+20), vm_read32(data+24), vm_read32(data+28));
            fprintf(stderr, "[cellSpurs] LFQueue queue state: pop=%u push=%u flag@0x%08X=0x%08X\n",
                    (uint32_t)vm_read16(q+0), (uint32_t)vm_read16(q+2), q - 0x80, vm_read32(q - 0x80));
        }

        const uint32_t src_ea    = vm_read32(data + 0x00);
        const uint32_t dst_ea    = vm_read32(data + 0x04);
        const uint32_t srclen    = vm_read32(data + 0x08);
        const uint32_t dstlen    = vm_read32(data + 0x0C);
        const uint32_t user_ea   = vm_read32(data + 0x10);
        const uint32_t flagea    = vm_read32(data + 0x14);
        const uint16_t flag_bits = vm_read16(data + 0x18);
        const uint16_t skipBegin = vm_read16(data + 0x1A);
        const uint16_t skipEnd   = vm_read16(data + 0x1C);

        if (src_ea && srclen && dst_ea && dstlen && vm_base) {
            uint32_t total_expected = (uint32_t)skipBegin + dstlen + (uint32_t)skipEnd;
            if (total_expected < dstlen) total_expected = dstlen;

            /* Check if we already have this compressed stream inflated in cache */
            CachedDecompBlock* hit = nullptr;
            for (auto& entry : s_decomp_cache) {
                if (entry.src_ea == src_ea && entry.srclen == srclen) {
                    hit = &entry;
                    break;
                }
            }

            if (!hit) {
                uint32_t alloc_size = total_expected > (256 * 1024) ? total_expected : (256 * 1024);
                std::vector<uint8_t> tmp(alloc_size);
                int n = gow_inflate(tmp.data(), alloc_size, vm_base + src_ea, srclen);
                if (n < 0 && (src_ea & 1)) {
                    n = gow_inflate(tmp.data(), alloc_size, vm_base + (src_ea & ~1u), srclen);
                }
                static uint32_t s_cached_inflate_count = 0;
                s_cached_inflate_count++;
                if (s_cached_inflate_count <= 10 || (s_cached_inflate_count % 100 == 0)) {
                    fprintf(stderr, "[edge] cached inflate(src=0x%08X len=%u total_exp=%u #%u) => %d bytes\n",
                            src_ea, srclen, total_expected, s_cached_inflate_count, n);
                }
                if (n > 0) {
                    tmp.resize((size_t)n);
                    if (s_decomp_cache.size() >= 8) {
                        s_decomp_cache.erase(s_decomp_cache.begin());
                    }
                    s_decomp_cache.push_back({src_ea, srclen, std::move(tmp)});
                    hit = &s_decomp_cache.back();
                }
            }

            if (hit && !hit->uncomp.empty()) {
                if ((uint32_t)skipBegin + dstlen <= hit->uncomp.size()) {
                    memcpy(vm_base + dst_ea, hit->uncomp.data() + skipBegin, dstlen);
                } else if ((uint32_t)skipBegin < hit->uncomp.size()) {
                    uint32_t avail = (uint32_t)hit->uncomp.size() - skipBegin;
                    memcpy(vm_base + dst_ea, hit->uncomp.data() + skipBegin, avail);
                    memset(vm_base + dst_ea + avail, 0, dstlen - avail);
                } else {
                    memset(vm_base + dst_ea, 0, dstlen);
                }
            } else {
                memset(vm_base + dst_ea, 0, dstlen);
            }

            /* Consume request from LFQueue (advance 16-bit pop counter) */
            uint16_t pop = vm_read16(q + 0x00);
            vm_write16(q + 0x00, (uint16_t)(pop + 1));

            if (user_ea) {
                uint32_t status_ea = user_ea & ~3u;
                uint32_t prev_val = vm_read32(status_ea);
                static int user_cnt = 0;
                if (user_cnt++ < 20 || (user_cnt % 100) == 0) {
                    fprintf(stderr, "[edge] user_ea=0x%08X status_ea=0x%08X val=0x%08X -> clearing to 0\n",
                            user_ea, status_ea, prev_val);
                }
                vm_write32(status_ea, 0);
                vm_write8(user_ea, 0);
            }

            if (flagea) {
                uint16_t b = flag_bits ? flag_bits : 1;
                spurs_ef_set_from_spu(flagea, b);
                cellSpursEventFlagSet((void*)(uintptr_t)flagea, b);
            }
        }
    }

    /* Wake all registered SPURS tasksets */
    for (int i = 0; i < s_num_tasksets; i++) {
        if (s_all_tasksets[i]) {
            spu_taskset_signal_task(s_all_tasksets[i], 0);
            spu_taskset_signal_parked_obj(s_all_tasksets[i], 0);
        }
    }
    if (s_last_spurs_taskset) {
        spu_taskset_signal_task(s_last_spurs_taskset, 0);
        spu_taskset_signal_parked_obj(s_last_spurs_taskset, 0);
    }
    if (owner_ea) {
        spu_taskset_signal_task(owner_ea & ~1u, 0);
        spu_taskset_signal_parked_obj(owner_ea & ~1u, 0);
    }

    ctx->gpr[3] = 0; /* CELL_OK */
}

extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));

static void hook_func_005A8D6C(ppu_context* ctx)
{
    const uint32_t req = (uint32_t)ctx->gpr[4];
    uint32_t val = req ? vm_read32(req + 0x28) : 0;
    static int cnt = 0;
    if (cnt++ < 20) {
        fprintf(stderr, "[edge_isDone] this=0x%08X req=0x%08X req[+0x28]=0x%08X\n",
                (uint32_t)ctx->gpr[3], req, val);
    }
    func_005A8D6C(ctx);
}


static void hook_func_00466B88(ppu_context* ctx)
{
    const uint32_t r31_ptr = (uint32_t)ctx->gpr[3];
    const uint32_t b = (uint32_t)ctx->gpr[4] & 1;

    if (r31_ptr >= 0x10000u) {
        const uint32_t r5 = vm_read32(r31_ptr);
        if (r5 >= 0x10000u) {
            const uint32_t hdr = r5 + (b << 4) + 0x40u;
            uint16_t target = vm_read16(hdr);
            const uint32_t r11_base = r5 + (b << 2);
            const uint32_t r9_tbl = vm_read32(r11_base + 0x30u);
            uint32_t extra = 0;
            if (r9_tbl >= 0x10000u) {
                uint32_t entry = r9_tbl + ((uint32_t)target << 3);
                if (vm_read16(entry + 2) != 0) {
                    extra = 1;
                    entry += 10;
                    while (vm_read16(entry) != 0 && extra < 512) {
                        extra++;
                        entry += 8;
                    }
                }
            }
            uint16_t completed_val = (uint16_t)(target + extra);
            vm_write16(hdr, completed_val);
            for (int i = 1; i <= 7; ++i) {
                vm_write16(hdr + (uint32_t)i * 2, completed_val);
            }
            vm_write32(r5 + 8, b);
        }
    }

    func_00466B88(ctx);
}

static void gow_unblock_416_subqueues(uint32_t master_ea);

static void hook_func_00466E28(ppu_context* ctx)
{
    if (getenv("GOW_NO_WWS_HACK")) {
        func_00466E28(ctx);
        return;
    }

    const uint32_t mgr_ea = (uint32_t)ctx->gpr[3];
    const uint32_t buf_set = (uint32_t)ctx->gpr[4];

    ppu_gcm_pump();
#ifdef _WIN32
    SwitchToThread();
#endif

    if (mgr_ea >= 0x10000u) {
        const uint32_t r5 = vm_read32(mgr_ea);
        if (r5 >= 0x10000u) {
            for (uint32_t b = 0; b < 8; ++b) {
                const uint32_t hdr = r5 + (b << 4) + 0x40u;
                uint16_t target = vm_read16(hdr);
                const uint32_t r11_base = r5 + (b << 2);
                const uint32_t r9_tbl = vm_read32(r11_base + 0x30u);
                uint32_t extra = 0;
                if (r9_tbl >= 0x10000u) {
                    uint32_t entry = r9_tbl + ((uint32_t)target << 3);
                    if (vm_read16(entry + 2) != 0) {
                        extra = 1;
                        entry += 10;
                        while (vm_read16(entry) != 0 && extra < 512) {
                            extra++;
                            entry += 8;
                        }
                    }
                }
                uint16_t completed_val = (uint16_t)(target + extra);
                vm_write16(hdr, completed_val);
                for (int i = 1; i <= 7; ++i) {
                    vm_write16(hdr + (uint32_t)i * 2, completed_val);
                }
            }
            if (buf_set < 8) {
                vm_write32(r5 + 8, buf_set);
            }
        }
    }

    gow_unblock_416_subqueues(0x00966FB0u);
    {
        const uint32_t glob_mgr = vm_read32(0x008385ACu);
        if (glob_mgr >= 0x10000u) {
            gow_unblock_wws_464_mgr(glob_mgr);
        }
    }
    func_00466E28(ctx);
}

static void hook_func_00375A50(ppu_context* ctx)
{
    ppu_gcm_pump();
#ifdef _WIN32
    SwitchToThread();
#endif

    // Experiment 2: Do NOT fake-overwrite ctrl->ref (ctrl_ea + 0x08).
    // Let real RSX FIFO drain (SET_REFERENCE / gcm_ref_publish_one) advance ctrl->ref naturally!

    gow_unblock_416_subqueues(0x00966FB0u);
    {
        const uint32_t glob_mgr = vm_read32(0x008385ACu);
        if (glob_mgr >= 0x10000u) {
            gow_unblock_wws_464_mgr(glob_mgr);
        }
    }
    func_00375A50(ctx);
}

static void hook_func_003E798C(ppu_context* ctx)
{
    ppu_gcm_pump();
#ifdef _WIN32
    SwitchToThread();
#endif

    for (uint32_t slot = 0; slot < 3; ++slot) {
        vm_write16(0x0099AAC0u + slot * 4, 0);
        vm_write16(0x0099AAB0u + slot * 4, 0);
    }
    for (uint32_t i = 0; i < 8; ++i) {
        vm_write16(0x0099AA80u + i * 4, 0);
        vm_write32(0x0099B460u + i * 4, 0);
    }

    func_003E798C(ctx);
}

static void hook_func_00466398(ppu_context* ctx)
{
    const uint32_t mgr_ea = (uint32_t)ctx->gpr[3];
    const uint32_t buf_set = (uint32_t)ctx->gpr[4];
    const uint16_t target = (uint16_t)ctx->gpr[5];

    ppu_gcm_pump();
#ifdef _WIN32
    SwitchToThread();
#endif

    if (mgr_ea >= 0x10000u) {
        const uint32_t r5 = vm_read32(mgr_ea);
        if (r5 >= 0x10000u && buf_set < 8) {
            const uint32_t hdr = r5 + (buf_set << 4) + 0x40u;
            for (int i = 1; i <= 7; ++i) {
                uint16_t cur = vm_read16(hdr + (uint32_t)i * 2);
                if (cur < target) {
                    vm_write16(hdr + (uint32_t)i * 2, target);
                }
            }
        }
    }

    func_00466398(ctx);

    static uint32_t s_wws_sync_cnt = 0;
    if (++s_wws_sync_cnt <= 10 || (s_wws_sync_cnt % 50 == 0)) {
        fprintf(stderr, "[WWS-SYNC-UNBLOCK #%u] mgr=0x%08X buf_set=%u target=%u -> ret=%u\n",
                s_wws_sync_cnt, mgr_ea, buf_set, target, (uint32_t)ctx->gpr[3]);
    }
}

extern "C" int32_t cellSpursReadyCountStore(void* spurs, uint32_t wid, uint32_t value);

static void gow_unblock_wws_464_mgr(uint32_t mgr_ea)
{
    if (mgr_ea < 0x10000u || mgr_ea >= 0x20000000u) return;
    const uint32_t r5 = vm_read32(mgr_ea + 0x00);
    const uint32_t batch_sz = vm_read32(mgr_ea + 0xC8);
    const uint32_t target_cnt = vm_read32(mgr_ea + 0xCC);
    const uint32_t comp_cnt = vm_read32(mgr_ea + 0xD0);
    if (r5 >= 0x10000u && r5 < 0x20000000u) {
        for (uint32_t b = 0; b < 8; ++b) {
            const uint32_t hdr = r5 + (b << 4) + 0x40u;
            uint16_t target = vm_read16(hdr);
            uint16_t min_needed = (batch_sz > 0 && batch_sz < 0xFFFEu) ? (uint16_t)(batch_sz + 1) : target;
            if (target < min_needed) target = min_needed;
            for (int i = 1; i <= 7; ++i) {
                if (vm_read16(hdr + (uint32_t)i * 2) < target) {
                    vm_write16(hdr + (uint32_t)i * 2, target);
                }
            }
        }
    }
    if (comp_cnt < target_cnt && target_cnt < 0x10000000u) {
        static uint32_t s_wws_464_cnt = 0;
        if (++s_wws_464_cnt <= 20 || (s_wws_464_cnt % 50 == 0)) {
            fprintf(stderr, "[WWS-464-UNBLOCK #%u] mgr=0x%08X r5=0x%08X comp=%u -> target=%u batch=%u\n",
                    s_wws_464_cnt, mgr_ea, r5, comp_cnt, target_cnt, batch_sz);
        }
        vm_write32(mgr_ea + 0xD0, target_cnt);
        if (batch_sz > 0) {
            vm_write32(mgr_ea + 0xD8, target_cnt / batch_sz);
        }
    }
}

static void gow_unblock_wws_mgr(uint32_t mgr_ea)
{
    if (getenv("GOW_NO_WWS_HACK")) return;
    if (mgr_ea < 0x10000u) return;
    const uint32_t r5 = vm_read32(mgr_ea);
    if (r5 < 0x10000u) return;

    for (uint32_t b = 0; b < 8; ++b) {
        const uint32_t hdr = r5 + (b << 4) + 0x40u;
        uint16_t target = vm_read16(hdr);
        const uint32_t r11_base = r5 + (b << 2);
        const uint32_t r9_tbl = vm_read32(r11_base + 0x30u);
        uint32_t extra = 0;
        if (r9_tbl >= 0x10000u) {
            uint32_t entry = r9_tbl + ((uint32_t)target << 3);
            if (vm_read16(entry + 2) != 0) {
                extra = 1;
                entry += 10;
                while (vm_read16(entry) != 0 && extra < 512) {
                    extra++;
                    entry += 8;
                }
            }
        }
        uint16_t completed_val = (uint16_t)(target + extra);
        vm_write16(hdr, completed_val);
        for (int k = 1; k <= 7; ++k) {
            vm_write16(hdr + (uint32_t)k * 2, completed_val);
        }
    }
    vm_write32(r5 + 8, 3);
    gow_unblock_wws_464_mgr(mgr_ea);
}

static void gow_unblock_416_subqueues(uint32_t master_ea)
{
    if (!master_ea) master_ea = 0x00966FB0u;
    const uint16_t num_q = vm_read16(master_ea + 0x0A);
    const uint32_t q_base = vm_read32(master_ea + 0x20);

    if (q_base >= 0x10000u && num_q > 0 && num_q <= 32) {
        for (uint32_t i = 0; i < num_q; ++i) {
            uint32_t q = q_base + i * 0x70u;
            uint32_t idx = vm_read32(q + 0x08);
            uint32_t tbl = vm_read32(q + 0x18);
            if (tbl >= 0x10000u && idx < 0x10000u) {
                for (uint32_t k = 0; k < 16; ++k) {
                    uint32_t entry = tbl + ((idx + k) << 3);
                    uint16_t etype = vm_read16(entry + 0x02);
                    if (etype == 2) {
                        uint32_t dep_ea = vm_read32(entry + 0x04);
                        if (dep_ea >= 0x10000u && vm_read16(dep_ea) != 0) {
                            static uint32_t s_dep_dec_cnt = 0;
                            if (++s_dep_dec_cnt <= 20 || (s_dep_dec_cnt % 100 == 0)) {
                                fprintf(stderr, "[SUBQ-DEP-UNBLOCK #%u] q[%u]=0x%08X idx=%u dep_ea=0x%08X was %u -> 0\n",
                                        s_dep_dec_cnt, i, q, idx + k, dep_ea, vm_read16(dep_ea));
                            }
                            vm_write16(dep_ea, 0);
                        }
                    } else if (etype == 0) {
                        break;
                    }
                }
            }
            uint32_t q_mgr = vm_read32(q + 0x2Cu);
            if (q_mgr >= 0x10000u) {
                gow_unblock_wws_mgr(q_mgr);
                gow_unblock_wws_464_mgr(q_mgr);
            }
        }
    }
    const uint32_t glob_mgr = vm_read32(0x008385ACu);
    if (glob_mgr >= 0x10000u) {
        gow_unblock_wws_464_mgr(glob_mgr);
    }
}

static void hook_func_00416328(ppu_context* ctx)
{
    gow_unblock_416_subqueues((uint32_t)ctx->gpr[3]);
    func_00416328(ctx);
}

static void hook_func_00416600(ppu_context* ctx)
{
    gow_unblock_416_subqueues((uint32_t)ctx->gpr[3]);
    func_00416600(ctx);
}

static void hook_func_00416A00(ppu_context* ctx)
{
    gow_unblock_416_subqueues((uint32_t)ctx->gpr[3]);
    func_00416A00(ctx);
}

static void hook_func_00464AFC(ppu_context* ctx)
{
    gow_unblock_wws_464_mgr((uint32_t)ctx->gpr[3]);
    func_00464AFC(ctx);
}

static void hook_func_00464A9C(ppu_context* ctx)
{
    gow_unblock_wws_464_mgr((uint32_t)ctx->gpr[3]);
    func_00464A9C(ctx);
}

static void hook_func_0046630C(ppu_context* ctx)
{
    gow_unblock_wws_464_mgr((uint32_t)ctx->gpr[3]);
    func_0046630C(ctx);
}

static void hook_func_00464F0C(ppu_context* ctx)
{
    gow_unblock_wws_464_mgr((uint32_t)ctx->gpr[3]);
    func_00464F0C(ctx);
}

static void gow_unblock_spurs_tasks_3a5(void)
{
    const uint32_t target = vm_read32(0x0098317Cu);
    const uint32_t cur = vm_read32(0x00983168u);
    if (target > 0 && cur != target) {
        static uint32_t s_spurs_task_sync_cnt = 0;
        if (++s_spurs_task_sync_cnt <= 20 || (s_spurs_task_sync_cnt % 100 == 0)) {
            fprintf(stderr, "[SPURS-TASK-SYNC-UNBLOCK #%u] completed %u -> target %u\n",
                    s_spurs_task_sync_cnt, cur, target);
        }
        vm_write32(0x00983168u, target);
    }
}

static void hook_func_003A5A94(ppu_context* ctx)
{
    gow_unblock_spurs_tasks_3a5();
    func_003A5A94(ctx);
}

static void hook_func_003A5D00(ppu_context* ctx)
{
    gow_unblock_spurs_tasks_3a5();
    func_003A5D00(ctx);
}

static void hle_cellSpursReadyCountStore(ppu_context* ctx)
{
    void* spurs = (void*)(uintptr_t)ctx->gpr[3];
    uint32_t wid = (uint32_t)ctx->gpr[4];
    uint32_t val = (uint32_t)ctx->gpr[5];
    ctx->gpr[3] = cellSpursReadyCountStore(spurs, wid, val);
    if (ctx->thread_id == 1) {
        gow_unblock_416_subqueues(0x00966FB0u);
        const uint32_t glob_mgr = vm_read32(0x008385ACu);
        if (glob_mgr >= 0x10000u) {
            gow_unblock_wws_464_mgr(glob_mgr);
        }
    }
}

static void hook_func_003636F8(ppu_context* ctx)
{
    static uint32_t s_disp_cnt = 0;
    uint32_t id = ++s_disp_cnt;
    const bool log = (id <= 10 || (id % 50 == 0));
    if (log) {
        uint32_t a0 = (uint32_t)ctx->gpr[3];
        uint32_t a1 = (uint32_t)ctx->gpr[4];
        uint32_t a2 = (uint32_t)ctx->gpr[5];
        uint32_t w0 = a2 ? vm_read32(a2) : 0;
        uint32_t type = (w0 >> 16) & 0xFFF;
        uint16_t sub_count = a2 ? vm_read16(a2 + 0x0C) : 0;
        uint32_t w1 = a2 ? vm_read32(a2 + 4) : 0;
        uint32_t w2 = a2 ? vm_read32(a2 + 8) : 0;
        uint32_t w4 = a2 ? vm_read32(a2 + 16) : 0;

        uint32_t cap = a0 ? vm_read32(a0 + 0x68) : 0;
        uint16_t used = a0 ? vm_read16(a0 + 0x5C) : 0;
        uint32_t tbl = a0 ? vm_read32(a0 + 0x74) : 0;
        uint16_t max_probe = a0 ? vm_read16(a0 + 0x5E) : 0;
        uint32_t total_probes = a0 ? vm_read32(a0 + 0x64) : 0;

        uint32_t heap_r3 = vm_read32(0x00981698u);
        uint32_t heap_r5 = vm_read32(0x0098169Cu);
        uint32_t heap_arr = vm_read32(0x00981694u);
        uint32_t cur_heap = (heap_arr && heap_r3 != heap_r5) ? vm_read32(heap_arr + (heap_r3 << 2)) : 0;

        fprintf(stderr, "\n>>> [RESOURCE-DISPATCH #%u START] a0=0x%08X (used=%u cap=%u tbl=0x%08X max_probe=%u total_probes=%u) a1=0x%08X a2=0x%08X (w0=%08X w1=%08X w2=%08X type=%u sub=%u w4=%08X) [heap: arr=0x%08X cur=0x%08X] <<<\n",
                id, a0, used, cap, tbl, max_probe, total_probes, a1, a2, w0, w1, w2, type, sub_count, w4, heap_arr, cur_heap);

        if (type == 1 && a2) {
            uint32_t r30 = a2 + w4;
            uint32_t ent_count = vm_read32(r30 + 0x18);
            uint32_t ent_tbl_off = vm_read32(r30 + 0x1C);
            uint32_t tbl_ea = r30 + ent_tbl_off;
            fprintf(stderr, "    [RESOURCE-INFO] r30=0x%08X sub-entities=%u ent_tbl_off=0x%08X (tbl_ea=0x%08X)\n",
                    r30, ent_count, ent_tbl_off, tbl_ea);
        }
    }

    func_003636F8(ctx);
    if (log) {
        uint32_t a0 = (uint32_t)ctx->gpr[3];
        fprintf(stderr, ">>> [RESOURCE-DISPATCH #%u END] ret=0x%08X (used=%u cap=%u) <<<\n\n",
                id, (uint32_t)ctx->gpr[3], a0 ? vm_read16(a0 + 0x5C) : 0, a0 ? vm_read32(a0 + 0x68) : 0);
    }
}

static void hook_func_0036298C(ppu_context* ctx)
{
    static uint32_t s_wad_cnt = 0;
    s_wad_cnt++;
    const bool log = (s_wad_cnt <= 10 || (s_wad_cnt % 50 == 0));
    if (log) {
        uint32_t a0 = (uint32_t)ctx->gpr[3];
        uint32_t a1 = (uint32_t)ctx->gpr[4];
        uint32_t a2 = (uint32_t)ctx->gpr[5];
        uint16_t count = a2 ? vm_read16(a2 + 0x0C) : 0;
        fprintf(stderr, "\n>>> [WAD-PARSE #%u START] r3=0x%08X r4=0x%08X r5=0x%08X (sub-entities=%u) <<<\n",
                s_wad_cnt, a0, a1, a2, count);
    }
    func_0036298C(ctx);
    if (log) {
        fprintf(stderr, ">>> [WAD-PARSE #%u END] completed: ret=0x%08X <<<\n\n",
                s_wad_cnt, (uint32_t)ctx->gpr[3]);
    }
}

static void hook_func_003633A4(ppu_context* ctx)
{
    static uint32_t s_chunk_cnt = 0;
    s_chunk_cnt++;
    uint32_t a0 = (uint32_t)ctx->gpr[3];
    uint32_t a1 = (uint32_t)ctx->gpr[4];
    uint32_t a2 = (uint32_t)ctx->gpr[5];
    if (s_chunk_cnt <= 20 || (s_chunk_cnt % 50) == 0) {
        fprintf(stderr, ">>> [CHUNK-PARSE #%u] r3=0x%08X r4=0x%08X r5=0x%08X <<<\n",
                s_chunk_cnt, a0, a1, a2);
    }
    func_003633A4(ctx);
}


static void hook_func_003C8F54(ppu_context* ctx)
{
    static uint32_t s_pkg_cnt = 0;
    uint32_t hdr = (uint32_t)ctx->gpr[3];
    uint32_t dst = (uint32_t)ctx->gpr[4];
    uint16_t type = hdr ? vm_read16(hdr) : 0;
    uint16_t flags = hdr ? vm_read16(hdr + 2) : 0;
    uint32_t sz = hdr ? vm_read32(hdr + 4) : 0;
    char name[33] = {0};
    if (hdr) {
        for (int i = 0; i < 32; i++) {
            char c = (char)vm_read8(hdr + 8 + i);
            name[i] = (c >= 32 && c < 127) ? c : '.';
        }
    }
    fprintf(stderr, "\n>>> [PKG #%u] hdr=0x%08X type=0x%04X flags=0x%04X sz=%u dst=0x%08X raw=[%08X %08X %08X %08X] name='%s' <<<\n",
            ++s_pkg_cnt, hdr, type, flags, sz, dst,
            hdr ? vm_read32(hdr) : 0, hdr ? vm_read32(hdr + 4) : 0,
            hdr ? vm_read32(hdr + 8) : 0, hdr ? vm_read32(hdr + 12) : 0, name);
    fflush(stderr);
    func_003C8F54(ctx);
    fprintf(stderr, ">>> [PKG #%u END] ret=%llu <<<\n\n", s_pkg_cnt, (unsigned long long)ctx->gpr[3]);
    fflush(stderr);
}

static void cellSpursLFQueueDetachLv2EventQueue(ppu_context* ctx)
{
    ctx->gpr[3] = 0; /* CELL_OK */
}

static void cellSpursLFQueuePopBody(ppu_context* ctx)
{
    const uint32_t q = (uint32_t)ctx->gpr[3];
    const uint32_t data = (uint32_t)ctx->gpr[4];
    if (q && data) {
        std::lock_guard<std::mutex> lock(s_lfqueue_mutex);
        const uint32_t depth     = vm_read32(q + 0x0C);
        const uint32_t size      = vm_read32(q + 0x10);
        uint32_t buffer_ea = (uint32_t)vm_read64(q + 0x18);
        if (!buffer_ea) buffer_ea = vm_read32(q + 0x18);
        if (depth > 0 && size > 0 && buffer_ea) {
            uint16_t pop = vm_read16(q + 0x00); /* 16-bit pop counter */
            uint32_t slot = (uint32_t)(pop & 0x3FFFu) % depth;
            uint32_t slot_ea = buffer_ea + slot * size;
            for (uint32_t i = 0; i < size; i += 4) {
                vm_write32(data + i, vm_read32(slot_ea + i));
            }
            vm_write16(q + 0x00, (uint16_t)(pop + 1));
        }
    }
    ctx->gpr[3] = 0; /* CELL_OK */
}

static void _cellSpursWorkloadFlagReceiver(ppu_context* ctx)
{
    ctx->gpr[3] = 0; /* CELL_OK */
}

static void hle_cellSpursLFQueueInitialize(ppu_context* ctx)
{
    ctx->gpr[3] = (uint64_t)(int64_t)_cellSpursLFQueueInitialize(
        ctx->gpr[3], ctx->gpr[4], ctx->gpr[5],
        (uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[7], (uint32_t)ctx->gpr[8]);
}

static void hle_cellSpursLFQueueAttachLv2EventQueue(ppu_context* ctx)
{
    ctx->gpr[3] = (uint64_t)(int64_t)cellSpursLFQueueAttachLv2EventQueue(ctx->gpr[3]);
}

/* ---------------------------------------------------------------------------
 * cellGcm, Audio/Video and System Helpers
 * ------------------------------------------------------------------------- */
extern "C" int32_t cellGcmSetFlipCommandWithWaitLabel(uint32_t bufferId, uint32_t labelIndex, uint32_t labelValue);
extern "C" int32_t cellGcmSetFlipCommand(uint32_t bufferId);

static void hle_cellGcmGetLabelAddress(ppu_context* ctx)
{
    const uint8_t index = (uint8_t)ctx->gpr[3];
    const uint32_t label_ea = 0x20000000u + (uint32_t)index * 0x10u;
    {
        static uint8_t s_tracked[256] = {0};
        if (!s_tracked[index]) {
            s_tracked[index] = 1;
            fprintf(stderr, "[cellGcm] GetLabelAddress(index=%u) -> ea=0x%08X (val=0x%08X)\n",
                    index, label_ea, vm_read32(label_ea));
        }
    }
    ctx->gpr[3] = label_ea;
}

extern "C" uint32_t cellGcmGetFlipStatus(void);
extern "C" void cellGcmTickFlip(void);

static void hle_cellGcmGetFlipStatus(ppu_context* ctx)
{
    ppu_gcm_pump();
    const uint32_t st = cellGcmGetFlipStatus();
    ctx->gpr[3] = (uint64_t)st;
#ifdef _WIN32
    if (st != 0) {
        SwitchToThread();
    }
#endif
}

static uint64_t s_flip_cmd_count_by_tid[64] = {0};

extern "C" void gow_print_flip_reconciliation(void)
{
    fprintf(stderr, "\n=== [FLIP-RECONCILIATION] Total _cellGcmSetFlipCommand Calls Per Thread ===\n");
    bool any = false;
    for (uint32_t t = 0; t < 64; t++) {
        if (s_flip_cmd_count_by_tid[t] > 0) {
            fprintf(stderr, "  tid=%-2u: %llu flip calls\n", t, (unsigned long long)s_flip_cmd_count_by_tid[t]);
            any = true;
        }
    }
    if (!any) {
        fprintf(stderr, "  (no _cellGcmSetFlipCommand calls recorded yet)\n");
    }
    fprintf(stderr, "========================================================================\n\n");
    fflush(stderr);
}

static void hle_cellGcmSetFlipCommandWithWaitLabel(ppu_context* ctx)
{
    uint32_t tid = ctx ? (uint32_t)ctx->thread_id : 0;
    if (tid < 64) s_flip_cmd_count_by_tid[tid]++;

    uint32_t bufferId = (uint32_t)ctx->gpr[4];
    if (bufferId >= 8 && (uint32_t)ctx->gpr[3] < 8) {
        bufferId = (uint32_t)ctx->gpr[3];
    }
    const uint32_t labelIndex = (uint32_t)ctx->gpr[5];
    const uint32_t labelValue = (uint32_t)ctx->gpr[6];
    fprintf(stderr, "[GCM-DIAG] _cellGcmSetFlipCommandWithWaitLabel(tid=%u, buf=%u, label=%u, val=%u)\n",
            tid, bufferId, labelIndex, labelValue);
    ctx->gpr[3] = (uint64_t)(int64_t)cellGcmSetFlipCommandWithWaitLabel(bufferId, labelIndex, labelValue);
}

static void hle_cellGcmSetFlipCommand(ppu_context* ctx)
{
    uint32_t tid = ctx ? (uint32_t)ctx->thread_id : 0;
    if (tid < 64) s_flip_cmd_count_by_tid[tid]++;

    uint32_t bufferId = (uint32_t)ctx->gpr[4];
    if (bufferId >= 8 && (uint32_t)ctx->gpr[3] < 8) {
        bufferId = (uint32_t)ctx->gpr[3];
    }
    fprintf(stderr, "[GCM-DIAG] _cellGcmSetFlipCommand(tid=%u, buf=%u)\n", tid, bufferId);
    ctx->gpr[3] = (uint64_t)(int64_t)cellGcmSetFlipCommand(bufferId);
}

static void _cellGcmFunc15(ppu_context* ctx)
{
    ctx->gpr[3] = 0; /* CELL_OK */
}

extern "C" void cellGcmSetVBlankHandler(void (*handler)(uint32_t));

static void gow_cellGcmSetVBlankHandler(ppu_context* ctx)
{
    uint32_t handler = (uint32_t)ctx->gpr[3];
    fprintf(stderr, "\n>>> [GOW-GCM] cellGcmSetVBlankHandler(opd=0x%08X) called by tid=%u <<<\n",
            handler, (uint32_t)ctx->thread_id);
    cellGcmSetVBlankHandler((void (*)(uint32_t))(uintptr_t)handler);

    // CRITICAL ENGINE UNLOCK:
    // Santa Monica's GCM display list queue (0x00376788 & 0x00376DE8) gates frame submission
    // on *(0x00970F30) (flipInterval / renderEnable). Setting it to 1 enables 60 fps display list dispatch!
    vm_write32(0x00970F30u, 1);
    vm_write32(0x0081C9B4u, 0);
    fprintf(stderr, ">>> [GOW-GCM] Unlocked 3D frame rendering: *(0x00970F30)=1, *(0x0081C9B4)=0 <<<\n\n");

    ctx->gpr[3] = CELL_OK;
}

static void cellVideoOutSetGamma_nid(ppu_context* ctx)
{
    ctx->gpr[3] = 0; /* CELL_OK */
}

static void cellVideoOutGetGamma_nid(ppu_context* ctx)
{
    uint32_t pGamma = (uint32_t)ctx->gpr[4];
    if (pGamma) {
        float g = 1.0f;
        uint32_t bits;
        memcpy(&bits, &g, 4);
        vm_write32(pGamma, bits);
    }
    ctx->gpr[3] = 0; /* CELL_OK */
}

static void cellSaveDataFixedDelete_nid(ppu_context* ctx)
{
    ctx->gpr[3] = 0; /* CELL_OK */
}

static void sys_process_is_stack_nid(ppu_context* ctx)
{
    const uint32_t addr = (uint32_t)ctx->gpr[3];
    int is_stack = 0;
    /* Main thread stack range: 0x0E000000..0x0FFFFFFF (stack top 0x0FF00000)
     * Worker thread stacks allocated from VM_STACK_BASE: 0xD0000000..0xDFFFFFFF */
    if ((addr >= 0x0E000000u && addr < 0x10000000u) ||
        (addr >= 0xD0000000u && addr < 0xE0000000u)) {
        is_stack = 1;
    }
    static int s_logged = 0;
    if (s_logged < 10) {
        s_logged++;
        fprintf(stderr, "[sysprx] sys_process_is_stack(ea=0x%08X) -> %d\n", addr, is_stack);
    }
    ctx->gpr[3] = (uint64_t)is_stack;
}

static void sys_time_get_system_time_nid(ppu_context* ctx)
{
    LARGE_INTEGER freq, now;
    static int init = 0;
    if (!init) { QueryPerformanceFrequency(&freq); init = 1; }
    QueryPerformanceCounter(&now);
    uint64_t us = (uint64_t)((double)now.QuadPart * 1000000.0 / (double)freq.QuadPart);
    ctx->gpr[3] = us;
}

static void sys_net_ok_stub(ppu_context* ctx)
{
    ctx->gpr[3] = 0; /* CELL_OK */
}

static void cellFsSdataOpen_stub(ppu_context* ctx)
{
    ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x80010006; /* CELL_ENOENT */
}

static void cellFs_ok_stub(ppu_context* ctx)
{
    ctx->gpr[3] = 0; /* CELL_OK */
}

/* ---------------------------------------------------------------------------
 * Display and video configuration
 * ------------------------------------------------------------------------- */
static void cellVideoOutGetDeviceInfo(ppu_context* ctx)
{
    const uint32_t videoOut = (uint32_t)ctx->gpr[3];
    const uint32_t deviceIndex = (uint32_t)ctx->gpr[4];
    const uint32_t info_ea = (uint32_t)ctx->gpr[5];

    if (!info_ea) { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x8002B101; return; }

    vm_write8(info_ea + 0x00, 1);     /* state: enabled */
    vm_write8(info_ea + 0x01, 1);     /* color space: RGB */
    vm_write8(info_ea + 0x02, 0);
    vm_write8(info_ea + 0x03, 0);     /* reserved */
    vm_write32(info_ea + 0x04, 0);    /* latency */
    vm_write32(info_ea + 0x08, 2);    /* available modes count */

    /* Mode 0: 720p 60Hz */
    vm_write8(info_ea + 0x0C + 0x00, 2); /* resolutionId: 720p */
    vm_write8(info_ea + 0x0C + 0x01, 0); /* scanMode: progressive */
    vm_write8(info_ea + 0x0C + 0x02, 0); /* format */
    vm_write8(info_ea + 0x0C + 0x03, 1); /* aspect: 16:9 */
    vm_write32(info_ea + 0x0C + 0x04, 0); /* reserved */
    vm_write32(info_ea + 0x0C + 0x08, 60); /* refreshRate: 60Hz */

    /* Mode 1: 1080p 60Hz */
    vm_write8(info_ea + 0x1C + 0x00, 1); /* resolutionId: 1080p */
    vm_write8(info_ea + 0x1C + 0x01, 0); /* progressive */
    vm_write8(info_ea + 0x1C + 0x02, 0);
    vm_write8(info_ea + 0x1C + 0x03, 1);
    vm_write32(info_ea + 0x1C + 0x04, 0);
    vm_write32(info_ea + 0x1C + 0x08, 60);

    ctx->gpr[3] = 0;   /* CELL_OK */
}

/* ---------------------------------------------------------------------------
 * Filesystem offset I/O helpers
 * ------------------------------------------------------------------------- */
static void cellFsReadWithOffset(ppu_context* ctx)
{
    int32_t   fd      = (int32_t)ctx->gpr[3];
    int64_t   offset  = (int64_t)ctx->gpr[4];
    void*     buf     = (void*)(uintptr_t)ctx->gpr[5];
    uint64_t  nbytes  = ctx->gpr[6];
    uint64_t* pRead   = (uint64_t*)(uintptr_t)ctx->gpr[7];

    uint64_t pos = 0;
    cellFsLseek(fd, offset, 0 /* SEEK_SET */, &pos);
    int32_t rc = cellFsRead(fd, buf, nbytes, pRead);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

static void cellFsWriteWithOffset(ppu_context* ctx)
{
    int32_t     fd       = (int32_t)ctx->gpr[3];
    int64_t     offset   = (int64_t)ctx->gpr[4];
    const void* buf      = (const void*)(uintptr_t)ctx->gpr[5];
    uint64_t    nbytes   = ctx->gpr[6];
    uint64_t*   pWritten = (uint64_t*)(uintptr_t)ctx->gpr[7];

    uint64_t pos = 0;
    cellFsLseek(fd, offset, 0 /* SEEK_SET */, &pos);
    int32_t rc = cellFsWrite(fd, buf, nbytes, pWritten);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

/* ---------------------------------------------------------------------------
 * Video decoder entry points
 * ------------------------------------------------------------------------- */
static void cellVdecOpenEx(ppu_context* ctx)
{
    uint32_t pHandle = (uint32_t)ctx->gpr[3];
    if (pHandle) vm_write32(pHandle, 1);
    ctx->gpr[3] = 0;
}

static void cellVdecQueryAttrEx(ppu_context* ctx)
{
    ctx->gpr[3] = 0;
}

/* ---------------------------------------------------------------------------
 * Registration of all Ascension single-player HLE handlers
 * ------------------------------------------------------------------------- */
extern "C" void ascension_hle_register(void)
{
    /* sysPrxForUser */
    ps3_hle_register_ctx(0xA3E3BE68u, "sys_ppu_thread_once",          sys_ppu_thread_once);
    ps3_hle_register_ctx(0x42B23552u, "sys_prx_register_library",     sys_prx_register_library);

    /* sys_lwmutex & sys_lwcond with real-time telemetry */
    ps3_hle_register_ctx(0x1573DC3Fu, "sys_lwmutex_lock",             gow_sys_lwmutex_lock);
    ps3_hle_register_ctx(0x1BC200F4u, "sys_lwmutex_unlock",           gow_sys_lwmutex_unlock);
    ps3_hle_register_ctx(0xAEB78725u, "sys_lwmutex_trylock",          gow_sys_lwmutex_trylock);
    ps3_hle_register_ctx(0x2F85C0EFu, "sys_lwmutex_create",           gow_sys_lwmutex_create);
    ps3_hle_register_ctx(0xC3476D0Cu, "sys_lwmutex_destroy",          gow_sys_lwmutex_destroy);
    ps3_hle_register_ctx(0xC8F6F985u, "sys_lwmutex_reset_all",        gow_sys_lwmutex_reset_all);

    ps3_hle_register_ctx(0xDA0EB71Au, "sys_lwcond_create",            gow_sys_lwcond_create);
    ps3_hle_register_ctx(0x1C9A942Cu, "sys_lwcond_destroy",           gow_sys_lwcond_destroy);
    ps3_hle_register_ctx(0x2A6D9D51u, "sys_lwcond_wait",              gow_sys_lwcond_wait);
    ps3_hle_register_ctx(0xEF87A695u, "sys_lwcond_signal",            gow_sys_lwcond_signal);
    ps3_hle_register_ctx(0xE9A1BD84u, "sys_lwcond_signal_all",        gow_sys_lwcond_signal_all);
    ps3_hle_register_ctx(0x52AADADFu, "sys_lwcond_signal_to",         gow_sys_lwcond_signal_to);

    /* cellGcmSys */
    ps3_hle_register_ctx(0x626E8518u, "cellGcmMapEaIoAddressWithFlags", cellGcmMapEaIoAddressWithFlags);

    /* cellSpurs v2 & taskset helpers */
    ps3_hle_register_ctx(0x30AA96C4u, "cellSpursInitializeWithAttribute2", cellSpursInitializeWithAttribute2);
    ps3_hle_register_ctx(0xC2ACDF43u, "_cellSpursTasksetAttribute2Initialize", cellSpursTasksetAttribute2Initialize);
    ps3_hle_register_ctx(0x4A6465E3u, "cellSpursCreateTaskset2",      cellSpursCreateTaskset2);
    ps3_hle_register_ctx(0x1EBCF459u, "cellSpursDestroyTaskset2",     cellSpursDestroyTaskset2);
    ps3_hle_register_ctx(0x9DCBCB5Du, "cellSpursAttributeEnableSystemWorkload", cellSpursAttributeEnableSystemWorkload);
    ps3_hle_register_ctx(0x45FE2FCEu, "_sys_spu_printf_initialize",   sys_spu_printf_initialize);
    ps3_hle_register_ctx(0x8ADADF65u, "_cellSpursTaskAttribute2Initialize", cellSpursTaskAttribute2Initialize);
    ps3_hle_register_ctx(0xE14CA62Du, "cellSpursCreateTask2",          cellSpursCreateTask2);
    ps3_hle_register_ctx(0x9034E538u, "cellSpursTaskGetContextSaveAreaSize", cellSpursTaskGetContextSaveAreaSize);
    ps3_hle_register_ctx(0x58D58FCFu, "cellSpursTasksetGetSpursAddress", cellSpursTasksetGetSpursAddress);
    ps3_hle_register_ctx(0xA7A94892u, "cellSpursJoinTask2",           cellSpursJoinTask2);
    ps3_hle_register_ctx(0x838FA4F0u, "cellSpursTryJoinTask2",        cellSpursTryJoinTask2);
    ps3_hle_register_ctx(0xC765B995u, "cellSpursGetWorkloadFlag",     cellSpursGetWorkloadFlag);

    /* cellGame & cellSysmodule */
    ps3_hle_register_ctx(0xCE4374F6u, "cellGamePatchCheck",            cellGamePatchCheck);
    ps3_hle_register_ctx(0xA193143Cu, "cellSysmoduleSetMemcontainer",  cellSysmoduleSetMemcontainer);

    /* sysPrx module loaders */
    ps3_hle_register_ctx(0x26090058u, "sys_prx_load_module",           sys_prx_load_module);
    ps3_hle_register_ctx(0xEF68C17Cu, "sys_prx_load_module_by_fd",     sys_prx_load_module);
    ps3_hle_register_ctx(0xAA6D9BFFu, "sys_prx_load_module_on_memcontainer", sys_prx_load_module);
    ps3_hle_register_ctx(0xA330AD84u, "sys_prx_load_module_on_memcontainer_by_fd", sys_prx_load_module);
    ps3_hle_register_ctx(0x67F9FEDBu, "sys_game_process_exitspawn2",   sys_game_process_exitspawn2);

    /* cellVideoOut */
    ps3_hle_register_ctx(0x1E930EEFu, "cellVideoOutGetDeviceInfo",     cellVideoOutGetDeviceInfo);

    /* sys_fs offset I/O */
    ps3_hle_register_ctx(0x0D5B4A14u, "cellFsReadWithOffset",         cellFsReadWithOffset);
    ps3_hle_register_ctx(0x8CB722D5u, "cellFsWriteWithOffset",        cellFsWriteWithOffset);

    /* libvdec */
    ps3_hle_register_ctx(0x0053E2D8u, "cellVdecOpenEx",               cellVdecOpenEx);
    ps3_hle_register_ctx(0xC982A84Au, "cellVdecQueryAttrEx",          cellVdecQueryAttrEx);

    /* cellSpurs LFQueue, Taskset & Workload bridges */
    ps3_hle_register_ctx(0x8A85674Du, "_cellSpursLFQueuePushBody",          _cellSpursLFQueuePushBody);
    ps3_hle_register_ctx(0x327C3F77u, "cellSpursLFQueuePush",               _cellSpursLFQueuePushBody);
    ps3_hle_register_ctx(0x35DAE22Bu, "_cellSpursLFQueuePopBody",           cellSpursLFQueuePopBody);
    ps3_hle_register_ctx(0x92DF869Cu, "cellSpursLFQueuePop",                cellSpursLFQueuePopBody);
    ps3_hle_register_ctx(0x73E06F91u, "cellSpursLFQueueDetachLv2EventQueue", cellSpursLFQueueDetachLv2EventQueue);
    ps3_hle_register_ctx(0xBEB600ACu, "cellSpursCreateTask",                hle_cellSpursCreateTask);
    ps3_hle_register_ctx(0x52CC6C82u, "cellSpursCreateTaskset",             hle_cellSpursCreateTaskset);
    ps3_hle_register_ctx(0xC10931CBu, "cellSpursCreateTasksetWithAttribute", hle_cellSpursCreateTasksetWithAttribute);
    ps3_hle_register_ctx(0x1656D49Fu, "cellSpursLFQueueAttachLv2EventQueue", hle_cellSpursLFQueueAttachLv2EventQueue);
    ps3_hle_register_ctx(0x011EE38Bu, "_cellSpursLFQueueInitialize",         hle_cellSpursLFQueueInitialize);
    ps3_hle_register_ctx(0xA73BF47Eu, "_cellSpursWorkloadFlagReceiver",      _cellSpursWorkloadFlagReceiver);
    ps3_hle_register_ctx(0x4E153E3Eu, "cellSpursGetWorkloadInfo",            hle_cellSpursGetWorkloadInfo);
    ps3_hle_register_ctx(0xC0158D8Bu, "cellSpursAddWorkloadWithAttribute",   hle_cellSpursAddWorkloadWithAttribute);

    /* cellGcmSys extra & label/flip auditing */
    ps3_hle_register_ctx(0x3A33C1FDu, "_cellGcmFunc15",                      _cellGcmFunc15);
    ps3_hle_register_ctx(0xF80196C1u, "cellGcmGetLabelAddress",              hle_cellGcmGetLabelAddress);
    ps3_hle_register_ctx(0xD8F88E1Au, "_cellGcmSetFlipCommandWithWaitLabel", hle_cellGcmSetFlipCommandWithWaitLabel);
    ps3_hle_register_ctx(0x21397818u, "_cellGcmSetFlipCommand",              hle_cellGcmSetFlipCommand);
    ps3_hle_register_ctx(0x72A577CEu, "cellGcmGetFlipStatus",               hle_cellGcmGetFlipStatus);
    ps3_hle_register_ctx(0xA91B0402u, "cellGcmSetVBlankHandler",            gow_cellGcmSetVBlankHandler);

    /* cellSysutilAvconfExt */
    ps3_hle_register_ctx(0xC7020F62u, "cellVideoOutSetGamma",                cellVideoOutSetGamma_nid);
    ps3_hle_register_ctx(0xFAA275A4u, "cellVideoOutGetGamma",                cellVideoOutGetGamma_nid);

    /* cellSysutil */
    ps3_hle_register_ctx(0xCED17573u, "cellSaveDataFixedDelete",             cellSaveDataFixedDelete_nid);

    /* sysPrxForUser CRT/Time */
    ps3_hle_register_ctx(0x4F7172C9u, "sys_process_is_stack",                sys_process_is_stack_nid);
    ps3_hle_register_ctx(0x8461E528u, "sys_time_get_system_time",           sys_time_get_system_time_nid);
    ps3_hle_register_ctx(0x744680A2u, "sys_initialize_tls_alias",           sys_net_ok_stub);
    ps3_hle_register_ctx(0x409AD939u, "sys_mmapper_free_memory",             sys_net_ok_stub);
    ps3_hle_register_ctx(0x4643BA6Eu, "sys_mmapper_unmap_memory",            sys_net_ok_stub);
    ps3_hle_register_ctx(0xDC578057u, "sys_mmapper_map_memory",              sys_net_ok_stub);

    /* Remaining sys_net imports */
    ps3_hle_register_ctx(0x1F953B9Fu, "sys_net_1F953B9F",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0x6005CDE1u, "sys_net_6005CDE1",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0x6DB6E8CDu, "sys_net_6DB6E8CD",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0x71F4C717u, "sys_net_71F4C717",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0x88F03575u, "sys_net_88F03575",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0x9647570Bu, "sys_net_9647570B",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0x9C056962u, "sys_net_9C056962",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0xB0A59804u, "sys_net_B0A59804",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0xFBA04F37u, "sys_net_FBA04F37",                   sys_net_ok_stub);
    ps3_hle_register_ctx(0x3F09E20Au, "sys_net_3F09E20A",                   sys_net_ok_stub);

    /* Remaining sys_fs imports */
    ps3_hle_register_ctx(0xB1840B53u, "cellFsSdataOpen",                     cellFsSdataOpen_stub);
    ps3_hle_register_ctx(0x2796FDF3u, "cellFs_2796FDF3",                     cellFs_ok_stub);
    ps3_hle_register_ctx(0x9B882495u, "cellFs_9B882495",                     cellFs_ok_stub);
    ps3_hle_register_ctx(0x6D3BB15Bu, "cellFs_6D3BB15B",                     cellFs_ok_stub);
    ps3_hle_register_ctx(0x7F13FC8Cu, "cellFs_7F13FC8C",                     cellFs_ok_stub);
    ps3_hle_register_ctx(0x967A162Bu, "cellFs_967A162B",                     cellFs_ok_stub);
    ps3_hle_register_ctx(0xC1C507E7u, "cellFs_C1C507E7",                     cellFs_ok_stub);
    ps3_hle_register_ctx(0x4CEF342Eu, "cellFs_4CEF342E",                     cellFs_ok_stub);
    ps3_hle_register_ctx(0x9F951810u, "cellFs_9F951810",                     cellFs_ok_stub);
    ps3_hle_register_ctx(0xDB869F20u, "cellFs_DB869F20",                     cellFs_ok_stub);
    ppu_register_function(0x005A8D6Cu, hook_func_005A8D6C);
    ppu_register_function(0x003636F8u, hook_func_003636F8);
    ppu_register_function(0x0036298Cu, hook_func_0036298C);
    ppu_register_function(0x003633A4u, hook_func_003633A4);
    ppu_register_function(0x003C8F54u, hook_func_003C8F54);
    ppu_register_function(0x00466B88u, hook_func_00466B88);
    ppu_register_function(0x00466E28u, hook_func_00466E28);
    ppu_register_function(0x00466398u, hook_func_00466398);
    ppu_register_function(0x003E798Cu, hook_func_003E798C);
    ppu_register_function(0x00375A50u, hook_func_00375A50);
    ppu_register_function(0x00416328u, hook_func_00416328);
    ppu_register_function(0x00416600u, hook_func_00416600);
    ppu_register_function(0x00416A00u, hook_func_00416A00);
    ppu_register_function(0x00464AFCu, hook_func_00464AFC);
    ppu_register_function(0x00464A9Cu, hook_func_00464A9C);
    ppu_register_function(0x0046630Cu, hook_func_0046630C);
    ppu_register_function(0x00464F0Cu, hook_func_00464F0C);
    ppu_register_function(0x003A5A94u, hook_func_003A5A94);
    ppu_register_function(0x003A5D00u, hook_func_003A5D00);
    ps3_hle_register_ctx(0xF843818Du, "cellSpursReadyCountStore", hle_cellSpursReadyCountStore);
}

/* Alias for compatibility */
extern "C" void gow_hle_register_extra(void)
{
    ascension_hle_register();
}

extern "C" int64_t sys_timer_usleep(ppu_context* ctx);

extern "C" int64_t hook_sys_timer_usleep(ppu_context* ctx)
{
    const uint32_t tid = (uint32_t)ctx->thread_id;
    const uint32_t lr = (uint32_t)ctx->lr;

    // brbDriver is tid=13 or callers with lr == 0x006245B8
    if (tid == 13 || lr == 0x006245B8u) {
        static uint32_t s_brb_ticks = 0;
        s_brb_ticks++;

        // Advance SPU audio workload progress counters using true pointer ctx->gpr[30]
        uint32_t r31_ptr = (uint32_t)ctx->gpr[30];
        if (!r31_ptr) r31_ptr = 0x009FE180u;
        const uint32_t r5 = vm_read32(r31_ptr);
        if (r5 != 0) {
            for (uint32_t b = 0; b < 2; ++b) {
                const uint32_t hdr = r5 + (b << 4) + 0x40u;
                uint16_t target = vm_read16(hdr);

                // Account for voice entries in 0x30(r11) table
                const uint32_t r11_base = r5 + (b << 2);
                const uint32_t r9_tbl = vm_read32(r11_base + 0x30u);
                uint32_t extra = 0;
                if (r9_tbl != 0) {
                    uint32_t entry = r9_tbl + ((uint32_t)target << 3);
                    if (vm_read16(entry + 2) != 0) {
                        extra = 1;
                        entry += 10;
                        while (vm_read16(entry) != 0 && extra < 512) {
                            extra++;
                            entry += 8;
                        }
                    }
                }
                uint16_t completed_val = (uint16_t)(target + extra);
                vm_write16(hdr, completed_val);
                for (int i = 1; i <= 7; ++i) {
                    vm_write16(hdr + (uint32_t)i * 2, completed_val);
                }
                vm_write32(r5 + 8, b);
            }
        }

        if (s_brb_ticks <= 10 || (s_brb_ticks % 1000) == 0) {
            uint32_t r7 = vm_read32(0x00818F74u);
            uint32_t sub = 0, comp = 0;
            if (r7 >= 0x10000u) {
                uint32_t r9 = vm_read32(r7 + 0x10u);
                uint32_t r10 = vm_read32(r7 + 0x14u);
                if (r9 >= 0x10000u) sub = vm_read32(r9 + 0x10u);
                if (r10 >= 0x10000u) comp = vm_read32(r10 + 0x24u);
            }
            fprintf(stderr, "[BRB-SPU-ADVANCE #%u] tid=%u lr=0x%08X r30=0x%08X r5=0x%08X sub=%u comp=%u\n",
                    s_brb_ticks, tid, lr, r31_ptr, r5, sub, comp);
        }
    }

    if (tid == 1 && lr == 0x0061B9FCu) {
        static uint32_t s_main_ticks = 0;
        uint32_t r28_val = (uint32_t)ctx->gpr[28];
        uint32_t r7 = (r28_val >= 0x10000u) ? vm_read32(r28_val) : 0;
        if (!r7) r7 = vm_read32(0x00818F74u);
        if (r7 >= 0x10000u) {
            uint32_t r9 = vm_read32(r7 + 0x10u);
            uint32_t r10 = vm_read32(r7 + 0x14u);
            if (r9 >= 0x10000u && r10 >= 0x10000u) {
                uint32_t sub = vm_read32(r9 + 0x10u);
                vm_write32(r10 + 0x24u, sub);
                if (++s_main_ticks % 100 == 1) {
                    fprintf(stderr, "[MAIN-BRB] Instantly satisfied brb_StartSession (sub=%u -> comp=%u)\n", sub, sub);
                }
            }
        }
    }

    if (tid == 1 && (lr == 0x00375A78u || lr == 0x00375AB4u)) {
        ppu_gcm_pump();
#ifdef _WIN32
        SwitchToThread();
#endif
        // Experiment 2: Do NOT fake-overwrite ctrl->ref. Diagnostic log only.
        const uint32_t ctrl_ea = vm_read32(0x009BE004u);
        const uint32_t fence_cnt = vm_read32(0x009710C8u);
        if (ctrl_ea >= 0x10000u) {
            const uint32_t cur_ref = vm_read32(ctrl_ea + 0x08);
            const uint32_t target_ref = fence_cnt + (uint32_t)(int32_t)ctx->gpr[31];
            const uint32_t ctrl_get = vm_read32(ctrl_ea + 0x04);
            const uint32_t dl_head = vm_read32(0x009711D4u);
            const uint32_t dl_tail = vm_read32(0x009711D8u);
            static uint32_t s_usleep_fence_diag = 0;
            if (++s_usleep_fence_diag <= 30 || (s_usleep_fence_diag % 100 == 0)) {
                fprintf(stderr, "[FENCE-WAIT #%u] tid=1 lr=0x%08X: cur_ref=%u target_ref=%u (diff=%d) s_fifo_getoff=0x%08X dl_head=%u dl_tail=%u (fence_cnt=%u r31=%d)\n",
                        s_usleep_fence_diag, lr, cur_ref, target_ref, (int32_t)(cur_ref - target_ref), ctrl_get, dl_head, dl_tail, fence_cnt, (int32_t)ctx->gpr[31]);
            }
        }
    }

    if (tid == 1 && (lr == 0x003E8378u || (lr >= 0x003E798Cu && lr <= 0x003E7B80u))) {
        ppu_gcm_pump();
#ifdef _WIN32
        SwitchToThread();
#endif
        for (uint32_t slot = 0; slot < 3; ++slot) {
            vm_write16(0x0099AAC0u + slot * 4, 0);
            vm_write16(0x0099AAB0u + slot * 4, 0);
        }
        for (uint32_t i = 0; i < 8; ++i) {
            vm_write16(0x0099AA80u + i * 4, 0);
            vm_write32(0x0099B460u + i * 4, 0);
        }
        static uint32_t s_frame_sync_unblock = 0;
        if (++s_frame_sync_unblock <= 20 || (s_frame_sync_unblock % 100 == 0)) {
            fprintf(stderr, "[FRAME-SYNC-UNBLOCK #%u] tid=1 lr=0x%08X cleared frame sync counters\n",
                    s_frame_sync_unblock, lr);
        }
    }

    if (tid == 1 && ((lr >= 0x00464A9Cu && lr <= 0x00465100u) || lr == 0x00443EA4u || lr == 0x00466580u || lr == 0x004665C4u || lr == 0x00464B4Cu)) {
        uint32_t mgr_r31 = (uint32_t)ctx->gpr[31];
        uint32_t mgr_r29 = (uint32_t)ctx->gpr[29];
        uint32_t mgr_r3  = (uint32_t)ctx->gpr[3];
        uint32_t glob_mgr = vm_read32(0x008385ACu);
        if (mgr_r31 >= 0x10000u) gow_unblock_wws_464_mgr(mgr_r31);
        if (mgr_r29 >= 0x10000u) gow_unblock_wws_464_mgr(mgr_r29);
        if (mgr_r3 >= 0x10000u) gow_unblock_wws_464_mgr(mgr_r3);
        if (glob_mgr >= 0x10000u) gow_unblock_wws_464_mgr(glob_mgr);
        gow_unblock_416_subqueues(0x00966FB0u);
    }

    if (tid == 1 && (lr == 0x003436ACu || lr == 0x00341864u || lr == 0x003E84DCu || (lr >= 0x003A5A94u && lr <= 0x003A5E00u))) {
        gow_unblock_spurs_tasks_3a5();
    }

    extern ppu_context* volatile g_ppu_main_ctx;
    if (g_ppu_main_ctx) {
        uint32_t mlr = (uint32_t)g_ppu_main_ctx->lr;
        if (mlr == 0x00467E1Cu) {
            if (!getenv("GOW_NO_WWS_HACK")) {
                uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[30];
            uint32_t buf_set = (uint32_t)g_ppu_main_ctx->gpr[4];
            uint16_t target = (uint16_t)g_ppu_main_ctx->gpr[31];
            if (mgr_ea >= 0x10000u) {
                const uint32_t r5 = vm_read32(mgr_ea);
                if (r5 >= 0x10000u && buf_set < 8) {
                    const uint32_t hdr = r5 + (buf_set << 4) + 0x40u;
                    for (int i = 1; i <= 7; ++i) {
                        uint16_t cur = vm_read16(hdr + (uint32_t)i * 2);
                        if (cur < target) {
                            vm_write16(hdr + (uint32_t)i * 2, target);
                        }
                    }
                }
            }
            }
        }
        if (mlr == 0x00375A78u || mlr == 0x00375AB4u) {
            ppu_gcm_pump();
#ifdef _WIN32
            SwitchToThread();
#endif
        }
        if (mlr == 0x003E8378u || (mlr >= 0x003E798Cu && mlr <= 0x003E7B80u)) {
            ppu_gcm_pump();
            for (uint32_t slot = 0; slot < 3; ++slot) {
                vm_write16(0x0099AAC0u + slot * 4, 0);
                vm_write16(0x0099AAB0u + slot * 4, 0);
            }
            for (uint32_t i = 0; i < 8; ++i) {
                vm_write16(0x0099AA80u + i * 4, 0);
                vm_write32(0x0099B460u + i * 4, 0);
            }
        }
        if (mlr == 0x00467B1Cu || mlr == 0x00467B34u || mlr == 0x00467B84u || mlr == 0x0046700Cu || mlr == 0x00467030u) {
            if (getenv("GOW_NO_WWS_HACK")) {
                static uint32_t s_wws_spin_ticks = 0;
                if (++s_wws_spin_ticks <= 20 || (s_wws_spin_ticks % 500 == 0)) {
                    uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[31];
                    const uint32_t r5 = (mgr_ea >= 0x10000u) ? vm_read32(mgr_ea) : 0;
                    uint32_t cur_b = (uint32_t)g_ppu_main_ctx->gpr[4];
                    uint16_t req = (uint16_t)g_ppu_main_ctx->gpr[5];
                    uint16_t cur_val = (r5 >= 0x10000u && (cur_b < 8)) ? vm_read16(r5 + (cur_b << 4) + 0x40u) : 0;
                    fprintf(stderr, "[GOW_NO_WWS_HACK] BLOCKED tid=1 mlr=0x%08X mgr=0x%08X r5=0x%08X lane=%u target=%u cur_val=%u spins=%u\n",
                            mlr, mgr_ea, r5, cur_b, req, cur_val, s_wws_spin_ticks);
                }
            } else {
            uint32_t mgr_ea = (uint32_t)g_ppu_main_ctx->gpr[31];
            if (mgr_ea >= 0x10000u) {
                const uint32_t r5 = vm_read32(mgr_ea);
                if (r5 >= 0x10000u) {
                    uint32_t cur_b = (uint32_t)g_ppu_main_ctx->gpr[4];
                    if (cur_b >= 8) {
                        cur_b = (uint32_t)(g_ppu_main_ctx->gpr[3] >> 4) & 7;
                    }
                    for (uint32_t b = 0; b < 8; ++b) {
                        const uint32_t hdr = r5 + (b << 4) + 0x40u;
                        uint16_t target = vm_read16(hdr);
                        const uint32_t r11_base = r5 + (b << 2);
                        const uint32_t r9_tbl = vm_read32(r11_base + 0x30u);
                        uint32_t extra = 0;
                        if (r9_tbl >= 0x10000u) {
                            uint32_t entry = r9_tbl + ((uint32_t)target << 3);
                            if (vm_read16(entry + 2) != 0) {
                                extra = 1;
                                entry += 10;
                                while (vm_read16(entry) != 0 && extra < 512) {
                                    extra++;
                                    entry += 8;
                                }
                            }
                        }
                        uint16_t completed_val = (uint16_t)(target + extra);
                        if (b == cur_b && (uint16_t)g_ppu_main_ctx->gpr[5] > completed_val) {
                            completed_val = (uint16_t)g_ppu_main_ctx->gpr[5];
                        }
                        vm_write16(hdr, completed_val);
                        for (int i = 1; i <= 7; ++i) {
                            vm_write16(hdr + (uint32_t)i * 2, completed_val);
                        }
                    }
                    if (cur_b < 8) {
                        vm_write32(r5 + 8, cur_b);
                    }
                    static uint32_t s_main_wws_unblock = 0;
                    if (++s_main_wws_unblock <= 20 || (s_main_wws_unblock % 100 == 0)) {
                        fprintf(stderr, "[WWS-MAIN-UNBLOCK #%u] lr=0x%08X mgr=0x%08X r5=0x%08X cur_b=%u completed all lanes\n",
                                s_main_wws_unblock, mlr, mgr_ea, r5, cur_b);
                    }
                }
            }
            }
        }
        if ((mlr >= 0x00464A9Cu && mlr <= 0x00465100u) || mlr == 0x00443EA4u || mlr == 0x00466580u || mlr == 0x004665C4u || mlr == 0x00464B4Cu) {
            uint32_t mgr_r31 = (uint32_t)g_ppu_main_ctx->gpr[31];
            uint32_t mgr_r29 = (uint32_t)g_ppu_main_ctx->gpr[29];
            uint32_t mgr_r3  = (uint32_t)g_ppu_main_ctx->gpr[3];
            uint32_t glob_mgr = vm_read32(0x008385ACu);
            if (mgr_r31 >= 0x10000u) gow_unblock_wws_464_mgr(mgr_r31);
            if (mgr_r29 >= 0x10000u) gow_unblock_wws_464_mgr(mgr_r29);
            if (mgr_r3 >= 0x10000u) gow_unblock_wws_464_mgr(mgr_r3);
            if (glob_mgr >= 0x10000u) gow_unblock_wws_464_mgr(glob_mgr);
            gow_unblock_416_subqueues(0x00966FB0u);
        }
        if (mlr == 0x003436ACu || mlr == 0x00341864u || mlr == 0x003E84DCu || (mlr >= 0x003A5A94u && mlr <= 0x003A5E00u)) {
            gow_unblock_spurs_tasks_3a5();
        }
        if (mlr == 0x00416A3Cu || mlr == 0x0041664Cu || (mlr >= 0x00416328u && mlr <= 0x00416B50u)) {
            gow_unblock_416_subqueues(0x00966FB0u);

            static uint32_t s_last_mlr = 0;
            static uint32_t s_subq_spin_ticks = 0;

            if (mlr == s_last_mlr) {
                s_subq_spin_ticks++;
            } else {
                s_last_mlr = mlr;
                s_subq_spin_ticks = 1;
            }

            if (s_subq_spin_ticks >= 5) {
                const uint32_t master_ea = 0x00966FB0u;
                const uint16_t num_q = vm_read16(master_ea + 0x0A);
                const uint32_t q_base = vm_read32(master_ea + 0x20);

                if (q_base >= 0x10000u && num_q > 0 && num_q <= 32) {
                    for (uint32_t i = 0; i < num_q; ++i) {
                        uint32_t q = q_base + i * 0x70u;
                        vm_write16(q + 0x00, 0);

                        for (uint32_t off = 0x1Cu; off <= 0x24u; off += 4u) {
                            uint32_t p = vm_read32(q + off);
                            if (p >= 0x10000u && vm_read16(p) != 0) {
                                vm_write16(p, 0);
                            }
                        }

                        uint32_t q_mgr = vm_read32(q + 0x2Cu);
                        if (q_mgr >= 0x10000u) {
                            gow_unblock_wws_mgr(q_mgr);
                            gow_unblock_wws_464_mgr(q_mgr);
                        }
                    }

                    const uint32_t glob_mgr = vm_read32(0x008385ACu);
                    if (glob_mgr >= 0x10000u) {
                        gow_unblock_wws_464_mgr(glob_mgr);
                    }
                    static uint32_t s_subq_unblock_log = 0;
                    if (++s_subq_unblock_log <= 20 || (s_subq_unblock_log % 50 == 0)) {
                        fprintf(stderr, "[SUBQ-COUNTER-UNBLOCK #%u] mlr=0x%08X spin_ticks=%u cleared all %u sub-queues\n",
                                s_subq_unblock_log, mlr, s_subq_spin_ticks, num_q);
                    }
                }
            }
        }
    }


    static uint32_t s_last_dl_head = 0xFFFFFFFF;
    static uint32_t s_last_dl_tail = 0xFFFFFFFF;
    static uint32_t s_dl_periodic_tick = 0;
    uint32_t dl_head = vm_read32(0x009711D4u);
    uint32_t dl_tail = vm_read32(0x009711D8u);
    uint32_t flip_int = vm_read32(0x00970F30u);
    if (dl_head != s_last_dl_head || dl_tail != s_last_dl_tail || (++s_dl_periodic_tick % 2000 == 1)) {
        s_last_dl_head = dl_head;
        s_last_dl_tail = dl_tail;
        uint32_t gate = vm_read32(0x0081C9B4u);
        uint32_t f996 = vm_read32(0x009967CCu);
        uint32_t f82b1 = vm_read32(0x0082B68Cu);
        uint32_t f82b2 = vm_read32(0x0082B690u);
        uint32_t vlist = vm_read32(0x00955E60u);
        uint32_t mview = vm_read32(0x008199DCu);
        extern ppu_context* volatile g_ppu_main_ctx;
        uint32_t main_lr = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->lr : 0;
        uint32_t main_r3 = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->gpr[3] : 0;
        uint32_t main_r31 = g_ppu_main_ctx ? (uint32_t)g_ppu_main_ctx->gpr[31] : 0;
        fprintf(stderr, ">>> [GCM-DISPLAY-LIST] dl_head=%u dl_tail=%u flipInt=%u | lr=0x%08X r3=0x%08X r31=0x%08X | gate_C9B4=%u flags=[%u,%u,%u] vlist=0x%08X mview=0x%08X <<<\n",
                dl_head, dl_tail, flip_int, main_lr, main_r3, main_r31, gate, f996, f82b1, f82b2, vlist, mview);
        if (main_lr == 0x00385108u && main_r31) {
            uint32_t d_next = vm_read32(main_r31 + 0x4D0u);
            uint32_t d_prev = vm_read32(main_r31 + 0x4CCu);
            fprintf(stderr, "    [VIEW-LOOP-DIAG] view=0x%08X: list_next=0x%08X list_prev=0x%08X cur_node=0x%08X (node_vt=0x%08X next=0x%08X)\n",
                    main_r31, d_next, d_prev, main_r3,
                    main_r3 ? vm_read32(main_r3) : 0,
                    main_r3 ? vm_read32(main_r3 + 4) : 0);
        }
    }

    return sys_timer_usleep(ctx);
}

extern "C" {
void spu_begin_image(int image_id);
void spu_register_function(uint32_t addr, void (*fn)(spu_context*));
void spu_wws_job_pm_spu_func_00000040(spu_context* ctx);
}

static void gow_wws_job_pm_interrupt_handler(spu_context* ctx)
{
    static int s_logged = 0;
    if (s_logged++ < 16) {
        fprintf(stderr, "[wws-irq] SPU img=2 interrupt 0x00A2C taken -> delegating to 0x0040 (stallstat=0x%X srr0=0x%05X loadJobState=%02X%02X%02X%02X)\n",
                ctx->list_stall_stat, ctx->srr0,
                ctx->ls[0x12A0], ctx->ls[0x12A1], ctx->ls[0x12A2], ctx->ls[0x12A3]);
    }

    // 0x2C..0x3C in wws_job_pm: save r78 to 0x1590, zero r78
    memcpy(ctx->ls + 0x1590, &ctx->gpr[78], 16);
    memset(&ctx->gpr[78], 0, sizeof(ctx->gpr[78]));

    // Enter lifted interrupt handler at 0x40.
    // Executes 0x40 -> 0x60 -> 0x80 (adjusts r1 and calls 0x1990) -> 0x90 -> 0xD0 (restores r1 & regs, iret to srr0).
    spu_wws_job_pm_spu_func_00000040(ctx);
}

void gow_register_spu_overrides(void)
{
    spu_begin_image(2);
    spu_register_function(0x00A2Cu, gow_wws_job_pm_interrupt_handler);
    spu_begin_image(0);
    fprintf(stderr, "[ascension] Registered SPU img=2 vector 0x00A2C -> gow_wws_job_pm_interrupt_handler\n");
}

__attribute__((constructor)) static void gow_spu_overrides_ctor(void)
{
    gow_register_spu_overrides();
}

extern "C" void ascension_post_syscall_init(void)
{
    gow_register_spu_overrides();
    g_lv2_syscalls.handlers[141] = (gow_lv2_syscall_fn)hook_sys_timer_usleep;
    fprintf(stderr, "[ascension] Hooked SYS_TIMER_USLEEP (141) with BRB audio driver handler\n");
}
