#!/usr/bin/env python3
"""Recover a CFG of the raw Stardust image with angr and pickle the project (analysis only)."""
import pickle, sys, time, logging
import angr

logging.getLogger('angr').setLevel(logging.ERROR)
logging.getLogger('cle').setLevel(logging.ERROR)
logging.getLogger('pyvex').setLevel(logging.ERROR)

BASE = 0x24000000
img, out = sys.argv[1], sys.argv[2]
p = angr.Project(img, main_opts={'backend': 'blob', 'arch': 'ARMCortexM',
                                 'base_addr': BASE, 'entry_point': 0x24001895},
                 auto_load_libs=False)
# Vector table entries are function starts too.
import struct
blob = open(img, 'rb').read()
vec = [struct.unpack_from('<I', blob, 4 * i)[0] for i in range(1, 166)]
starts = sorted({v for v in vec if BASE <= (v & ~1) < BASE + len(blob)})
t = time.time()
cfg = p.analyses.CFGFast(normalize=True, data_references=True, cross_references=True,
                         function_starts=starts, force_complete_scan=True,
                         resolve_indirect_jumps=True)
print('cfg', round(time.time() - t, 1), 's; functions', len(cfg.kb.functions))
pickle.dump((p, cfg), open(out, 'wb'))
