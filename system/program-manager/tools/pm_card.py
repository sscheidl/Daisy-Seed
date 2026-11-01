#!/usr/bin/env python3
"""Prepare and check a LOCAL card template for the Daisy Program Manager.

  pm_card.py init   DIR                      create DIR/DAISY/catalog.json (empty) and DIR/DAISY/PROGRAMS
  pm_card.py add    DIR FILE.dsy --slot N [--name NAME] [--category C]   copy and register a package
  pm_card.py remove DIR --slot N
  pm_card.py check  DIR                      validate the card tree (exit 1 on any problem)

The tool only touches the directory it is given. It refuses to init/add/remove on a
drive root (a real card) unless --allow-drive is passed: copying a template to a card
is the human's step, after `check` passed. `check` also enforces the card rule of the
unchanged Daisy bootloader: no file whose name contains ".bin" (any case) may lie in
the ROOT directory, because the bootloader would flash it before the manager starts.
"""
import argparse
import json
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pmtools  # noqa: E402
from pmtools import PackageError  # noqa: E402


def is_drive_root(path):
    p = pathlib.Path(path).resolve()
    return p == pathlib.Path(p.anchor)


def guard(args):
    if is_drive_root(args.dir) and not args.allow_drive:
        raise PackageError("DRIVE", f"{args.dir} looks like a drive root; pass --allow-drive to write there")


def catalog_path(d):
    return pathlib.Path(d) / "DAISY" / "catalog.json"


def empty_catalog():
    return {"format": pmtools.CATALOG_FORMAT, "generation": 1, "hardware": pmtools.HARDWARE_ID, "programs": []}


def cmd_init(args):
    guard(args)
    cat = catalog_path(args.dir)
    if cat.exists() and not args.force:
        raise PackageError("EXISTS", f"{cat} exists; use --force to overwrite")
    (pathlib.Path(args.dir) / "DAISY" / "PROGRAMS").mkdir(parents=True, exist_ok=True)
    cat.write_text(pmtools.dump_catalog(empty_catalog()), encoding="utf-8")
    print(f"PASS: created {cat}")
    return 0


def cmd_add(args):
    guard(args)
    data = pathlib.Path(args.file).read_bytes()
    header = pmtools.verify_package(data)
    cat = catalog_path(args.dir)
    doc = pmtools.load_catalog(cat) if cat.exists() else empty_catalog()
    if not 1 <= args.slot <= pmtools.CATALOG_MAX_PROGRAMS:
        raise PackageError("SLOT", f"slot {args.slot} outside 1..{pmtools.CATALOG_MAX_PROGRAMS}")
    rel = f"PROGRAMS/{header['product']}/{pathlib.Path(args.file).name}"
    if not pmtools.valid_path(rel):
        raise PackageError("PATH", f"{rel} is not a valid catalog path (file name characters?)")
    dest = pathlib.Path(args.dir) / "DAISY" / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.file, dest)
    programs = [e for e in doc["programs"] if e["slot"] != args.slot and e["id"] != header["product"]]
    entry = {
        "slot": args.slot,
        "id": header["product"],
        "name": args.name or header["product"],
        "category": args.category,
        "version": header["version"],
        "path": rel,
        "payload_sha256": header["payload_sha256"],
        "file_sha256": header["file_sha256"],
    }
    programs.append(entry)
    programs.sort(key=lambda e: e["slot"])
    doc["programs"] = programs
    doc["generation"] = (doc["generation"] + 1) & 0xFFFFFFFF
    pmtools.validate_catalog(doc)
    cat.parent.mkdir(parents=True, exist_ok=True)
    cat.write_text(pmtools.dump_catalog(doc), encoding="utf-8")
    print(f"PASS: slot {args.slot} = {header['product']} {header['version']} ({rel}); generation {doc['generation']}")
    return 0


def cmd_remove(args):
    guard(args)
    cat = catalog_path(args.dir)
    doc = pmtools.load_catalog(cat)
    before = len(doc["programs"])
    doc["programs"] = [e for e in doc["programs"] if e["slot"] != args.slot]
    if len(doc["programs"]) == before:
        raise PackageError("NOENTRY", f"slot {args.slot} is empty")
    doc["generation"] = (doc["generation"] + 1) & 0xFFFFFFFF
    cat.write_text(pmtools.dump_catalog(doc), encoding="utf-8")
    print(f"PASS: slot {args.slot} removed (file kept); generation {doc['generation']}")
    return 0


def check_tree(root):
    """Returns (problems, entries_report)."""
    root = pathlib.Path(root)
    problems = []
    rows = []
    # Card rule: nothing with ".bin" in its name in the root directory.
    for p in root.iterdir():
        if p.is_file() and ".bin" in p.name.lower():
            problems.append(f"ROOTBIN: {p.name} in the root directory would be flashed by the Daisy bootloader")
    cat = catalog_path(root)
    if not cat.exists():
        problems.append("MISSING: DAISY/catalog.json not found")
        return problems, rows
    try:
        doc = pmtools.load_catalog(cat)
    except PackageError as exc:
        problems.append(f"CATALOG {exc}")
        return problems, rows
    for e in doc["programs"]:
        row = {"slot": e["slot"], "id": e["id"], "version": e["version"], "path": e["path"], "status": "OK"}
        file = root / "DAISY" / e["path"]
        try:
            if not file.exists():
                raise PackageError("NOFILE", f"{e['path']} does not exist")
            header = pmtools.verify_package(file.read_bytes())
            if header["product"] != e["id"]:
                raise PackageError("PRODUCT", f"package product {header['product']} != catalog id {e['id']}")
            if header["payload_sha256"] != e["payload_sha256"].lower():
                raise PackageError("CATHASH", "catalog payload_sha256 differs from the package header")
            if "file_sha256" in e and header["file_sha256"] != e["file_sha256"].lower():
                raise PackageError("FILEHASH", "catalog file_sha256 differs from the file")
            if header["version"] != e["version"]:
                raise PackageError("VERSION", f"package version {header['version']} != catalog {e['version']}")
            row["bytes"] = header["payload_length"]
        except PackageError as exc:
            row["status"] = str(exc)
            problems.append(f"slot {e['slot']} ({e['id']}): {exc}")
        rows.append(row)
    return problems, rows


def cmd_check(args):
    problems, rows = check_tree(args.dir)
    for r in rows:
        print(f"  slot {r['slot']:>2}  {r['id']:<24} {r['version']:<10} {r['status']}")
    for p in problems:
        print("FAIL:", p)
    if problems:
        print(f"FAIL: {len(problems)} problem(s) in {args.dir}")
        return 1
    print(f"PASS: {args.dir} ({len(rows)} program(s), root free of .bin files)")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("init")
    p.add_argument("dir", type=pathlib.Path)
    p.add_argument("--force", action="store_true")
    p.add_argument("--allow-drive", action="store_true")
    p = sub.add_parser("add")
    p.add_argument("dir", type=pathlib.Path)
    p.add_argument("file", type=pathlib.Path)
    p.add_argument("--slot", type=int, required=True)
    p.add_argument("--name")
    p.add_argument("--category", default="other", choices=pmtools.CATEGORIES)
    p.add_argument("--allow-drive", action="store_true")
    p = sub.add_parser("remove")
    p.add_argument("dir", type=pathlib.Path)
    p.add_argument("--slot", type=int, required=True)
    p.add_argument("--allow-drive", action="store_true")
    p = sub.add_parser("check")
    p.add_argument("dir", type=pathlib.Path)
    args = ap.parse_args(argv)
    try:
        return {"init": cmd_init, "add": cmd_add, "remove": cmd_remove, "check": cmd_check}[args.cmd](args)
    except PackageError as exc:
        print(f"FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
