#!/usr/bin/env python3
"""Reproducible evidence-only inspection of Qu-Bit Stardust v1.1.1.

Run: python stardust_static_analysis.py Stardust_v1_1_1.bin -o evidence.json
No third-party dependencies; never writes to the input file, and never executes it.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import re
import struct
from collections import Counter
from pathlib import Path

ORIGIN = 0x24000000


def extract(path: Path) -> dict:
    blob=path.read_bytes()
    if len(blob)<0x200:
        raise ValueError('Not enough bytes for a Cortex-M vector table')
    word=lambda p: struct.unpack_from('<I',blob,p)[0]
    sp,reset=word(0),word(4)
    vars_data=[word(o) for o in (0x18c0,0x18c4,0x18c8,0x18cc,0x18d0)] if len(blob)>0x18d4 else []
    evidence={
        'file':path.name,
        'length_bytes':len(blob),
        'sha256':hashlib.sha256(blob).hexdigest(),
        'vector_table':{'stack_pointer':hex(sp),'reset_vector':hex(reset),'reset_offset':hex((reset & ~1)-ORIGIN),
                        'thumb_bit_set':bool(reset&1),
                        'stack_in_DTCM':0x20000000<=sp<=0x20020000,
                        'reset_within_image':0<=((reset&~1)-ORIGIN)<len(blob)},
        'startup_literal_pool': {},
        'strings':[],
        'entropy_windows':[],
        'notes': [
           'ELF/Thumb execution origin inferred from reset vector and startup code; not the file format.',
           'Raw 32-bit word matches must NOT be treated as pointers without instruction analysis; float32 constants can match SDRAM-address ranges.',
           'No decompilation, hardware execution, or audio DSP behavioral testing is performed.',
        ],
    }
    if vars_data:
        s,e,source,zs,ze=vars_data
        evidence['startup_literal_pool']={
           'data_destination_start':hex(s),'data_destination_end':hex(e),
           'data_source_start':hex(source),
           'bss_start':hex(zs),'bss_end':hex(ze),
           'data_bytes':e-s,'bss_bytes':ze-zs,
           'data_source_inside_image':0<=source-ORIGIN<len(blob),
           'data_source_end_inside_image':0<=source-ORIGIN+(e-s)<=len(blob),
        }
    for match in re.finditer(rb'[\x20-\x7e]{5,}\x00',blob):
        txt=match.group()[:-1].decode('ascii')
        if match.start()>=214000:
            evidence['strings'].append({'file_offset':hex(match.start()),'value':txt})
    for start in range(0,len(blob),4096):
        part=blob[start:start+4096];cnt=Counter(part)
        ent=-sum((n/len(part))*math.log2(n/len(part)) for n in cnt.values())
        evidence['entropy_windows'].append({'file_offset':hex(start),'entropy_bits_per_byte':round(ent,3)})
    return evidence


def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary',type=Path)
    p.add_argument('-o','--output',type=Path,default=Path('stardust_evidence.json'))
    args=p.parse_args()
    info=extract(args.binary)
    args.output.write_text(json.dumps(info,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print('SHA256',info['sha256'])
    print('Vectors',info['vector_table'])
    print('Startup RAM',info['startup_literal_pool'])
    print('String matches',len(info['strings']))
    print('Written',args.output)

if __name__=='__main__':main()
