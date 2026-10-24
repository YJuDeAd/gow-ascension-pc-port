# tools/patch_cellgcmsys.py
import sys

def main():
    path = "ps3recomp/libs/video/cellGcmSys.c"
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    target = """static u32 s_fifo_getoff;   /* tentative: defined below */

void cellGcm_request_tick(void)
{
    s_vblank_count++;"""

    replacement = """static u32 s_fifo_getoff;   /* tentative: defined below */

extern uint32_t ppu_diag_get_current_tid(void);
volatile uint64_t g_vblank_ticks_count = 0;
volatile uint64_t g_vblank_handler_invocations = 0;
volatile uint64_t g_ppu_gcm_pump_calls_by_tid[64] = {0};

typedef struct {
    const char* file;
    int line;
    uint32_t tid;
    uint64_t count;
} gcm_pump_caller_entry_t;

#define MAX_GCM_PUMP_CALLERS 256
static gcm_pump_caller_entry_t s_pump_callers[MAX_GCM_PUMP_CALLERS];
static int s_pump_caller_count = 0;
static SRWLOCK s_pump_caller_lock = SRWLOCK_INIT;

void ppu_gcm_pump_record_caller(uint32_t tid, const char* file, int line)
{
    if (tid < 64) {
        g_ppu_gcm_pump_calls_by_tid[tid]++;
    }
    if (!file) return;
    AcquireSRWLockExclusive(&s_pump_caller_lock);
    for (int i = 0; i < s_pump_caller_count; i++) {
        if (s_pump_callers[i].tid == tid &&
            s_pump_callers[i].line == line &&
            strcmp(s_pump_callers[i].file, file) == 0) {
            s_pump_callers[i].count++;
            ReleaseSRWLockExclusive(&s_pump_caller_lock);
            return;
        }
    }
    if (s_pump_caller_count < MAX_GCM_PUMP_CALLERS) {
        s_pump_callers[s_pump_caller_count].file = file;
        s_pump_callers[s_pump_caller_count].line = line;
        s_pump_callers[s_pump_caller_count].tid = tid;
        s_pump_callers[s_pump_caller_count].count = 1;
        s_pump_caller_count++;
    }
    ReleaseSRWLockExclusive(&s_pump_caller_lock);
}

void gow_print_pump_callers(void)
{
    AcquireSRWLockExclusive(&s_pump_caller_lock);
    fprintf(stderr, "\\n=== [PPU_GCM_PUMP-CALLERS] Callers of ppu_gcm_pump (%d distinct sites) ===\\n", s_pump_caller_count);
    for (int i = 0; i < s_pump_caller_count; i++) {
        fprintf(stderr, "  tid=%-2u caller=%s:%d  count=%llu\\n",
                s_pump_callers[i].tid,
                s_pump_callers[i].file,
                s_pump_callers[i].line,
                (unsigned long long)s_pump_callers[i].count);
    }
    fprintf(stderr, "========================================================================\\n\\n");
    fflush(stderr);
    ReleaseSRWLockExclusive(&s_pump_caller_lock);
}

static _Thread_local int t_in_pump_caller = 0;

void ppu_gcm_pump(void);

void ppu_gcm_pump_caller(const char* file, int line)
{
    uint32_t tid = ppu_diag_get_current_tid();
    ppu_gcm_pump_record_caller(tid, file, line);
    t_in_pump_caller = 1;
    ppu_gcm_pump();
    t_in_pump_caller = 0;
}

void cellGcm_request_tick(void)
{
    g_vblank_ticks_count++;
    s_vblank_count++;"""

    if target not in content:
        print("ERROR: target 1 not found in cellGcmSys.c")
        sys.exit(1)

    content = content.replace(target, replacement, 1)

    target_pump = """void ppu_gcm_pump(void)
{
    if (!GCM_PUMP_TRY_ENTER()) return;"""

    replacement_pump = """void ppu_gcm_pump(void)
{
    if (!t_in_pump_caller) {
        uint32_t tid = ppu_diag_get_current_tid();
        ppu_gcm_pump_record_caller(tid, "direct_ppu_gcm_pump", 0);
    }
    if (!GCM_PUMP_TRY_ENTER()) return;"""

    if target_pump not in content:
        print("ERROR: target_pump not found in cellGcmSys.c")
        sys.exit(1)

    content = content.replace(target_pump, replacement_pump, 1)

    target_inv = """    if ((p & 1) && s_vblank_handler_opd && g_ps3_guest_caller) {
        fprintf(stderr, "[GCM-DIAG] INVOKING VBlank handler OPD 0x%08X (count=%u)\\n", s_vblank_handler_opd, s_vblank_count);"""

    replacement_inv = """    if ((p & 1) && s_vblank_handler_opd && g_ps3_guest_caller) {
        g_vblank_handler_invocations++;
        fprintf(stderr, "[GCM-DIAG] INVOKING VBlank handler OPD 0x%08X (count=%u)\\n", s_vblank_handler_opd, s_vblank_count);"""

    if target_inv not in content:
        print("ERROR: target_inv not found in cellGcmSys.c")
        sys.exit(1)

    content = content.replace(target_inv, replacement_inv, 1)

    target_tick = """void cellGcmTickVBlank(void) { s_vblank_count++; GCM_PENDING_SET(1); }"""
    replacement_tick = """void cellGcmTickVBlank(void) { g_vblank_ticks_count++; s_vblank_count++; GCM_PENDING_SET(1); }"""

    if target_tick not in content:
        print("ERROR: target_tick not found in cellGcmSys.c")
        sys.exit(1)

    content = content.replace(target_tick, replacement_tick, 1)

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

    print("SUCCESS: cellGcmSys.c patched successfully")

if __name__ == "__main__":
    main()
