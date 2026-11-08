#!/usr/bin/env python3
"""Check a BOOT_SRAM firmware (ELF + BIN) against the image profile axi-sram-480k-v1 and
package it as a .dsy file for the Daisy Program Manager.

  dsy_pack.py check --elf X.elf --bin X.bin [--noload NAME ...] [--map X.map]
  dsy_pack.py pack  --elf X.elf --bin X.bin --out DIR_OR_FILE.dsy
                    (--project ID | --product ID --version V) [--noload NAME ...] [--report R.json]
  dsy_pack.py info  FILE.dsy

Checks (docs/PACKAGE.md section 3):
  * .isr_vector at 0x24000000; every section with file content is loaded inside
    0x24000000..0x24078000; .data lives in DTCM with its load address in that image;
    .bss and the stack in DTCM; nothing in internal flash or QSPI (.qspiflash_* empty);
  * NOLOAD sections inside the image target (runtime buffers such as .axi_bss) must
    end below 0x24078000 AND be named with --noload: the product documents that it
    initializes them itself (the loader zeroes the whole target before copying);
  * the BIN is exactly the loaded extent, 8..491520 bytes, SP in DTCM, Thumb reset
    vector inside the image, ELF entry == reset vector;
  * with --project: product id/version from components/registry.json, the BIN file
    name must be <image>-v<version>.bin and the version marker must be embedded once.
Only local files are read and written; nothing talks to a device or a card.
"""
import argparse
import hashlib
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pmtools  # noqa: E402
from pmtools import PackageError, Elf32, SHT_NOBITS  # noqa: E402

FLASH_START, FLASH_END = 0x08000000, 0x08020000
QSPI_START, QSPI_END = 0x90000000, 0x90800000
ALLOWED_RAM = [
    ("DTCM", pmtools.DTCM_START, pmtools.DTCM_END),
    ("D2", 0x30000000, 0x30048000),
    ("D3", 0x38000000, 0x38010000),
    ("BACKUP", 0x38800000, 0x38801000),
    ("SDRAM", 0xC0000000, 0xC4000000),
    ("ITCM", 0x00000000, 0x00010000),
]


def region_name(a0, a1):
    if pmtools.TARGET_BASE <= a0 and a1 <= pmtools.TARGET_END:
        return "TARGET"
    if pmtools.HELPER_START <= a0 and a1 <= pmtools.HELPER_END:
        return "AXI_HELPER"
    for name, lo, hi in ALLOWED_RAM:
        if lo <= a0 and a1 <= hi:
            return name
    if FLASH_START <= a0 < FLASH_END:
        return "FLASH"
    if QSPI_START <= a0 < QSPI_END:
        return "QSPI"
    return None


def check_image(elf_bytes, image, allowed_noload=(), marker=None, version=None):
    """Returns a report dict; raises PackageError on the first violation."""
    elf = Elf32(elf_bytes)
    report = {"sections": [], "noload_in_target": [], "warnings": []}
    load_lo, load_hi = None, 0
    for s in elf.allocated():
        vma0, vma1 = s["addr"], s["addr"] + s["size"]
        nobits = s["type"] == SHT_NOBITS
        lma0 = vma0 if nobits else elf.lma(s)
        lma1 = lma0 + s["size"]
        entry = {"name": s["name"], "vma": f"{vma0:#x}", "lma": f"{lma0:#x}", "size": s["size"],
                 "noload": nobits, "region": region_name(vma0, vma1)}
        report["sections"].append(entry)
        if s["name"].startswith(".qspiflash"):
            raise PackageError("QSPISECTION", f"{s['name']} is not empty ({s['size']} bytes): external QSPI dependency")
        if entry["region"] in ("FLASH", "QSPI") or region_name(lma0, lma1) in ("FLASH", "QSPI"):
            raise PackageError("REGION", f"{s['name']} at {vma0:#x} (load {lma0:#x}) lies in flash/QSPI")
        if entry["region"] is None:
            raise PackageError("REGION", f"{s['name']} {vma0:#x}-{vma1:#x} outside every known region")
        if not nobits:
            if not (pmtools.TARGET_BASE <= lma0 and lma1 <= pmtools.TARGET_END):
                raise PackageError("LMA", f"{s['name']} load address {lma0:#x}-{lma1:#x} outside the image target")
            load_lo = lma0 if load_lo is None else min(load_lo, lma0)
            load_hi = max(load_hi, lma1)
        elif entry["region"] == "TARGET":
            report["noload_in_target"].append(s["name"])
            if s["name"] not in allowed_noload:
                raise PackageError("NOLOAD", f"{s['name']} is a NOLOAD section inside the image target "
                                             f"({vma0:#x}-{vma1:#x}); name it with --noload if the product initializes it")
        if s["name"] == ".isr_vector" and vma0 != pmtools.TARGET_BASE:
            raise PackageError("VECTORS", f".isr_vector at {vma0:#x}, expected {pmtools.TARGET_BASE:#x}")
        if s["name"] == ".data" and not (pmtools.DTCM_START <= vma0 and vma1 <= pmtools.DTCM_END):
            raise PackageError("DATA", f".data VMA {vma0:#x}-{vma1:#x} not in DTCM")
        if s["name"] in (".bss", ".reserved_for_stack") and not (pmtools.DTCM_START <= vma0 and vma1 <= pmtools.DTCM_END):
            raise PackageError("BSS", f"{s['name']} {vma0:#x}-{vma1:#x} not in DTCM")
    if load_lo is None:
        raise PackageError("EMPTY", "no loaded sections")
    if load_lo != pmtools.TARGET_BASE:
        raise PackageError("VECTORS", f"first loaded byte at {load_lo:#x}, expected {pmtools.TARGET_BASE:#x}")
    extent = load_hi - load_lo
    if len(image) != extent:
        raise PackageError("BINSIZE", f"BIN has {len(image)} bytes, ELF load extent is {extent}")
    if not pmtools.MIN_PAYLOAD <= len(image) <= pmtools.TARGET_SIZE:
        raise PackageError("LENGTH", f"image of {len(image)} bytes outside 8..{pmtools.TARGET_SIZE}")
    sp, reset = pmtools.check_vectors(image)
    if (elf.e_entry & ~1) != (reset & ~1):
        raise PackageError("ENTRY", f"ELF entry {elf.e_entry:#x} differs from the reset vector {reset:#x}")
    syms = elf.symbols()
    if syms.get("_estack") not in (None, pmtools.DTCM_END):
        raise PackageError("STACK", f"_estack {syms['_estack']:#x} is not the DTCM end")
    if marker is not None:
        found = re.findall(re.escape(marker.encode()) + rb"([0-9A-Za-z.\-]+)\x00", image)
        if len(found) != 1:
            raise PackageError("MARKER", f"expected exactly one marker {marker!r}, found {found}")
        if version is not None and found[0].decode() != version:
            raise PackageError("MARKER", f"embedded version {found[0].decode()} != {version}")
        report["marker_version"] = found[0].decode()
    report.update({"image_bytes": len(image), "stack": f"{sp:#x}", "reset": f"{reset:#x}",
                   "payload_sha256": hashlib.sha256(image).hexdigest()})
    return report


def cmd_check(args):
    report = run_check(args)
    print(json.dumps(report, indent=2))
    print(f"PASS: {args.bin.name} fits profile {pmtools.PROFILE_ID}")
    return 0


def resolve_product(args):
    if args.project:
        root = pmtools.repo_root(args.elf)
        if root is None:
            raise PackageError("REGISTRY", "components/registry.json not found above the ELF")
        p = pmtools.registry_product(root, args.project)
        expected = f"{p['image']}-v{p['version']}.bin"
        if args.bin.name != expected:
            raise PackageError("NAME", f"BIN is {args.bin.name}, registry expects {expected}")
        return p["id"], p["version"], p["marker"]
    if not (args.product and args.version):
        raise PackageError("ARG", "--project ID or --product ID --version V is required")
    return args.product, args.version, args.marker


def run_check(args):
    elf_bytes = args.elf.read_bytes()
    image = args.bin.read_bytes()
    marker, version = None, None
    if getattr(args, "project", None) or getattr(args, "product", None):
        _pid, version, marker = resolve_product(args)
    return check_image(elf_bytes, image, tuple(args.noload or ()), marker, version)


def cmd_pack(args):
    product, version, _marker = resolve_product(args)
    report = run_check(args)
    image = args.bin.read_bytes()
    header = pmtools.build_header(product, version, image)
    package = header + image
    out = args.out
    if out.suffix.lower() != ".dsy":  # a directory: the package is named after the BIN
        out.mkdir(parents=True, exist_ok=True)
        out = out / f"{args.bin.stem}.dsy"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(package)
    pmtools.verify_package(package)  # the writer re-reads its own output
    report.update({"product": product, "version": version, "package": str(out),
                   "package_bytes": len(package), "file_sha256": hashlib.sha256(package).hexdigest(),
                   "elf": str(args.elf), "bin": str(args.bin)})
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: wrote {out} ({len(package)} bytes) product={product} version={version} "
          f"payload_sha256={report['payload_sha256']}")
    return 0


def cmd_info(args):
    data = args.file.read_bytes()
    header = pmtools.verify_package(data)
    print(json.dumps({"file": str(args.file), "bytes": len(data), **header}, indent=2))
    print("PASS: header, length, payload hash and vectors are consistent")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("check", "pack"):
        p = sub.add_parser(name)
        p.add_argument("--elf", type=pathlib.Path, required=True)
        p.add_argument("--bin", type=pathlib.Path, required=True)
        p.add_argument("--map", type=pathlib.Path)
        p.add_argument("--project", help="product id in components/registry.json (derives version, name, marker)")
        p.add_argument("--product", help="product id (with --version) when not using the registry")
        p.add_argument("--version")
        p.add_argument("--marker", help="embedded marker prefix, e.g. TOYPIANO-FW-VERSION=")
        p.add_argument("--noload", action="append", help="NOLOAD section inside the target the product initializes itself")
        if name == "pack":
            p.add_argument("--out", type=pathlib.Path, required=True, help="output .dsy file or directory")
            p.add_argument("--report", type=pathlib.Path, help="JSON report path")
    p = sub.add_parser("info")
    p.add_argument("file", type=pathlib.Path)
    args = ap.parse_args(argv)
    try:
        return {"check": cmd_check, "pack": cmd_pack, "info": cmd_info}[args.cmd](args)
    except PackageError as exc:
        print(f"FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
