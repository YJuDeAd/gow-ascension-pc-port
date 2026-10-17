/*
 * boot_main.cpp — Native x86-64 PPU boot harness for God of War: Ascension (PS3).
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

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

extern "C" {
uint32_t ppu_load_elf(const char* path);
void     ppu_recomp_register(void);
void     ppu_hle_init(void);
void     ppu_sysprx_register(void);
void     ppu_fs_register(void);
void     gow_hle_register_extra(void);   /* src/hle_extra.cpp */
void     gow_psn_stubs_register(void);   /* src/compat/psn_stubs.cpp */
void     gow_init_title_metadata(void);  /* src/hle_extra.cpp */
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
    fprintf(stderr, "[CRASH] module=%p rva=0x%llX\n",
            (void*)mod, (unsigned long long)((char*)er->ExceptionAddress - (char*)mod));

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

static HANDLE s_hwnd_ready_event = NULL;
static void*  s_shared_hwnd = NULL;
static volatile LONG s_window_closed = 0;

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
            rsx_live_draw_present(0);
            fprintf(stderr, "[rsx] live-draw engine up (D3D12); GDI present suppressed\n");
            rsx_ok = 1;
        } else {
            fprintf(stderr, "[rsx] live-draw init failed (%d)\n", r);
        }
    } else if (!live) {
        rsx_ok = 1;
    }

    fprintf(stderr, "[rsx] backend init %s\n", rsx_ok ? "OK" : "FAILED (running headless)");

    ULONGLONG next_tick = GetTickCount64();
    ULONGLONG last_stats = GetTickCount64();

    while (!s_window_closed) {
        Sleep(1);
        if (rsx_ok) {
            cellGcm_rsx_process_fifo();
            const int flipped = cellGcm_take_flip_pending_synced();
            if (flipped) {
                if (live) rsx_live_draw_present(0);
                else      rsx_d3d12_backend_present();
            }
        }

        ULONGLONG now = GetTickCount64();
        if ((long long)(now - next_tick) >= 0) {
            cellGcm_request_tick();
            if (rsx_ok) cellGcm_rsx_process_fifo();
            next_tick = now + 16;
        }

        if (now - last_stats >= 1000) {
            last_stats = now;
            if (live) {
                fprintf(stderr, "[live] frames=%u last_draws=%u\n",
                        rsx_live_draw_get_frames(),
                        rsx_live_draw_get_last_draws());
                fflush(stderr);
            }
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

    if (argc < 2) {
        printf("Usage: %s <path_to_EBOOT.ELF>\n", argv[0]);
        return 2;
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

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "[FATAL] Failed to load ELF image: %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root configured to: %s\n", ppu_vfs_root);

    printf("[boot] Registering lifted PPU function table...\n");
    ppu_recomp_register();

    ps3_load_prx_modules();

    printf("[boot] Initializing HLE subsystem...\n");
    ppu_hle_init();
    gow_hle_register_extra();
    gow_psn_stubs_register();
    ppu_sysprx_register();
    ppu_fs_register();
    gow_init_title_metadata();

    printf("[boot] Initializing LV2 kernel syscalls...\n");
    lv2_init_syscalls();

    g_ps3_guest_caller = harness_guest_caller;

#ifdef _WIN32
    s_hwnd_ready_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    CreateThread(NULL, 1024 * 1024, window_message_thread, NULL, 0, NULL);
    CreateThread(NULL, 4u * 1024 * 1024, rsx_vblank_thread, NULL, 0, NULL);
#endif

    printf("\n[boot] Dispatching entry OPD 0x%08X (stack top 0x%08X)...\n\n", entry, STACK_TOP);
    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] Execution finished with exit code %d\n", rc);
    return rc;
}
