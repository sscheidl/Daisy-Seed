#!/usr/bin/env python3
"""Wrap an unmodified ARM Cortex-M raw BIN in an analysis-only ELF container.

Run: python stardust_wrap_elf.py Stardust_v1_1_1.bin -o analysis_only.elf
     llvm-objdump -d --triple=thumbv7em --mcpu=cortex-m7 \
       --start-address=0x24001890 --stop-address=0x240018c0 analysis_only.elf

DO NOT FLASH THE RESULTING ELF. It is not an original build or a firmware port.
"""
from pathlib import Path
import argparse
import struct

BASE=0x24000000

def wrap(raw:bytes) ->bytes:
    shnames=b'\x00.text\x00.symtab\x00.strtab\x00.shstrtab\x00'
    strtab=b'\x00$t\x00Reset_Handler\x00Default_Handler\x00'
    symbols=[(0,0,0,0,0,0),
             (strtab.index(b'$t'),BASE+0x1890,0,0,0,1),
             (strtab.index(b'Reset_Handler'),BASE+0x1894,0,18,0,1),
             (strtab.index(b'Default_Handler'),BASE+0x1890,0,18,0,1)]
    syms=b''.join(struct.pack('<IIIBBH',*s) for s in symbols)
    content=bytearray(b'\0'*0x100)
    textoff=len(content);content.extend(raw)
    def add(data,alignment=4):
        while len(content)%alignment:content.append(0)
        pos=len(content);content.extend(data);return pos
    symoff=add(syms);stroff=add(strtab,1);shstroff=add(shnames,1)
    while len(content)%4:content.append(0)
    shoff=len(content)
    headers=[(0,0,0,0,0,0,0,0,0,0),
             (shnames.index(b'.text'),1,6,BASE,textoff,len(raw),0,0,4,0),
             (shnames.index(b'.symtab'),2,0,0,symoff,len(syms),3,2,4,16),
             (shnames.index(b'.strtab'),3,0,0,stroff,len(strtab),0,0,1,0),
             (shnames.index(b'.shstrtab'),3,0,0,shstroff,len(shnames),0,0,1,0)]
    for header in headers:content.extend(struct.pack('<IIIIIIIIII',*header))
    magic=b'\x7fELF'+bytes([1,1,1,0,0])+b'\0'*7
    head=struct.pack('<16sHHIIIIIHHHHHH',magic,2,40,1,BASE+0x1894,0,shoff,0x05000000,52,0,0,40,len(headers),4)
    content[:len(head)]=head
    return bytes(content)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('binary',type=Path)
    ap.add_argument('-o','--output',type=Path,default=Path('stardust_analysis_only.elf'))
    args=ap.parse_args()
    args.output.write_bytes(wrap(args.binary.read_bytes()))
    print('Analysis-only ELF:',args.output,'(DO NOT FLASH)')

if __name__=='__main__':main()
