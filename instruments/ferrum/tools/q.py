#!/usr/bin/env python3
"""Query the pickled angr CFG (analysis only).

  q.py cfg.pkl refs  <addr>...      functions/instructions referencing a data address
  q.py cfg.pkl func  <addr>...      function containing addr: start, size, callers, callees
  q.py cfg.pkl dec   <addr>...      decompile function containing addr
  q.py cfg.pkl list                 all functions (start size #callers #callees)
"""
import pickle, sys, logging
logging.disable(logging.CRITICAL)

p, cfg = pickle.load(open(sys.argv[1], 'rb'))
kb = cfg.kb
F = kb.functions
cmd, args = sys.argv[2], [int(a, 0) for a in sys.argv[3:]]

def owner(a):
    best = None
    for f in F.values():
        for b in f.block_addrs_set:
            blk = f.get_block(b)
            if b <= a < b + blk.size:
                return f
    return best

def info(f):
    callers = sorted({c for c in kb.callgraph.predecessors(f.addr)}) if f.addr in kb.callgraph else []
    callees = sorted({c for c in kb.callgraph.successors(f.addr)}) if f.addr in kb.callgraph else []
    return f'{f.addr:#x} size={f.size} blocks={len(f.block_addrs_set)} callers={[hex(c) for c in callers][:12]} callees={[hex(c) for c in callees][:30]}'

if cmd == 'list':
    for f in sorted(F.values(), key=lambda f: f.addr):
        n_in = len(list(kb.callgraph.predecessors(f.addr))) if f.addr in kb.callgraph else 0
        n_out = len(list(kb.callgraph.successors(f.addr))) if f.addr in kb.callgraph else 0
        print(f'{f.addr:#x} {f.size} {n_in} {n_out}')
elif cmd == 'refs':
    for a in args:
        for x in kb.xrefs.get_xrefs_by_dst(a):
            f = owner(x.ins_addr) if x.ins_addr else None
            print(f'{a:#x} <- ins {x.ins_addr and hex(x.ins_addr)} func {f and hex(f.addr)}')
elif cmd == 'func':
    for a in args:
        f = owner(a)
        print(info(f) if f else f'{a:#x}: no function')
elif cmd == 'dec':
    for a in args:
        f = F.get(a) or owner(a)
        print(f'// ===== {f.addr:#x} =====\n// {info(f)}')
        try:
            d = p.analyses.Decompiler(f, cfg=cfg.model)
            print(d.codegen.text if d.codegen else '// <no output>')
        except Exception as e:
            print('// decompile failed:', repr(e)[:300])
