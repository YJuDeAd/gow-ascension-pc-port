"""tools/spbalance.py — Static stack-pointer balance auditor for God of War: Ascension.

Walks lifted PPU logical functions, tracking cumulative stack allocations and
verifying that every real-return path restores what the entry allocated.
"""
import glob, os, re, sys, collections

ALLOC = re.compile(r'ctx->gpr\[1\] \+= -(0x[0-9A-Fa-f]+);')
REST  = re.compile(r'ctx->gpr\[1\] = ctx->gpr\[1\] \+ \(int64_t\)\((0x[0-9A-Fa-f]+)\);|ctx->gpr\[1\] = \(int64_t\)\(int32_t\)\(ctx->gpr\[1\] \+ (0x[0-9A-Fa-f]+)\)')
SIG   = re.compile(r'^void (func_[0-9A-Fa-f]{8})\(ppu_context\* ctx\) \{')
TRAMP = re.compile(r'g_trampoline_fn = \(void\(\*\)\(void\*\)\)(func_[0-9A-Fa-f]{8})')

def parse(d):
    funcs = {}
    for path in sorted(glob.glob(os.path.join(d, 'ppu_recomp_*.cpp'))):
        cur = None
        lines = []
        def flush(name, body):
            alloc = 0
            for m in ALLOC.finditer('\n'.join(body)):
                alloc += int(m.group(1), 16)
            tramps = set(TRAMP.findall('\n'.join(body)))
            real = []
            last_rest = 0
            for ln in body:
                rm = REST.search(ln)
                if rm:
                    val = rm.group(1) or rm.group(2)
                    last_rest = int(val, 16)
                if re.search(r'\breturn;', ln) and 'g_trampoline_fn' not in ln:
                    real.append(last_rest)
                    last_rest = 0
            funcs[name] = {'alloc': alloc, 'real_restores': real, 'tramps': tramps,
                           'has_real_ret': len(real) > 0}

        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = SIG.match(line)
                if m:
                    if cur: flush(cur, lines)
                    cur = m.group(1)
                    lines = [line]
                elif cur:
                    lines.append(line)
            if cur: flush(cur, lines)
    return funcs

def audit(d, focus=None):
    funcs = parse(d)
    with_prologue = [k for k, v in funcs.items() if v['alloc'] > 0]
    print(f"parsed {len(funcs)} functions; {len(with_prologue)} with a prologue")

    imbalanced = []
    for entry in with_prologue:
        if focus and entry != focus:
            continue
        alloc = funcs[entry]['alloc']
        visited = set()
        queue = collections.deque([entry])
        bad_returns = []
        while queue:
            node = queue.popleft()
            if node in visited or node not in funcs:
                continue
            visited.add(node)
            fn = funcs[node]
            for rest in fn['real_restores']:
                if rest != alloc:
                    bad_returns.append((node, rest))
            for nxt in fn['tramps']:
                if nxt not in visited:
                    queue.append(nxt)
        if bad_returns:
            imbalanced.append((entry, alloc, bad_returns, len(visited)))

    print(f"IMBALANCED logical functions (restore != entry alloc): {len(imbalanced)}")
    for entry, alloc, bads, size in imbalanced[:20]:
        for node, rest in bads:
            print(f"  {entry}: alloc=0x{alloc:X} but {node} returns restoring 0x{rest:X} (leak 0x{alloc-rest:X}); chain size {size}")

    return len(imbalanced)

if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else 'src/gen'
    focus = None
    if '--focus' in sys.argv:
        idx = sys.argv.index('--focus')
        focus = sys.argv[idx + 1]
    ret = audit(d, focus)
    sys.exit(0 if ret == 0 else 1)
