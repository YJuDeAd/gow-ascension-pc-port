import re

# Read cellSpurs.c
with open('ps3recomp/libs/spurs/cellSpurs.c', 'r', encoding='utf-8') as f:
    c = f.read()

# 1. Attribute init priority
t_attr = """    for (int i = 0; i < 8; i++)
        *(vm_base + (u32)attr_ea + WKATTR_PRIORITY + i) =
            prio_ea ? *(vm_base + (u32)prio_ea + i) : 0;"""

r_attr = """    for (int i = 0; i < 8; i++)
        *(vm_base + (u32)attr_ea + WKATTR_PRIORITY + i) =
            (prio_ea && *(vm_base + (u32)prio_ea + i)) ? *(vm_base + (u32)prio_ea + i) : 1;"""

# 2. AddWorkload priority safety
t_wkl = """            if (priority_h)
                memcpy(s_workloads[i].priority, priority_h, CELL_SPURS_MAX_SPU);
            else
                memset(s_workloads[i].priority, 1, CELL_SPURS_MAX_SPU);

            /* Publish the workload in the REAL BE instance so the game's
             * inlined kernel protocol (readyCount stores, signal bits, state
             * reads) and the policy module's own instance DMAs see it. */
            u32 info = spurs_ea + wkl_info_off(i);
            vm_write64(info + 0x00, (u64)(uintptr_t)pm);        /* addr */
            vm_write64(info + 0x08, data);                       /* arg  */
            vm_write32(info + 0x10, sizePm);                     /* size */
            vm_write32(info + 0x14, i << 24);                    /* uniqueId */
            for (int b = 0; b < 8; b++)
                *(vm_base + info + 0x18 + b) = priority_h ? priority_h[b] : 1;"""

r_wkl = """            int all_zero = 1;
            if (priority_h) {
                for (int b = 0; b < CELL_SPURS_MAX_SPU; b++) {
                    if (priority_h[b] != 0) { all_zero = 0; break; }
                }
            }
            if (priority_h && !all_zero)
                memcpy(s_workloads[i].priority, priority_h, CELL_SPURS_MAX_SPU);
            else
                memset(s_workloads[i].priority, 1, CELL_SPURS_MAX_SPU);

            /* Publish the workload in the REAL BE instance so the game's
             * inlined kernel protocol (readyCount stores, signal bits, state
             * reads) and the policy module's own instance DMAs see it. */
            u32 info = spurs_ea + wkl_info_off(i);
            vm_write64(info + 0x00, (u64)(uintptr_t)pm);        /* addr */
            vm_write64(info + 0x08, data);                       /* arg  */
            vm_write32(info + 0x10, sizePm);                     /* size */
            vm_write32(info + 0x14, i << 24);                    /* uniqueId */
            for (int b = 0; b < 8; b++)
                *(vm_base + info + 0x18 + b) = s_workloads[i].priority[b];"""

# 3. wkl_maxcont default
t_maxc = """static u32 wkl_maxcont(u32 ea, u32 wid)
{
    const u32 over = wkl_maxcont_override(wid);
    if (over) return over;
    return wid < 16 ? vm_read8(ea + SPURS_WKL_MAXCONT + wid) : s_workloads[wid].maxContention;
}"""

r_maxc = """static u32 wkl_maxcont(u32 ea, u32 wid)
{
    const u32 over = wkl_maxcont_override(wid);
    if (over) return over;
    if (wid < 16) return vm_read8(ea + SPURS_WKL_MAXCONT + wid);
    return s_workloads[wid].maxContention ? s_workloads[wid].maxContention : 8;
}"""

# 4. spurs_poll_select
t_poll = """    /* If another workload with higher or equal priority wants this SPU, yield to it */
    static int s_revert_dde = -1;
    if (s_revert_dde < 0) { const char* e = getenv("GOW_REVERT_DDE3F83"); s_revert_dde = (e && *e == '1') ? 1 : 0; }
    if (!s_revert_dde) {
        if (pick != 0x20 && (pick_pri <= cur_pri || (!cur_sig && cur_ready < si->active[cur]))) {
            return pick;
        }
    }

        u32 ret_sel = pick;
    if (cur_sig || cur_ready >= si->active[cur]) {
        ret_sel = cur;
    }"""

r_poll = """    u32 ret_sel = cur;
    if (pick != 0x20) {
        /* A runnable workload wants this SPU:
         * Yield to pick if pick has higher/equal priority, or if cur has no work pending,
         * or if cur already has at least as many active SPUs as its ready count. */
        if (pick_pri <= cur_pri || !cur_sig || cur_ready == 0 || cur_ready <= si->active[cur]) {
            ret_sel = pick;
        }
    } else {
        /* No other workload currently waiting. If cur has no work pending either,
         * return 0x20 to let the policy module exit to the kernel rather than spin. */
        if (!cur_sig && cur_ready == 0) {
            ret_sel = 0x20;
        }
    }"""

patches = [('attr', t_attr, r_attr), ('wkl', t_wkl, r_wkl), ('maxc', t_maxc, r_maxc), ('poll', t_poll, r_poll)]

for name, t, r in patches:
    if t in c:
        print(f"Found {name}")
        c = c.replace(t, r)
    else:
        print(f"NOT found {name}")

with open('ps3recomp/libs/spurs/cellSpurs.c', 'w', encoding='utf-8') as f:
    f.write(c)
print("Done patching cellSpurs.c")
