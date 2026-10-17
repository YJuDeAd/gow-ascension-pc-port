/*
 * hle_extra.cpp — God of War: Ascension HLE extensions and runtime shims.
 *
 * Implements PS3 firmware imports required by God of War: Ascension that are either
 * missing from upstream ps3recomp runtime or require specific behavioral fixes.
 *
 * Proven PS3 recompilation patterns incorporated from twistedmetal:
 *  1. sys_lwcond_create struct layout fix (32-bit EA into both words for guest FIOS compatibility).
 *  2. sys_ppu_thread_once initializer execution (runs engine initializer once under lock).
 *  3. SPURS v2 taskset & task attribute forwarding (forwards to v1 runtime calls).
 *  4. cellNetCtlGetState returning IPObtained (3) so network polling loops exit cleanly.
 *  5. cellVideoOutGetDeviceInfo advertising valid 720p/1080p 60Hz display modes.
 *  6. cellFsReadWithOffset / cellFsWriteWithOffset filesystem helpers.
 *  7. cellSpursTasksetGetSpursAddress retrieval.
 */

#include "ppu_recomp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mutex>

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

int32_t  cellGcmMapEaIoAddress(uint32_t ea, uint32_t io, uint32_t size);
int32_t  cellSpursCreateTaskset(void* spurs, void* taskset, uint64_t args,
                                const void* priority, uint32_t maxContention);
int32_t  cellSpursCreateTask(void* taskset, void* taskId, void* elf,
                             void* context, uint32_t sizeContext,
                             uint32_t lsPattern_ea, uint32_t argument_ea);

int32_t  cellFsLseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos);
int32_t  cellFsRead(int32_t fd, void* buf, uint64_t nbytes, uint64_t* nread);
int32_t  cellFsWrite(int32_t fd, const void* buf, uint64_t nbytes, uint64_t* nwritten);

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
 * sys_lwcond_create (sysPrxForUser 0xDA0EB71A)
 * PS3 ABI: struct sys_lwcond_t { sys_lwmutex_t* lwmutex; u32 lwcond_queue; }
 * Write mutex EA to both words so guest FIOS pointer check passes.
 * ------------------------------------------------------------------------- */
static void sys_lwcond_create(ppu_context* ctx)
{
    const uint32_t lwcond  = (uint32_t)ctx->gpr[3];
    const uint32_t lwmutex = (uint32_t)ctx->gpr[4];
    vm_write32(lwcond + 0x00, lwmutex);   /* 32-bit lwmutex pointer */
    vm_write32(lwcond + 0x04, lwmutex);   /* lwcond_queue */
    ctx->gpr[3] = 0;   /* CELL_OK */
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

static void cellSpursCreateTaskset2(ppu_context* ctx)
{
    const uint32_t spurs = (uint32_t)ctx->gpr[3], taskset = (uint32_t)ctx->gpr[4];
    const uint32_t attr = (uint32_t)ctx->gpr[5];
    const uint64_t args = attr ? vm_read64(attr + TSA2_ARGS) : 0;
    const uint32_t maxcont = attr ? vm_read32(attr + TSA2_MAXCONT) : 8;

    fprintf(stderr, "[cellSpurs] CreateTaskset2(spurs=0x%08X, taskset=0x%08X, attr=0x%08X) "
                    "args=0x%llX maxContention=%u -> v1 CreateTaskset\n",
            spurs, taskset, attr, (unsigned long long)args, maxcont);

    if (taskset) {
        for (uint32_t o = 0; o < 0x2900; o += 4) vm_write32(taskset + o, 0);
    }

    const int32_t rc = cellSpursCreateTaskset(
        (void*)(uintptr_t)spurs, (void*)(uintptr_t)taskset, args,
        attr ? (const void*)(uintptr_t)(attr + TSA2_PRIORITY) : nullptr, maxcont);

    if (taskset && rc == 0) {
        vm_write32(taskset + 0x1890, 0x2900);   /* Taskset2 size */
    }

    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

static void cellSpursDestroyTaskset2(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

static void cellSpursAttributeEnableSystemWorkload(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

static void sys_spu_printf_initialize(ppu_context* ctx)
{
    ctx->gpr[3] = 0;   /* CELL_OK */
}

#define TA2_SIZE       256
#define TA2_SIZECTX    0x04
#define TA2_EACTX      0x08
#define TA2_LSPATTERN  0x10

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
    const uint32_t taskset = (uint32_t)ctx->gpr[3], taskId = (uint32_t)ctx->gpr[4];
    const uint32_t elf = (uint32_t)ctx->gpr[5], argument = (uint32_t)ctx->gpr[6];
    const uint32_t attr = (uint32_t)ctx->gpr[7];

    const uint32_t sizeContext = attr ? vm_read32(attr + TA2_SIZECTX) : 0;
    const uint32_t eaContext   = attr ? (uint32_t)vm_read64(attr + TA2_EACTX) : 0;
    const uint32_t lsPattern   = attr ? attr + TA2_LSPATTERN : 0;

    fprintf(stderr, "[cellSpurs] CreateTask2(taskset=0x%08X, elf=0x%08X, arg=0x%08X, "
                    "attr=0x%08X ctx=0x%08X/%u) -> v1 CreateTask\n",
            taskset, elf, argument, attr, eaContext, sizeContext);

    const int32_t rc = cellSpursCreateTask((void*)(uintptr_t)taskset,
                                           (void*)(uintptr_t)taskId,
                                           (void*)(uintptr_t)elf,
                                           (void*)(uintptr_t)eaContext,
                                           sizeContext, lsPattern, argument);
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

static void cellSpursTaskGetContextSaveAreaSize(ppu_context* ctx)
{
    ctx->gpr[3] = 0x40000;   /* 256 KB local store */
}

/* cellSpursTasksetGetSpursAddress (0x58D58FCF)
 * pTaskset at r3, ppSpurs at r4. CellSpurs* is at +0x60 in taskset. */
static void cellSpursTasksetGetSpursAddress(ppu_context* ctx)
{
    const uint32_t taskset = (uint32_t)ctx->gpr[3];
    const uint32_t ppSpurs = (uint32_t)ctx->gpr[4];
    if (taskset && ppSpurs) {
        uint32_t spurs = vm_read32(taskset + 0x60);
        vm_write32(ppSpurs, spurs);
    }
    ctx->gpr[3] = 0;   /* CELL_OK */
}

/* ---------------------------------------------------------------------------
 * cellNetCtlGetState (0x8B3EBA69) — report IPObtained (3) so network wait loops exit.
 * ------------------------------------------------------------------------- */
#define CELL_NET_CTL_STATE_IPObtained 3

static void cellNetCtlGetState(ppu_context* ctx)
{
    const uint32_t state = (uint32_t)ctx->gpr[3];
    if (state) {
        vm_write32(state, CELL_NET_CTL_STATE_IPObtained);
    }
    ctx->gpr[3] = 0;   /* CELL_OK */
}

/* ---------------------------------------------------------------------------
 * cellVideoOutGetDeviceInfo (0x1E930EEF) — advertise 720p / 1080p 60Hz HDMI
 * ------------------------------------------------------------------------- */
#define VO_RES_1080         1
#define VO_RES_720          2
#define VO_RES_480          4
#define VO_RES_576          5
#define VO_SCAN_PROGRESSIVE 1
#define VO_ASPECT_16_9      2
#define VO_RATES            (0x0001 | 0x0004)   /* 59.94Hz | 60Hz */

static void cellVideoOutGetDeviceInfo(ppu_context* ctx)
{
    const uint32_t videoOut = (uint32_t)ctx->gpr[3];
    const uint32_t info = (uint32_t)ctx->gpr[5];
    if (!info)    { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x8002B221; return; }
    if (videoOut) { ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)0x8002B220; return; }

    for (uint32_t o = 0; o < 0x0C + 32 * 8; o += 4) vm_write32(info + o, 0);

    uint8_t modes[4] = { VO_RES_720, VO_RES_1080, VO_RES_480, VO_RES_576 };
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t m = info + 0x0C + i * 8;
        vm_write8(m + 0, modes[i]);
        vm_write8(m + 1, VO_SCAN_PROGRESSIVE);
        vm_write8(m + 2, 0);
        vm_write8(m + 3, VO_ASPECT_16_9);
        vm_write16(m + 6, VO_RATES);
    }

    vm_write8(info + 0x00, 1);   /* portType = HDMI */
    vm_write8(info + 0x01, 0);   /* colorSpace = RGB */
    vm_write16(info + 0x02, 0);  /* latency */
    vm_write8(info + 0x04, 4);   /* availableModeCount */
    vm_write8(info + 0x05, 2);   /* state = connected */
    vm_write8(info + 0x06, 1);   /* rgbOutputRange = full */

    ctx->gpr[3] = 0;   /* CELL_OK */
}

/* ---------------------------------------------------------------------------
 * Offset-based filesystem operations: cellFsReadWithOffset & cellFsWriteWithOffset
 * ------------------------------------------------------------------------- */
static void cellFsReadWithOffset(ppu_context* ctx)
{
    int32_t  fd       = (int32_t)ctx->gpr[3];
    int64_t  offset   = (int64_t)ctx->gpr[4];
    void*    buf      = (void*)(uintptr_t)ctx->gpr[5];
    uint64_t nbytes   = ctx->gpr[6];
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
 * Video decoder entry points: cellVdecOpenEx & cellVdecQueryAttrEx
 * ------------------------------------------------------------------------- */
static void cellVdecOpenEx(ppu_context* ctx)
{
    /* r3 = handle out-ptr, return CELL_OK */
    uint32_t pHandle = (uint32_t)ctx->gpr[3];
    if (pHandle) vm_write32(pHandle, 1);
    ctx->gpr[3] = 0;
}

static void cellVdecQueryAttrEx(ppu_context* ctx)
{
    ctx->gpr[3] = 0;
}

/* ---------------------------------------------------------------------------
 * Title metadata from PARAM.SFO
 * ------------------------------------------------------------------------- */
extern "C" void gow_init_title_metadata(void)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/PS3_GAME/PARAM.SFO",
             ppu_vfs_root && *ppu_vfs_root ? ppu_vfs_root : ".");
    cellGame_init_from_paramsfo(path);
}

/* ---------------------------------------------------------------------------
 * Registration of all extra HLE handlers
 * ------------------------------------------------------------------------- */
extern "C" void gow_hle_register_extra(void)
{
    /* sysPrxForUser */
    ps3_hle_register_ctx(0xA3E3BE68u, "sys_ppu_thread_once",          sys_ppu_thread_once);
    ps3_hle_register_ctx(0x42B23552u, "sys_prx_register_library",     sys_prx_register_library);
    ps3_hle_register_ctx(0xDA0EB71Au, "sys_lwcond_create",            sys_lwcond_create);

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

    /* cellVideoOut & cellNetCtl */
    ps3_hle_register_ctx(0x1E930EEFu, "cellVideoOutGetDeviceInfo",     cellVideoOutGetDeviceInfo);
    ps3_hle_register_ctx(0x8B3EBA69u, "cellNetCtlGetState",           cellNetCtlGetState);

    /* sys_fs offset I/O */
    ps3_hle_register_ctx(0x0D5B4A14u, "cellFsReadWithOffset",         cellFsReadWithOffset);
    ps3_hle_register_ctx(0x8CB722D5u, "cellFsWriteWithOffset",        cellFsWriteWithOffset);

    /* libvdec */
    ps3_hle_register_ctx(0x0053E2D8u, "cellVdecOpenEx",               cellVdecOpenEx);
    ps3_hle_register_ctx(0xC982A84Au, "cellVdecQueryAttrEx",          cellVdecQueryAttrEx);
}
