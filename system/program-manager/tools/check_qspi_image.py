#!/usr/bin/env python3
"""Check a Daisy Program Manager build (BOOT_QSPI): ELF placement, free SRAM target, BIN.

Usage: check_qspi_image.py [--profile manager|probe] [--version X.Y.Z] image.elf image.bin [image.map]

Checks (docs/CONTRACT.md section 5 and 9):
  * every allocated ELF section lies in an allowed region; nothing (VMA or LMA, incl.
    NOLOAD) intersects the image target 0x24000000..0x24078000;
  * code/rodata/vectors and the .data load address are in QSPI from 0x90040000 and
    end below 0x907E0000 (Mod on Daisy storage reservation);
  * .data VMA, .bss and the stack are in DTCM; the initial stack pointer is _estack;
  * .pm_axi_bounce (if present) lies in the 32 KiB helper region 0x24078000..0x24080000
    and is 32-byte aligned;
  * the .qspiflash_* sections are empty;
  * the vector table: SP in DTCM, reset vector Thumb and inside the QSPI image;
  * the BIN is exactly the QSPI extent of the ELF (first to last loaded byte) and starts
    with the vector table; the embedded version marker matches --version;
  * prints the SHA-256 of the BIN and a region usage summary.
Exit 0 on PASS, 1 on any failure. This replaces nothing on hardware.
"""
import argparse
import hashlib
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from pmtools import Elf32, SHF_ALLOC, SHT_NOBITS  # noqa: E402

QSPI_START = 0x90040000
QSPI_LIMIT = 0x907E0000
TARGET_START = 0x24000000
TARGET_END = 0x24078000
HELPER_START = 0x24078000
HELPER_END = 0x24080000
DTCM_START = 0x20000000
DTCM_END = 0x20020000
D2_START = 0x30000000
D2_END = 0x30048000
D3_START = 0x38000000
D3_END = 0x38010000
BACKUP_START = 0x38800000
BACKUP_END = 0x38801000
SDRAM_START = 0xC0000000
SDRAM_END = 0xC4000000
ITCM_START = 0x00000000
ITCM_END = 0x00010000

REGIONS = [
    ("QSPI", QSPI_START, QSPI_LIMIT),
    ("SRAM_HELPER", HELPER_START, HELPER_END),
    ("DTCM", DTCM_START, DTCM_END),
    ("D2", D2_START, D2_END),
    ("D3", D3_START, D3_END),
    ("BACKUP", BACKUP_START, BACKUP_END),
    ("SDRAM", SDRAM_START, SDRAM_END),
    ("ITCM", ITCM_START, ITCM_END),
]


def region_of(start, end):
    for name, lo, hi in REGIONS:
        if lo <= start and end <= hi:
            return name
    return None


def intersects(a0, a1, b0, b1):
    return a0 < b1 and b0 < a1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", choices=("manager", "probe"), default="manager")
    ap.add_argument("--version", default=None, help="expected PROGRAM_MANAGER_VERSION in the BIN marker")
    ap.add_argument("elf", type=pathlib.Path)
    ap.add_argument("bin", type=pathlib.Path)
    ap.add_argument("map", type=pathlib.Path, nargs="?")
    args = ap.parse_args()

    failures = []

    def fail(msg):
        failures.append(msg)
        print("FAIL:", msg)

    elf = Elf32(args.elf.read_bytes())
    image = args.bin.read_bytes()
    usage = {name: 0 for name, _, _ in REGIONS}
    qspi_lo, qspi_hi = None, 0
    alloc = [s for s in elf.sections if s["flags"] & SHF_ALLOC and s["size"] > 0]
    for s in alloc:
        vma0, vma1 = s["addr"], s["addr"] + s["size"]
        lma0 = elf.lma(s) if s["type"] != SHT_NOBITS else vma0
        lma1 = lma0 + s["size"]
        spans = [("VMA", vma0, vma1)]
        if lma0 != vma0:
            spans.append(("LMA", lma0, lma1))
        for kind, a0, a1 in spans:
            if intersects(a0, a1, TARGET_START, TARGET_END):
                fail(f"{s['name']} {kind} {a0:#x}-{a1:#x} intersects the SRAM image target")
            reg = region_of(a0, a1)
            if reg is None:
                fail(f"{s['name']} {kind} {a0:#x}-{a1:#x} is outside every allowed region")
            elif kind == "VMA" or reg == "QSPI":
                usage[reg] += s["size"]
            if reg == "QSPI":
                qspi_lo = a0 if qspi_lo is None else min(qspi_lo, a0)
                qspi_hi = max(qspi_hi, a1)
        if s["name"].startswith(".qspiflash"):
            fail(f"{s['name']} must be empty ({s['size']} bytes)")
        if s["name"] == ".pm_axi_bounce":
            if not (HELPER_START <= vma0 and vma1 <= HELPER_END):
                fail(f".pm_axi_bounce {vma0:#x}-{vma1:#x} outside the AXI helper region")
            if vma0 % 32 or s["size"] % 32:
                fail(".pm_axi_bounce is not a whole number of 32-byte cache lines")
        if s["name"] == ".isr_vector" and vma0 != QSPI_START:
            fail(f".isr_vector at {vma0:#x}, expected {QSPI_START:#x}")
        if s["name"] == ".data":
            if not (DTCM_START <= vma0 and vma1 <= DTCM_END):
                fail(f".data VMA {vma0:#x}-{vma1:#x} not in DTCM")
            if not (QSPI_START <= lma0 and lma1 <= QSPI_LIMIT):
                fail(f".data LMA {lma0:#x}-{lma1:#x} not in QSPI")
        if s["name"] in (".bss", ".reserved_for_stack") and not (DTCM_START <= vma0 and vma1 <= DTCM_END):
            fail(f"{s['name']} {vma0:#x}-{vma1:#x} not in DTCM")
    if qspi_lo is None:
        fail("no QSPI sections found")
    elif qspi_hi > QSPI_LIMIT:
        fail(f"QSPI image ends at {qspi_hi:#x}, above the limit {QSPI_LIMIT:#x}")

    syms = elf.symbols()
    estack = syms.get("_estack")
    if estack is None:
        print("note: no symbol table, stack symbols not checked")
    elif estack != DTCM_END:
        fail(f"_estack is {estack:#x}, expected {DTCM_END:#x}")
    stack_end = syms.get("__reserved_for_stack_end__")
    if stack_end is not None and estack is not None and estack - stack_end < 16 * 1024:
        fail(f"only {estack - stack_end} bytes between the stack reservation and _estack")

    # BIN: exact QSPI extent, vectors, marker.
    if qspi_lo is not None:
        if len(image) != qspi_hi - qspi_lo:
            fail(f"BIN size {len(image)} != QSPI extent {qspi_hi - qspi_lo} ({qspi_lo:#x}-{qspi_hi:#x})")
        if qspi_lo != QSPI_START:
            fail(f"first QSPI byte at {qspi_lo:#x}, expected {QSPI_START:#x}")
    if len(image) < 8:
        fail("BIN too small for a vector table")
    else:
        sp, reset = struct.unpack_from("<II", image)
        if not (DTCM_START < sp <= DTCM_END) or sp % 8:
            fail(f"initial SP {sp:#x} not in DTCM / not 8-byte aligned")
        if not reset & 1:
            fail("reset vector is not Thumb")
        entry = reset & ~1
        if not (QSPI_START <= entry < QSPI_START + len(image)):
            fail(f"reset vector {entry:#x} outside the QSPI image")
        if elf.e_entry & ~1 != entry:
            fail(f"ELF entry {elf.e_entry:#x} differs from the reset vector {reset:#x}")
    markers = re.findall(rb"PROGRAM-MANAGER-FW-VERSION=([0-9A-Za-z.\-]+)\x00", image)
    if len(markers) != 1:
        fail(f"expected exactly one version marker, found {markers}")
    elif args.version is not None and markers[0].decode() != args.version:
        fail(f"marker version {markers[0].decode()} != {args.version}")

    if args.map and args.map.exists():
        text = args.map.read_text(errors="replace")
        if "program_manager_qspi.lds" not in text and "qspi" not in text.lower():
            print("note: map does not mention the QSPI linker script")

    sha = hashlib.sha256(image).hexdigest()
    print(f"profile={args.profile} bin={args.bin.name} bytes={len(image)} sha256={sha}")
    for name, lo, hi in REGIONS:
        if usage[name]:
            print(f"  {name:<12} {usage[name]:>8} / {hi - lo:>8} bytes")
    if failures:
        print(f"FAIL: {len(failures)} problem(s)")
        return 1
    print("PASS: BOOT_QSPI placement, free SRAM target, vectors, marker")
    return 0


if __name__ == "__main__":
    sys.exit(main())
