import os

# 1. Update ps3recomp/libs/video/cellGcmSys.c
with open('ps3recomp/libs/video/cellGcmSys.c', 'r', encoding='utf-8') as f:
    gcm = f.read()

# Add gcm_watch_selfjump_ea & gcm_is_watched_selfjump_ea after gcm_io2ea
target_io2ea = """static u32 gcm_io2ea(u32 io)
{
    u32 page = io >> 20;
    if (page >= 65536) return 0;
    u16 ea_page = s_ea_address_table[page];
    if (ea_page == 0xFFFF) return 0;
    return ((u32)ea_page << 20) | (io & 0xFFFFFu);
}"""

repl_io2ea = """static u32 gcm_io2ea(u32 io)
{
    u32 page = io >> 20;
    if (page >= 65536) return 0;
    u16 ea_page = s_ea_address_table[page];
    if (ea_page == 0xFFFF) return 0;
    return ((u32)ea_page << 20) | (io & 0xFFFFFu);
}

static uint32_t s_watched_sj_ea[1024] = { 0x11281AFCu };
static uint32_t s_num_watched_sj = 1;
static SRWLOCK  s_sj_watch_lock = SRWLOCK_INIT;

void gcm_watch_selfjump_ea(uint32_t ea)
{
    if (ea < 0x11280000u || ea >= 0x11A80000u) return;
    AcquireSRWLockExclusive(&s_sj_watch_lock);
    for (uint32_t i = 0; i < s_num_watched_sj; ++i) {
        if (s_watched_sj_ea[i] == ea) {
            ReleaseSRWLockExclusive(&s_sj_watch_lock);
            return;
        }
    }
    if (s_num_watched_sj < 1024) {
        s_watched_sj_ea[s_num_watched_sj++] = ea;
        fprintf(stderr, "[SELFJUMP-WATCH-ADD] Now watching EA 0x%08X (IO 0x%08X) total=%u\\n",
                ea, ea - 0x11000000u, s_num_watched_sj);
    }
    ReleaseSRWLockExclusive(&s_sj_watch_lock);
}

int gcm_is_watched_selfjump_ea(uint32_t ea)
{
    if (ea == 0x11281AFCu) return 1;
    if (ea < 0x11280000u || ea >= 0x11A80000u) return 0;
    if (s_num_watched_sj == 0) return 0;
    AcquireSRWLockShared(&s_sj_watch_lock);
    for (uint32_t i = 0; i < s_num_watched_sj; ++i) {
        if (s_watched_sj_ea[i] == ea) {
            ReleaseSRWLockShared(&s_sj_watch_lock);
            return 1;
        }
    }
    ReleaseSRWLockShared(&s_sj_watch_lock);
    return 0;
}"""

assert target_io2ea in gcm, "target_io2ea not found"
gcm = gcm.replace(target_io2ea, repl_io2ea, 1)

# In gcm_rsx_process_fifo_unlocked: replace self-jump handler
target_sj = """              if (tgt == s_fifo_getoff) {
                  /* Parked. If the title has meanwhile moved `put` somewhere"""

end_marker = """                  why = "parked";
                  break;
              }
              s_fifo_getoff = tgt; }"""

idx_start = gcm.find(target_sj)
assert idx_start != -1, "target_sj not found in cellGcmSys.c"
idx_end = gcm.find(end_marker, idx_start)
assert idx_end != -1, "end_marker not found in cellGcmSys.c"

repl_sj = """              if (tgt == s_fifo_getoff) {
                  u32 cur_ea = gcm_io2ea(s_fifo_getoff);
                  if (cur_ea) {
                      gcm_watch_selfjump_ea(cur_ea);
                  }

                  static u32 s_spin_log = 0;
                  static u32 s_last_spin_off = 0;
                  if (s_fifo_getoff != s_last_spin_off) {
                      s_last_spin_off = s_fifo_getoff;
                      s_spin_log = 0;
                  }
                  if (++s_spin_log <= 15 || (s_spin_log % 500 == 0)) {
                      u32 dl_h = vm_read32(0x009711D4u);
                      u32 dl_t = vm_read32(0x009711D8u);
                      fprintf(stderr, "[FIFO-SELFJUMP-SPIN] spinning at IO 0x%08X (EA 0x%08X w=0x%08X put=0x%08X dl_head=%u dl_tail=%u) #%u\\n",
                              s_fifo_getoff, cur_ea, w, put, dl_h, dl_t, s_spin_log);
                  }
#ifdef _WIN32
                  SwitchToThread();
#endif
                  why = "parked-spin";
                  break;
              }
              if (gcm_is_watched_selfjump_ea(gcm_io2ea(s_fifo_getoff))) {
                  fprintf(stderr, "[FIFO-JUMP-FOLLOWED] IO 0x%08X -> 0x%08X (w=0x%08X put=0x%08X)\\n",
                          s_fifo_getoff, tgt, w, put);
              }
              s_fifo_getoff = tgt; }"""

gcm = gcm[:idx_start] + repl_sj + gcm[idx_end + len(end_marker):]

with open('ps3recomp/libs/video/cellGcmSys.c', 'w', encoding='utf-8') as f:
    f.write(gcm)
print("Updated cellGcmSys.c successfully")

# 2. Update ps3recomp/runtime/ppu/ppu_loader.cpp
with open('ps3recomp/runtime/ppu/ppu_loader.cpp', 'r', encoding='utf-8') as f:
    ppul = f.read()

target_vmw32 = """void vm_write32(uint64_t a, uint32_t v) { barrier_watch_hit((uint32_t)a, v, 4, __builtin_return_address(0)); if (vm_oob((uint32_t)a,4) || vm_null_store((uint32_t)a, v, 4, __builtin_return_address(0))) return;"""

repl_vmw32 = """extern "C" {
int gcm_is_watched_selfjump_ea(uint32_t ea);
void gcm_watch_selfjump_ea(uint32_t ea);
}

static inline void check_selfjump_write_ppu(uint32_t a, uint32_t v, void* ra)
{
    if (a >= 0x11280000u && a < 0x11A80000u) {
        uint32_t old_be = 0;
        if (vm_base) memcpy(&old_be, vm_base + a, 4);
        uint32_t old_val = __builtin_bswap32(old_be);
        uint32_t io = a - 0x11000000u;
        bool is_sj_write = (v == (0x20000000u | io));
        bool old_was_sj  = (old_val == (0x20000000u | io));
        bool is_watched  = (a == 0x11281AFCu) || gcm_is_watched_selfjump_ea(a);

        if (is_sj_write) {
            gcm_watch_selfjump_ea(a);
            is_watched = true;
        }

        if (is_watched || old_was_sj) {
            extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
            uint32_t tid = g_active_ctx ? (uint32_t)g_active_ctx->thread_id : 0;
            uint32_t lr = g_active_ctx ? (uint32_t)g_active_ctx->lr : 0;
            uint32_t pc = g_active_ctx ? (uint32_t)g_active_ctx->pc : ppu_prof_resolve_host(ra);
            fprintf(stderr, "[SELFJUMP-WRITE-PPU] tid=%u pc=0x%08X lr=0x%08X ea=0x%08X (io=0x%08X) old=0x%08X new=0x%08X\\n",
                    tid, pc, lr, a, io, old_val, v);
        }
    }
}

void vm_write32(uint64_t a, uint32_t v) { check_selfjump_write_ppu((uint32_t)a, v, __builtin_return_address(0)); barrier_watch_hit((uint32_t)a, v, 4, __builtin_return_address(0)); if (vm_oob((uint32_t)a,4) || vm_null_store((uint32_t)a, v, 4, __builtin_return_address(0))) return;"""

assert target_vmw32 in ppul, "target_vmw32 not found in ppu_loader.cpp"
ppul = ppul.replace(target_vmw32, repl_vmw32, 1)

target_vmw64 = """void vm_write64(uint64_t a, uint64_t v) {
    /* PPU_WWATCH covers the 64-bit store too. It did not, which made the watch"""

repl_vmw64 = """void vm_write64(uint64_t a, uint64_t v) {
    check_selfjump_write_ppu((uint32_t)a,     (uint32_t)(v >> 32), __builtin_return_address(0));
    check_selfjump_write_ppu((uint32_t)a + 4, (uint32_t)v,         __builtin_return_address(0));
    /* PPU_WWATCH covers the 64-bit store too. It did not, which made the watch"""

assert target_vmw64 in ppul, "target_vmw64 not found in ppu_loader.cpp"
ppul = ppul.replace(target_vmw64, repl_vmw64, 1)

with open('ps3recomp/runtime/ppu/ppu_loader.cpp', 'w', encoding='utf-8') as f:
    f.write(ppul)
print("Updated ppu_loader.cpp successfully")

# 3. Update ps3recomp/runtime/spu/spu_dma.h
with open('ps3recomp/runtime/spu/spu_dma.h', 'r', encoding='utf-8') as f:
    spud = f.read()

target_spu_hook = """        { extern void (*g_spu_put_hook)(uint32_t ea, uint32_t size);
          if (g_spu_put_hook) g_spu_put_hook((uint32_t)ea, size); }"""

repl_spu_hook = """        { extern void (*g_spu_put_hook)(uint32_t ea, uint32_t size);
          if (g_spu_put_hook) g_spu_put_hook((uint32_t)ea, size); }
        /* Watch self-jump writers from SPU */
        if ((uint32_t)ea + size > 0x11280000u && (uint32_t)ea < 0x11A80000u) {
            extern int gcm_is_watched_selfjump_ea(uint32_t ea);
            extern void gcm_watch_selfjump_ea(uint32_t ea);
            for (uint32_t o = 0; o + 4 <= size; o += 4) {
                uint32_t w_ea = (uint32_t)ea + o;
                if (w_ea >= 0x11280000u && w_ea < 0x11A80000u) {
                    uint32_t io = w_ea - 0x11000000u;
                    uint32_t nw_be; memcpy(&nw_be, (const char*)ls_ptr + o, 4);
                    uint32_t nw = ps3_bswap32(nw_be);
                    uint32_t old_be; memcpy(&old_be, (const char*)ea_ptr + o, 4);
                    uint32_t old_val = ps3_bswap32(old_be);
                    bool is_sj_write = (nw == (0x20000000u | io));
                    bool old_was_sj  = (old_val == (0x20000000u | io));
                    bool is_watched  = (w_ea == 0x11281AFCu) || gcm_is_watched_selfjump_ea(w_ea);
                    if (is_sj_write) {
                        gcm_watch_selfjump_ea(w_ea);
                        is_watched = true;
                    }
                    if (is_watched || old_was_sj) {
                        fprintf(stderr, "[SELFJUMP-WRITE-SPU] spu_img=%u spu_pc=0x%05X ea=0x%08X (io=0x%08X) old=0x%08X new=0x%08X\\n",
                                spu->image_id, (uint32_t)spu->pc, w_ea, io, old_val, nw);
                    }
                }
            }
        }"""

assert target_spu_hook in spud, "target_spu_hook not found in spu_dma.h"
spud = spud.replace(target_spu_hook, repl_spu_hook, 1)

with open('ps3recomp/runtime/spu/spu_dma.h', 'w', encoding='utf-8') as f:
    f.write(spud)
print("Updated spu_dma.h successfully")
