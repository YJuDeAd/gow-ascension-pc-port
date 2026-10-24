import os

with open('ps3recomp/libs/spurs/cellSpurs.c', 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Update SpursWorkload
target_wkl = """typedef struct {
    int         in_use;
    const void* pm;            /* policy-module image EA (guest) */
    u32         sizePm;
    u64         data;          /* workload data (e.g. joblist EA) */
    u32         spurs_ea;      /* owning CellSpurs instance EA */
    u8          priority[CELL_SPURS_MAX_SPU];
    u32         minContention;
    u32         maxContention;
    u32         readyCount;
    int         started;       /* its SPU program has been dispatched        */
} SpursWorkload;"""

repl_wkl = """typedef struct {
    int         in_use;
    const void* pm;            /* policy-module image EA (guest) */
    u32         sizePm;
    u64         data;          /* workload data (e.g. joblist EA) */
    u32         spurs_ea;      /* owning CellSpurs instance EA */
    u8          priority[CELL_SPURS_MAX_SPU];
    u32         minContention;
    u32         maxContention;
    u32         readyCount;
    int         started;       /* its SPU program has been dispatched        */
    u8          state;         /* 0: unused, 2: runnable, 3: detached        */
    u8          curContention;
    u8          idleSpuRequest;
    u16         signal;
} SpursWorkload;"""

assert target_wkl in text, "target_wkl not found in cellSpurs.c"
text = text.replace(target_wkl, repl_wkl, 1)

# 2. Update SpursInst active/idle arrays
target_inst = """    struct SpursPmSlot slots[6];
    u8 active[16], idle[16];
    u32 cursor, pass_no;"""

repl_inst = """    struct SpursPmSlot slots[6];
    u8 active[CELL_SPURS_MAX_WORKLOAD], idle[CELL_SPURS_MAX_WORKLOAD];
    u32 cursor, pass_no;"""

assert target_inst in text, "target_inst not found in cellSpurs.c"
text = text.replace(target_inst, repl_inst, 1)

# 3. Update cellSpursSetMaxContention
target_maxcont = """    s_workloads[wid].maxContention = maxContention;
    vm_write8(si->ea + SPURS_WKL_MAXCONT + wid, (u8)maxContention);"""

repl_maxcont = """    s_workloads[wid].maxContention = maxContention;
    if (wid < 16) {
        vm_write8(si->ea + SPURS_WKL_MAXCONT + wid, (u8)maxContention);
    }"""

assert target_maxcont in text, "target_maxcont not found in cellSpurs.c"
text = text.replace(target_maxcont, repl_maxcont, 1)

# 4. Update cellSpursSetPriorities
target_prio = """    memcpy(s_workloads[wid].priority, priorities_h, CELL_SPURS_MAX_SPU);
    for (u32 sn = 0; sn < CELL_SPURS_MAX_SPU; sn++)
        vm_write8(si->ea + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 0x18 + sn, priorities_h[sn]);"""

repl_prio = """    memcpy(s_workloads[wid].priority, priorities_h, CELL_SPURS_MAX_SPU);
    if (wid < 16) {
        for (u32 sn = 0; sn < CELL_SPURS_MAX_SPU; sn++)
            vm_write8(si->ea + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 0x18 + sn, priorities_h[sn]);
    }"""

assert target_prio in text, "target_prio not found in cellSpurs.c"
text = text.replace(target_prio, repl_prio, 1)

# 5. Update cellSpursAddWorkloadWithAttribute
target_add = """    for (u32 i = 0; i < CELL_SPURS_MAX_WORKLOAD; i++) {
        if (!s_workloads[i].in_use) {
            spurs_scheduler_forget(i);
            si->idle[i] = 0;
            spu_lockline_lock();
            vm_base[spurs_ea + SPURS_WKL_READY1 + i] = 0;
            vm_base[spurs_ea + SPURS_WKL_CURCONT + i] = 0;
            ef_w16(spurs_ea + SPURS_WKL_SIGNAL1,
                   (u16)(vm_read16(spurs_ea + SPURS_WKL_SIGNAL1) & ~(0x8000u >> i)));
            ef_line_end(spurs_ea);
            s_workloads[i].in_use = 1;
            s_workloads[i].pm = pm;
            s_workloads[i].sizePm = sizePm;
            s_workloads[i].data = data;
            s_workloads[i].spurs_ea = spurs_ea;
            s_workloads[i].minContention = minContention;
            s_workloads[i].maxContention = maxContention;
            s_workloads[i].readyCount = 0;

            if (priority_h)
                memcpy(s_workloads[i].priority, priority_h, CELL_SPURS_MAX_SPU);
            else
                memset(s_workloads[i].priority, 1, CELL_SPURS_MAX_SPU);

            /* Publish the workload in the REAL BE instance so the game's
             * inlined kernel protocol (readyCount stores, signal bits, state
             * reads) and the policy module's own instance DMAs see it. */
            u32 info = spurs_ea + SPURS_WKL_INFO1 + i * SPURS_WKL_INFO_SZ;
            vm_write64(info + 0x00, (u64)(uintptr_t)pm);        /* addr */
            vm_write64(info + 0x08, data);                       /* arg  */
            vm_write32(info + 0x10, sizePm);                     /* size */
            vm_write32(info + 0x14, i << 24);                    /* uniqueId */
            for (int b = 0; b < 8; b++)
                *(vm_base + info + 0x18 + b) = priority_h ? priority_h[b] : 1;
            *(vm_base + spurs_ea + SPURS_WKL_STATE1  + i) = 2;   /* runnable */
            *(vm_base + spurs_ea + SPURS_WKL_MINCONT + i) = (u8)(minContention ? minContention : 1);
            *(vm_base + spurs_ea + SPURS_WKL_MAXCONT + i) = (u8)(maxContention ? maxContention : 1);
            vm_write32(spurs_ea + SPURS_WKL_ENABLED,
                       vm_read32(spurs_ea + SPURS_WKL_ENABLED) | (0x80000000u >> i));
            *(vm_base + spurs_ea + SPURS_SYSSRV_MSG) = 0xFF;

            /* wid out-param is guest BE */
            vm_write32((u32)(uintptr_t)wid, i);
            printf("[cellSpurs] AddWorkload(wid=%u, pm=%p, size=%u)\\n",
                   i, pm, sizePm);
            spurs_api_unlock(si);
            return CELL_OK;
        }
    }"""

repl_add = """    for (u32 i = 0; i < CELL_SPURS_MAX_WORKLOAD; i++) {
        if (!s_workloads[i].in_use) {
            spurs_scheduler_forget(i);
            si->idle[i] = 0;
            if (i < 16) {
                spu_lockline_lock();
                vm_base[spurs_ea + SPURS_WKL_READY1 + i] = 0;
                vm_base[spurs_ea + SPURS_WKL_CURCONT + i] = 0;
                ef_w16(spurs_ea + SPURS_WKL_SIGNAL1,
                       (u16)(vm_read16(spurs_ea + SPURS_WKL_SIGNAL1) & ~(0x8000u >> i)));
                ef_line_end(spurs_ea);
            }
            s_workloads[i].in_use = 1;
            s_workloads[i].pm = pm;
            s_workloads[i].sizePm = sizePm;
            s_workloads[i].data = data;
            s_workloads[i].spurs_ea = spurs_ea;
            s_workloads[i].minContention = minContention ? minContention : 1;
            s_workloads[i].maxContention = maxContention ? maxContention : 1;
            s_workloads[i].readyCount = 0;
            s_workloads[i].state = 2; // runnable
            s_workloads[i].curContention = 0;
            s_workloads[i].idleSpuRequest = 0;
            s_workloads[i].signal = 0;

            if (priority_h)
                memcpy(s_workloads[i].priority, priority_h, CELL_SPURS_MAX_SPU);
            else
                memset(s_workloads[i].priority, 1, CELL_SPURS_MAX_SPU);

            if (i < 16) {
                u32 info = spurs_ea + SPURS_WKL_INFO1 + i * SPURS_WKL_INFO_SZ;
                vm_write64(info + 0x00, (u64)(uintptr_t)pm);        /* addr */
                vm_write64(info + 0x08, data);                       /* arg  */
                vm_write32(info + 0x10, sizePm);                     /* size */
                vm_write32(info + 0x14, i << 24);                    /* uniqueId */
                for (int b = 0; b < 8; b++)
                    *(vm_base + info + 0x18 + b) = priority_h ? priority_h[b] : 1;
                *(vm_base + spurs_ea + SPURS_WKL_STATE1  + i) = 2;   /* runnable */
                *(vm_base + spurs_ea + SPURS_WKL_MINCONT + i) = (u8)s_workloads[i].minContention;
                *(vm_base + spurs_ea + SPURS_WKL_MAXCONT + i) = (u8)s_workloads[i].maxContention;
            }
            vm_write32(spurs_ea + SPURS_WKL_ENABLED,
                       vm_read32(spurs_ea + SPURS_WKL_ENABLED) | (0x80000000u >> i));
            *(vm_base + spurs_ea + SPURS_SYSSRV_MSG) = 0xFF;

            /* wid out-param is guest BE */
            vm_write32((u32)(uintptr_t)wid, i);
            printf("[cellSpurs] AddWorkload(wid=%u, pm=%p, size=%u)\\n",
                   i, pm, sizePm);
            spurs_api_unlock(si);
            return CELL_OK;
        }
    }"""

assert target_add in text, "target_add not found in cellSpurs.c"
text = text.replace(target_add, repl_add, 1)

# 6. Update cellSpursRemoveWorkload
target_rem = """    s_workloads[wid].in_use = 0;
    vm_write8(si->ea + SPURS_WKL_STATE1 + wid, 3);
    vm_write32(si->ea + SPURS_WKL_ENABLED,
               vm_read32(si->ea + SPURS_WKL_ENABLED) & ~(0x80000000u >> wid));"""

repl_rem = """    s_workloads[wid].in_use = 0;
    s_workloads[wid].state = 3;
    if (wid < 16) {
        vm_write8(si->ea + SPURS_WKL_STATE1 + wid, 3);
    }
    vm_write32(si->ea + SPURS_WKL_ENABLED,
               vm_read32(si->ea + SPURS_WKL_ENABLED) & ~(0x80000000u >> wid));"""

assert target_rem in text, "target_rem not found in cellSpurs.c"
text = text.replace(target_rem, repl_rem, 1)

# 7. Update spurs_pm_worker
target_pmw = """        u32 wid = slot->wid;
        if (si->active[wid]) si->active[wid]--;
        spu_lockline_lock();
        vm_base[slot->ea + SPURS_WKL_CURCONT + wid] = si->active[wid];
        ef_line_end(slot->ea);"""

repl_pmw = """        u32 wid = slot->wid;
        if (si->active[wid]) si->active[wid]--;
        if (wid < 16) {
            spu_lockline_lock();
            vm_base[slot->ea + SPURS_WKL_CURCONT + wid] = si->active[wid];
            ef_line_end(slot->ea);
        }
        s_workloads[wid].curContention = si->active[wid];"""

assert target_pmw in text, "target_pmw not found in cellSpurs.c"
text = text.replace(target_pmw, repl_pmw, 1)

# 8. Update spurs_kernel_thread dispatch loop
target_disp = """            for (u32 checked = 0; checked < 16; checked++) {
                u32 wid = si->cursor;
                si->cursor = (wid + 1) & 15;
                if (!(enabled & (0x80000000u >> wid)) ||
                    vm_read8(ea + SPURS_WKL_STATE1 + wid) != 2 ||
                    !s_workloads[wid].in_use || s_workloads[wid].spurs_ea != ea)
                    continue;
                if (!vm_read8(ea + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 0x18 + sn))
                    continue; /* priority zero makes this physical lane ineligible */
                u32 maxcont = vm_read8(ea + SPURS_WKL_MAXCONT + wid);
                if (!maxcont || si->active[wid] >= maxcont) continue;
                WklPm* pm = spurs_resolve_pm(wid);
                if (!pm) continue;
                u32 signal = vm_read16(ea + SPURS_WKL_SIGNAL1);
                u32 ready = vm_read8(ea + SPURS_WKL_READY1 + wid);
                int kicked = ready || (signal & (0x8000u >> wid));
                if (kicked) si->idle[wid] = 0;
                if (!kicked && (si->pass_no & ((1u << si->idle[wid]) - 1)))
                    continue;
                spu_lockline_lock();
                ready = vm_read8(ea + SPURS_WKL_READY1 + wid);
                signal = vm_read16(ea + SPURS_WKL_SIGNAL1);
                if (ready) vm_base[ea + SPURS_WKL_READY1 + wid] = (u8)(ready - 1);
                if (signal & (0x8000u >> wid))
                    ef_w16(ea + SPURS_WKL_SIGNAL1, (u16)(signal & ~(0x8000u >> wid)));
                ef_line_end(ea);
                slot->fn = pm->fn; slot->image_id = pm->image_id;
                slot->pm = vm_base + (u32)(uintptr_t)s_workloads[wid].pm;
                slot->pm_size = s_workloads[wid].sizePm;
                slot->data = vm_read64(ea + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 8);
                slot->wid = wid; slot->ea = ea;
                si->active[wid]++;
                spu_lockline_lock();
                vm_base[ea + SPURS_WKL_CURCONT + wid] = si->active[wid];
                ef_line_end(ea);
                slot->busy = 1;
                if (wid < 16) g_spurs_wid_diags[wid].dispatched_count++;
                WakeAllConditionVariable(&si->wake);
                break;
            }"""

repl_disp = """            for (u32 checked = 0; checked < CELL_SPURS_MAX_WORKLOAD; checked++) {
                u32 wid = si->cursor;
                si->cursor = (wid + 1) % CELL_SPURS_MAX_WORKLOAD;
                if (!(enabled & (0x80000000u >> wid)))
                    continue;
                u8 state = (wid < 16) ? vm_read8(ea + SPURS_WKL_STATE1 + wid) : s_workloads[wid].state;
                if (state != 2 || !s_workloads[wid].in_use || s_workloads[wid].spurs_ea != ea)
                    continue;
                u8 prio = (wid < 16) ? vm_read8(ea + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 0x18 + sn)
                                     : s_workloads[wid].priority[sn];
                if (!prio)
                    continue; /* priority zero makes this physical lane ineligible */
                u32 maxcont = (wid < 16) ? vm_read8(ea + SPURS_WKL_MAXCONT + wid) : s_workloads[wid].maxContention;
                if (!maxcont || si->active[wid] >= maxcont) continue;
                WklPm* pm = spurs_resolve_pm(wid);
                if (!pm) continue;
                u32 signal = (wid < 16) ? vm_read16(ea + SPURS_WKL_SIGNAL1) : s_workloads[wid].signal;
                u32 ready = (wid < 16) ? vm_read8(ea + SPURS_WKL_READY1 + wid) : s_workloads[wid].readyCount;
                int kicked = (wid < 16) ? (ready || (signal & (0x8000u >> wid))) : (ready || signal);
                if (kicked) si->idle[wid] = 0;
                if (!kicked && (si->pass_no & ((1u << si->idle[wid]) - 1)))
                    continue;
                spu_lockline_lock();
                if (wid < 16) {
                    ready = vm_read8(ea + SPURS_WKL_READY1 + wid);
                    signal = vm_read16(ea + SPURS_WKL_SIGNAL1);
                    if (ready) vm_base[ea + SPURS_WKL_READY1 + wid] = (u8)(ready - 1);
                    s_workloads[wid].readyCount = ready ? (ready - 1) : 0;
                    if (signal & (0x8000u >> wid))
                        ef_w16(ea + SPURS_WKL_SIGNAL1, (u16)(signal & ~(0x8000u >> wid)));
                    s_workloads[wid].signal = (u16)(signal & ~(0x8000u >> wid));
                } else {
                    if (s_workloads[wid].readyCount) s_workloads[wid].readyCount--;
                    s_workloads[wid].signal = 0;
                }
                ef_line_end(ea);
                slot->fn = pm->fn; slot->image_id = pm->image_id;
                slot->pm = vm_base + (u32)(uintptr_t)s_workloads[wid].pm;
                slot->pm_size = s_workloads[wid].sizePm;
                slot->data = (wid < 16) ? vm_read64(ea + SPURS_WKL_INFO1 + wid * SPURS_WKL_INFO_SZ + 8)
                                        : s_workloads[wid].data;
                slot->wid = wid; slot->ea = ea;
                si->active[wid]++;
                if (wid < 16) {
                    spu_lockline_lock();
                    vm_base[ea + SPURS_WKL_CURCONT + wid] = si->active[wid];
                    ef_line_end(ea);
                }
                s_workloads[wid].curContention = si->active[wid];
                slot->busy = 1;
                if (wid < 32) g_spurs_wid_diags[wid].dispatched_count++;
                WakeAllConditionVariable(&si->wake);
                break;
            }"""

assert target_disp in text, "target_disp not found in cellSpurs.c"
text = text.replace(target_disp, repl_disp, 1)

# 9. Update spurs_scheduler_stop
target_stop = """    for (u32 wid = 0; wid < 16; wid++)
        if (si->active[wid]) { ReleaseSRWLockExclusive(&si->lock); return CELL_SPURS_CORE_ERROR_BUSY; }"""

repl_stop = """    for (u32 wid = 0; wid < CELL_SPURS_MAX_WORKLOAD; wid++)
        if (si->active[wid]) { ReleaseSRWLockExclusive(&si->lock); return CELL_SPURS_CORE_ERROR_BUSY; }"""

assert target_stop in text, "target_stop not found in cellSpurs.c"
text = text.replace(target_stop, repl_stop, 1)

# 10. Update cellSpursReadyCountStore
target_rcs = """    s_workloads[wid].readyCount = value;
    /* The real store: the instance byte the kernel (poll thread) watches. */
    vm_write8(si->ea + SPURS_WKL_READY1 + wid, (u8)value);"""

repl_rcs = """    s_workloads[wid].readyCount = value;
    /* The real store: the instance byte the kernel (poll thread) watches. */
    if (wid < 16) {
        vm_write8(si->ea + SPURS_WKL_READY1 + wid, (u8)value);
    }"""

assert target_rcs in text, "target_rcs not found in cellSpurs.c"
text = text.replace(target_rcs, repl_rcs, 1)

# 11. Update spurs_ready_exchange
target_rex = """    spu_lockline_lock();
    u8 previous = vm_read8(si->ea + SPURS_WKL_READY1 + wid);
    if (!conditional || previous == compare) {
        vm_base[si->ea + SPURS_WKL_READY1 + wid] = (u8)value;
        s_workloads[wid].readyCount = value;
        ef_line_end(si->ea);
    } else spu_lockline_unlock();
    vm_write32((u32)(uintptr_t)old, previous);"""

repl_rex = """    u8 previous;
    if (wid < 16) {
        spu_lockline_lock();
        previous = vm_read8(si->ea + SPURS_WKL_READY1 + wid);
        if (!conditional || previous == compare) {
            vm_base[si->ea + SPURS_WKL_READY1 + wid] = (u8)value;
            s_workloads[wid].readyCount = value;
            ef_line_end(si->ea);
        } else spu_lockline_unlock();
    } else {
        previous = (u8)s_workloads[wid].readyCount;
        if (!conditional || previous == compare) {
            s_workloads[wid].readyCount = value;
        }
    }
    vm_write32((u32)(uintptr_t)old, previous);"""

assert target_rex in text, "target_rex not found in cellSpurs.c"
text = text.replace(target_rex, repl_rex, 1)

# 12. Update cellSpursGetWorkloadInfo
target_gwi = """    u32 se = (u32)spurs_ea;
    *(vm_base + base + 0x20) = *(vm_base + se + SPURS_WKL_CURCONT + wid);  /* contention   */
    *(vm_base + base + 0x21) = (u8)w->minContention;                       /* minContention */
    *(vm_base + base + 0x22) = (u8)w->maxContention;                       /* maxContention */
    *(vm_base + base + 0x23) = *(vm_base + se + SPURS_WKL_READY1 + wid);   /* readyCount   */
    *(vm_base + base + 0x24) = *(vm_base + se + SPURS_WKL_IDLE2  + wid);   /* idleSpuRequest */
    u32 sig = vm_read32(se + SPURS_WKL_SIGNAL1) >> 16;
    *(vm_base + base + 0x25) = (sig & (0x8000u >> wid)) ? 1 : 0;           /* hasSignal    */"""

repl_gwi = """    u32 se = (u32)spurs_ea;
    if (wid < 16) {
        *(vm_base + base + 0x20) = *(vm_base + se + SPURS_WKL_CURCONT + wid);  /* contention   */
        *(vm_base + base + 0x21) = (u8)w->minContention;                       /* minContention */
        *(vm_base + base + 0x22) = (u8)w->maxContention;                       /* maxContention */
        *(vm_base + base + 0x23) = *(vm_base + se + SPURS_WKL_READY1 + wid);   /* readyCount   */
        *(vm_base + base + 0x24) = *(vm_base + se + SPURS_WKL_IDLE2  + wid);   /* idleSpuRequest */
        u32 sig = vm_read32(se + SPURS_WKL_SIGNAL1) >> 16;
        *(vm_base + base + 0x25) = (sig & (0x8000u >> wid)) ? 1 : 0;           /* hasSignal    */
    } else {
        *(vm_base + base + 0x20) = (u8)w->curContention;                        /* contention   */
        *(vm_base + base + 0x21) = (u8)w->minContention;                       /* minContention */
        *(vm_base + base + 0x22) = (u8)w->maxContention;                       /* maxContention */
        *(vm_base + base + 0x23) = (u8)w->readyCount;                          /* readyCount   */
        *(vm_base + base + 0x24) = (u8)w->idleSpuRequest;                      /* idleSpuRequest */
        *(vm_base + base + 0x25) = w->signal ? 1 : 0;                          /* hasSignal    */
    }"""

assert target_gwi in text, "target_gwi not found in cellSpurs.c"
text = text.replace(target_gwi, repl_gwi, 1)

with open('ps3recomp/libs/spurs/cellSpurs.c', 'w', encoding='utf-8') as f:
    f.write(text)
print("Updated cellSpurs.c successfully")
