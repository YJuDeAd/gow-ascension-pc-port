# tools/apply_exp2_ref_fence.py
import sys

def main():
    path = "src/compat/ascension_hle.cpp"
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    target1 = """    const uint32_t ctrl_ea = vm_read32(0x009BE004u);
    const uint32_t fence_cnt = vm_read32(0x009710C8u);
    if (ctrl_ea >= 0x10000u) {
        const uint32_t cur_ref = vm_read32(ctrl_ea + 0x08);
        const uint32_t target_ref = fence_cnt + (uint32_t)(int32_t)ctx->gpr[3];
        if ((int32_t)(cur_ref - target_ref) < 0) {
            static uint32_t s_fence_unblock = 0;
            if (++s_fence_unblock <= 20 || (s_fence_unblock % 100 == 0)) {
                fprintf(stderr, "[FENCE-SYNC-SATISFY #%u] hook_func_00375A50: cur_ref=%u target_ref=%u (fence_cnt=%u r3=%d)\\n",
                        s_fence_unblock, cur_ref, target_ref, fence_cnt, (int32_t)ctx->gpr[3]);
            }
            vm_write32(ctrl_ea + 0x08, target_ref);
        }
    }"""

    replacement1 = """    // Experiment 2: Do NOT fake-overwrite ctrl->ref (ctrl_ea + 0x08).
    // Let real RSX FIFO drain (SET_REFERENCE / gcm_ref_publish_one) advance ctrl->ref naturally!"""

    target2 = """    if (tid == 1 && (lr == 0x00375A78u || lr == 0x00375AB4u)) {
        ppu_gcm_pump();
#ifdef _WIN32
        SwitchToThread();
#endif
        const uint32_t ctrl_ea = vm_read32(0x009BE004u);
        const uint32_t fence_cnt = vm_read32(0x009710C8u);
        if (ctrl_ea >= 0x10000u) {
            const uint32_t cur_ref = vm_read32(ctrl_ea + 0x08);
            const uint32_t target_ref = fence_cnt + (uint32_t)(int32_t)ctx->gpr[31];
            if ((int32_t)(cur_ref - target_ref) < 0) {
                static uint32_t s_usleep_fence_unblock = 0;
                if (++s_usleep_fence_unblock <= 20 || (s_usleep_fence_unblock % 100 == 0)) {
                    fprintf(stderr, "[FENCE-USLEEP-SATISFY #%u] tid=1 lr=0x%08X: cur_ref=%u target_ref=%u (fence_cnt=%u r31=%d)\\n",
                            s_usleep_fence_unblock, lr, cur_ref, target_ref, fence_cnt, (int32_t)ctx->gpr[31]);
                }
                vm_write32(ctrl_ea + 0x08, target_ref);
            }
        }
    }"""

    replacement2 = """    if (tid == 1 && (lr == 0x00375A78u || lr == 0x00375AB4u)) {
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
                fprintf(stderr, "[FENCE-WAIT #%u] tid=1 lr=0x%08X: cur_ref=%u target_ref=%u (diff=%d) s_fifo_getoff=0x%08X dl_head=%u dl_tail=%u (fence_cnt=%u r31=%d)\\n",
                        s_usleep_fence_diag, lr, cur_ref, target_ref, (int32_t)(cur_ref - target_ref), ctrl_get, dl_head, dl_tail, fence_cnt, (int32_t)ctx->gpr[31]);
            }
        }
    }"""

    target3 = """        if (mlr == 0x00375A78u || mlr == 0x00375AB4u) {
            ppu_gcm_pump();
            const uint32_t ctrl_ea = vm_read32(0x009BE004u);
            const uint32_t fence_cnt = vm_read32(0x009710C8u);
            if (ctrl_ea >= 0x10000u) {
                const uint32_t cur_ref = vm_read32(ctrl_ea + 0x08);
                const uint32_t target_ref = fence_cnt + (uint32_t)(int32_t)g_ppu_main_ctx->gpr[31];
                if ((int32_t)(cur_ref - target_ref) < 0) {
                    vm_write32(ctrl_ea + 0x08, target_ref);
                }
            }
        }"""

    replacement3 = """        if (mlr == 0x00375A78u || mlr == 0x00375AB4u) {
            ppu_gcm_pump();
#ifdef _WIN32
            SwitchToThread();
#endif
        }"""

    # Check normalized line endings
    crlf = "\r\n" in text
    linesep = "\r\n" if crlf else "\n"

    def norm(s):
        return s.replace("\r\n", "\n").replace("\n", linesep)

    target1 = norm(target1)
    replacement1 = norm(replacement1)
    target2 = norm(target2)
    replacement2 = norm(replacement2)
    target3 = norm(target3)
    replacement3 = norm(replacement3)

    if target1 not in text:
        print("ERROR: target1 not found!")
        sys.exit(1)
    if target2 not in text:
        print("ERROR: target2 not found!")
        sys.exit(1)
    if target3 not in text:
        print("ERROR: target3 not found!")
        sys.exit(1)

    text = text.replace(target1, replacement1, 1)
    text = text.replace(target2, replacement2, 1)
    text = text.replace(target3, replacement3, 1)

    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)

    print("SUCCESS: Experiment 2 changes applied successfully to src/compat/ascension_hle.cpp")

if __name__ == "__main__":
    main()
