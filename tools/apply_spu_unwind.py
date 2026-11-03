import sys
import subprocess

# Restore from git first
subprocess.run(['git', '-C', 'ps3recomp', 'checkout', 'runtime/spu/spu_drain.c'], check=True)

with open('ps3recomp/runtime/spu/spu_drain.c', 'r', encoding='utf-8') as f:
    code = f.read()

# Normalize line endings to \n
code = code.replace('\r\n', '\n')

decl = '''typedef struct spu_drain_frame {
    jmp_buf                  env;
    struct spu_drain_frame*  prev;
    uint32_t                 return_pc;
    uint32_t                 depth;
    int                      image_id;
    uint32_t                 entry_pc;
} spu_drain_frame;

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static size_t get_thread_committed_stack_kb(void) {
    MEMORY_BASIC_INFORMATION mbi;
    char dummy;
    if (!VirtualQuery(&dummy, &mbi, sizeof(mbi))) return 0;
    char* p = (char*)mbi.AllocationBase;
    size_t committed = 0;
    while (VirtualQuery(p, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT) committed += mbi.RegionSize;
        else if (mbi.State == MEM_FREE) break;
        p += mbi.RegionSize;
        if (p - (char*)mbi.AllocationBase > 32 * 1024 * 1024) break;
    }
    return committed / 1024;
}
#else
static size_t get_thread_committed_stack_kb(void) { return 0; }
#endif

static void spu_lane_telemetry_step(spu_context* ctx)
{
    extern unsigned long long ps3_qpc_us(void);
    static _Thread_local unsigned long long t_last_report = 0;
    const unsigned long long now = ps3_qpc_us();
    if (now - t_last_report < 5000000ull) return;
    t_last_report = now;

    unsigned lane = (unsigned)(ctx->spu_id & 7u);
    size_t stack_kb = get_thread_committed_stack_kb();

    struct { uint32_t pc; unsigned count; } top[5] = {0};
    for (spu_drain_frame* f = (spu_drain_frame*)ctx->drain_frame; f; f = f->prev) {
        uint32_t pc = f->entry_pc;
        int found = 0;
        for (int i = 0; i < 5; i++) {
            if (top[i].pc == pc) { top[i].count++; found = 1; break; }
        }
        if (!found) {
            for (int i = 0; i < 5; i++) {
                if (top[i].count == 0) { top[i].pc = pc; top[i].count = 1; found = 1; break; }
            }
        }
    }
    fprintf(stderr, "[spu-lane %u] host_depth=%u committed_stack=%zu KB top unreturned:",
            lane, ctx->host_depth, stack_kb);
    for (int i = 0; i < 5 && top[i].count > 0; i++) {
        fprintf(stderr, " 0x%05X(%u)", top[i].pc, top[i].count);
    }
    fprintf(stderr, "\\n");
    fflush(stderr);
}
'''

guard_marker = '/* See the declaration in spu_context.h. */\nvoid spu_depth_guard(spu_context* ctx)'
assert guard_marker in code, 'guard_marker not found'
code = code.replace(guard_marker, decl + '\n' + guard_marker)

old_guard_body = '''    if (ctx->host_depth < s_max) return;
    static int s_reported = 0;
    if (s_reported < 1) {   /* one report: five SPU threads all trip together */
        s_reported++;
        fprintf(stderr, "[spu-depth] img=%d pc=0x%05X lr=0x%05X host_depth=%u"
                        " -- lifted call recursion, halting the SPU\\n",
                ctx->image_id, (uint32_t)ctx->pc & SPU_LS_MASK,
                ctx->gpr[0]._u32[0] & SPU_LS_MASK, ctx->host_depth);
        fprintf(stderr, "[spu-depth] recent drain sites (oldest first):");
        uint32_t start = (s_n > RING) ? (s_n - RING) : 0;
        for (uint32_t k = start; k < s_n; k++)
            fprintf(stderr, " %05X", s_ring[k % RING]);
        fprintf(stderr, "\\n");
        fflush(stderr);
    }
    { extern void spu_halt(spu_context*); spu_halt(ctx); }'''

new_guard_body = '''    if (ctx->host_depth < s_max) return;
    static int s_reported = 0;
    if (s_reported < 1) {   /* one report: five SPU threads all trip together */
        s_reported++;
        fprintf(stderr, "[spu-depth] img=%d pc=0x%05X lr=0x%05X host_depth=%u"
                        " -- lifted call recursion, halting the SPU\\n",
                ctx->image_id, (uint32_t)ctx->pc & SPU_LS_MASK,
                ctx->gpr[0]._u32[0] & SPU_LS_MASK, ctx->host_depth);
        fprintf(stderr, "[spu-depth] recent drain sites (oldest first):");
        uint32_t start = (s_n > RING) ? (s_n - RING) : 0;
        for (uint32_t k = start; k < s_n; k++)
            fprintf(stderr, " %05X", s_ring[k % RING]);
        fprintf(stderr, "\\n");
        fprintf(stderr, "[spu-depth-stack] SPU %u (img=%d) host_depth=%u shadow frames:\\n",
                (unsigned)(ctx->spu_id & 7u), ctx->image_id, ctx->host_depth);
        for (spu_drain_frame* f = (spu_drain_frame*)ctx->drain_frame; f; f = f->prev) {
            fprintf(stderr, "  depth=%u entry_pc=0x%05X return_pc=0x%05X img=%d\\n",
                    f->depth, f->entry_pc, f->return_pc, f->image_id);
        }
        fflush(stderr);
    }
    { extern void spu_halt(spu_context*); spu_halt(ctx); }'''

assert old_guard_body in code, 'old_guard_body not found'
code = code.replace(old_guard_body, new_guard_body)
code = code.replace('s_max = e ? (uint32_t)strtoul(e, 0, 0) : 2000u;', 's_max = e ? (uint32_t)strtoul(e, 0, 0) : 50u;')

target_loop_start = 'static void spu_drain_loop(spu_context* ctx, uint32_t return_pc, int any_return)'
loop_start_idx = code.find(target_loop_start)
loop_end_marker = 'void spu_drain_call(spu_context* ctx, uint32_t return_pc)'
loop_end_idx = code.find(loop_end_marker)
assert loop_start_idx != -1 and loop_end_idx != -1, 'loop indices not found'

new_drain_loop = '''static void spu_drain_loop(spu_context* ctx, uint32_t return_pc, int any_return)
{
    const int outer_anyret = g_spu_drain_anyret;
    if (any_return) g_spu_drain_anyret = ctx->host_depth;
    spu_depth_guard(ctx);
    spu_lane_telemetry_step(ctx);

    spu_drain_frame df;
    df.prev = (spu_drain_frame*)ctx->drain_frame;
    df.return_pc = return_pc & SPU_LS_MASK;
    df.depth = ctx->host_depth;
    df.image_id = ctx->image_id;
    df.entry_pc = (uint32_t)ctx->pc & SPU_LS_MASK;
    ctx->drain_frame = &df;

    spu_irq_frame f;
    f.prev = 0; f.depth = 0; f.image_id = 0;
#define SPU_DRAIN_POP_IRQ() do { \\
    if (ctx->irq_frame == &f) ctx->irq_frame = f.prev; \\
    ctx->drain_ret_pc = 0; \\
    ctx->drain_frame = df.prev; \\
} while (0)

    int jmprc = setjmp(df.env);
    if (jmprc == 1) {
        ctx->host_depth = df.depth;
        SPU_DRAIN_POP_IRQ();
        return;
    } else if (jmprc == 2) {
        ctx->host_depth = df.depth;
        g_spu_trampoline_fn = spu_indirect_branch;
    } else {
        if (df.entry_pc != 0 && df.return_pc != 0) {
            for (spu_drain_frame* anc = df.prev; anc; anc = anc->prev) {
                if (anc->entry_pc == df.entry_pc && anc->return_pc == df.return_pc) {
                    static int s_uw = 0;
                    if (s_uw++ < 20) {
                        fprintf(stderr, "[spu-unwind] Repeating call entry=0x%05X ret=0x%05X unwinds from depth %u to outer depth %u\\n",
                                df.entry_pc, df.return_pc, ctx->host_depth, anc->depth);
                    }
                    ctx->drain_frame = anc;
                    longjmp(anc->env, 2);
                }
            }
        }
    }

    for (;;) {
        while (g_spu_trampoline_fn) {
            uint32_t target_pc = ctx->pc & SPU_LS_MASK;
            if (target_pc == (return_pc & SPU_LS_MASK)) {
                g_spu_trampoline_fn = 0;
                SPU_DRAIN_POP_IRQ();
                return;
            }
            for (spu_drain_frame* anc = df.prev; anc; anc = anc->prev) {
                if (anc->return_pc == target_pc) {
                    static int s_uw = 0;
                    if (s_uw++ < 20) {
                        fprintf(stderr, "[spu-unwind] Trampoline return to 0x%05X unwinds from depth %u to %u\\n",
                                target_pc, ctx->host_depth, anc->depth);
                    }
                    g_spu_trampoline_fn = 0;
                    ctx->drain_frame = anc;
                    longjmp(anc->env, 1);
                }
            }
            void (*fn)(spu_context*) = g_spu_trampoline_fn;
            g_spu_trampoline_fn = 0;
            ctx->drain_ret_pc = return_pc & SPU_LS_MASK;
            { extern volatile int g_yz_lockstep_on;
              if (g_yz_lockstep_on) yz_lockstep_tick(ctx); }
            spu_task_launch_check(ctx, (void*)fn);
            if (ctx->int_enable && (ctx->event_status & ctx->event_mask)) {
                void (*vf)(spu_context*) = spu_take_interrupt(ctx, fn);
                if (!ctx->int_enable) {
                    if (ctx->irq_frame != &f) {
                        f.prev = (spu_irq_frame*)ctx->irq_frame;
                        f.depth = ctx->host_depth;
                        f.image_id = ctx->image_id;
                        ctx->irq_frame = &f;
                    }
                    if (setjmp(f.env) == 0) {
                        vf(ctx);
                    } else {
                        ctx->host_depth = f.depth;
                        ctx->image_id = f.image_id;
                        g_spu_trampoline_fn = spu_indirect_branch;
                        SPU_DRAIN_POP_IRQ();
                    }
                    continue;
                }
            }
            fn(ctx);
        }
        {
            uint32_t target_pc = ctx->pc & SPU_LS_MASK;
            if (target_pc == (return_pc & SPU_LS_MASK)) {
                SPU_DRAIN_POP_IRQ();
                return;
            }
            for (spu_drain_frame* anc = df.prev; anc; anc = anc->prev) {
                if (anc->return_pc == target_pc) {
                    static int s_uw = 0;
                    if (s_uw++ < 20) {
                        fprintf(stderr, "[spu-unwind] C return to 0x%05X unwinds from depth %u to %u\\n",
                                target_pc, ctx->host_depth, anc->depth);
                    }
                    ctx->drain_frame = anc;
                    longjmp(anc->env, 1);
                }
            }
        }
        if (any_return) {
            SPU_DRAIN_POP_IRQ();
            return;
        }
        {
            uint32_t pc = ctx->pc & SPU_LS_MASK;
            spu_drain_fn fn = ctx->resident_ovl ? spu_lookup(pc, ctx->resident_ovl) : 0;
            if (!fn) fn = spu_lookup(pc, ctx->image_id);
            if (fn) {
                { static int _n = 0; if (PS3_LOG_FIRST(_n, 8))
                    fprintf(stderr, "[spu] drain-resume return_pc=0x%05X at lifted entry 0x%05X img=%d depth=%d ovl=%d\\n",
                            (unsigned)(return_pc & SPU_LS_MASK), pc, ctx->image_id, ctx->host_depth,
                            (int)ctx->resident_ovl); }
                g_spu_trampoline_fn = spu_indirect_branch;
                continue;
            }
        }
        {
            extern void spu_restart_dispatch(spu_context*);
            SPU_DRAIN_POP_IRQ();
            spu_restart_dispatch(ctx);
            return;
        }
    }
    SPU_DRAIN_POP_IRQ();
    g_spu_drain_anyret = outer_anyret;
#undef SPU_DRAIN_POP_IRQ
}

'''

code = code[:loop_start_idx] + new_drain_loop + code[loop_end_idx:]

with open('ps3recomp/runtime/spu/spu_drain.c', 'w', encoding='utf-8') as f:
    f.write(code)

print('Updated spu_drain.c successfully!')
