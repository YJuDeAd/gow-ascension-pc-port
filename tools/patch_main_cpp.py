# tools/patch_main_cpp.py
import sys

def main():
    path = "src/main.cpp"
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    target_inc = '#include <signal.h>'
    replacement_inc = """#include <signal.h>
#include <chrono>"""

    if target_inc not in content:
        print("ERROR: target_inc not found in main.cpp")
        sys.exit(1)

    content = content.replace(target_inc, replacement_inc, 1)

    target_decl = 'extern "C" uint32_t cellGcm_control_guest_addr(void);'
    replacement_decl = """extern "C" uint32_t cellGcm_control_guest_addr(void);
extern "C" volatile uint64_t g_vblank_ticks_count;
extern "C" volatile uint64_t g_vblank_handler_invocations;
extern "C" volatile uint64_t g_ppu_gcm_pump_calls_by_tid[64];
extern "C" void gow_print_pump_callers(void);
uint32_t vm_read32(uint64_t addr);"""

    if target_decl in content:
        content = content.replace(target_decl, replacement_decl, 1)
    else:
        # place before gow_watchdog_thread
        target_wd = 'static DWORD WINAPI gow_watchdog_thread(LPVOID)'
        decl_code = """extern "C" volatile uint64_t g_vblank_ticks_count;
extern "C" volatile uint64_t g_vblank_handler_invocations;
extern "C" volatile uint64_t g_ppu_gcm_pump_calls_by_tid[64];
extern "C" void gow_print_pump_callers(void);
uint32_t vm_read32(uint64_t addr);

"""
        if target_wd not in content:
            print("ERROR: target_wd not found in main.cpp")
            sys.exit(1)
        content = content.replace(target_wd, decl_code + target_wd, 1)

    # Insert gow_telemetry_5s_thread before gow_watchdog_thread
    telemetry_thread_code = """static DWORD WINAPI gow_telemetry_5s_thread(LPVOID)
{
    auto start_time = std::chrono::steady_clock::now();
    auto last_time = start_time;
    uint64_t last_vblank_ticks = 0;
    uint64_t last_vblank_invocations = 0;
    uint64_t last_pump_calls[64] = {0};

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

        uint32_t dl_head = 0, dl_tail = 0, cur_ref = 0;
        if (vm_base) {
            dl_head = vm_read32(0x009711D4u);
            dl_tail = vm_read32(0x009711D8u);
            uint32_t ctrl_ea = vm_read32(0x009BE004u);
            if (ctrl_ea >= 0x10000u) {
                cur_ref = vm_read32(ctrl_ea + 0x08);
            }
        }

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

        fprintf(stderr, "[TELEMETRY 5s] wall=%.1fs | dl_head=%u dl_tail=%u ref=%u | cellGcmTickVBlank=%.1f/s | vblank_handler=%.1f/s | ppu_gcm_pump/s: %s\\n",
                wall_sec, dl_head, dl_tail, cur_ref, vblank_tick_rate, vblank_inv_rate, pump_buf);
        fflush(stderr);

        static int s_dump_t = 0;
        if (++s_dump_t % 2 == 0) {
            gow_print_pump_callers();
        }
    }
    return 0;
}

"""
    target_wd = 'static DWORD WINAPI gow_watchdog_thread(LPVOID)'
    content = content.replace(target_wd, telemetry_thread_code + target_wd, 1)

    target_spawn = 'CreateThread(NULL, 1024 * 1024, gow_watchdog_thread, NULL, 0, NULL);'
    replacement_spawn = """CreateThread(NULL, 1024 * 1024, gow_watchdog_thread, NULL, 0, NULL);
    CreateThread(NULL, 1024 * 1024, gow_telemetry_5s_thread, NULL, 0, NULL);"""

    if target_spawn not in content:
        print("ERROR: target_spawn not found in main.cpp")
        sys.exit(1)

    content = content.replace(target_spawn, replacement_spawn, 1)

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

    print("SUCCESS: main.cpp patched successfully")

if __name__ == "__main__":
    main()
