#!/usr/bin/env python3
"""Build and compare rename candidates without using the checkout's build outputs."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from tempfile import TemporaryDirectory


SOURCE = "https://github.com/sscheidl/Daisy-Seed.git"
SECTIONS = (".isr_vector", ".text", ".rodata", ".data")
HASHES = ("bin_sha256", *SECTIONS, "elf_sha256")
COLUMNS = ("variant", "image", *HASHES, "checker")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(directory, *args):
    return subprocess.check_output(("git", "-C", directory, *args), text=True).strip()


def snapshot(directory):
    return {path: (path.stat().st_mtime_ns, digest(path))
            for path in directory.rglob("*.bin") if path.is_file()}


def read_manifest(out, side, map_hash, variants):
    manifest = out / "manifest.tsv"
    meta = dict(line.split("=", 1) for line in (out / "meta.txt").read_text(encoding="utf-8").splitlines())
    if (meta["side"], meta["map_sha256"], meta["manifest_sha256"]) != (side, map_hash, digest(manifest)):
        raise ValueError(f"{out}: manifest, side or rename map does not match metadata")
    with manifest.open(encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file, delimiter="\t")
        if reader.fieldnames != list(COLUMNS):
            raise ValueError(f"{manifest}: invalid columns")
        rows = {}
        for row in reader:
            key = row["variant"], row["image"]
            if (key in rows or None in row or not row["image"] or not row["image"].endswith(".bin")
                    or row["checker"] not in {"PASS", "FAIL"}
                    or any(not re.fullmatch(r"[0-9a-f]{64}", row[field] or "") for field in HASHES)):
                raise ValueError(f"{manifest}: invalid or duplicate row for {key}")
            rows[key] = row
    if {variant for variant, _ in rows} != set(variants):
        raise ValueError(f"{manifest}: incomplete or unexpected variants")
    return meta, rows


def check_image(clone, image, section_file, log):
    elf = image.with_suffix(".elf")
    hashes = [digest(image)]
    for section in SECTIONS:
        # BOOT_SRAM links .rodata into .text, so objcopy may emit an empty file.
        subprocess.run(("arm-none-eabi-objcopy", "-O", "binary", f"--only-section={section}",
                        elf, section_file), check=True, stdout=log, stderr=subprocess.STDOUT)
        hashes.append(digest(section_file))
    checker = subprocess.run((sys.executable, clone / "scripts/check-firmware.py", image),
                             cwd=clone, stdout=log, stderr=subprocess.STDOUT)
    return (*hashes, digest(elf), "PASS" if checker.returncode == 0 else "FAIL")


def build(args, mapping, map_hash):
    out = args.out.resolve()
    clone = out / "src"
    variants = mapping["variants"]
    if (out / "meta.txt").exists():
        meta, rows = read_manifest(out, args.side, map_hash, variants)
        if not meta["commit"].startswith(args.commit.lower()):
            raise ValueError(f"{out}: commit does not match completed build")
        print(f"{out}: using completed {args.side} manifest ({len(rows)} images)")
        return all(row["checker"] == "PASS" for row in rows.values())

    out.mkdir(parents=True, exist_ok=True)
    clone.mkdir()  # Refuse to reuse a checkout from an interrupted build.
    subprocess.run(("git", "clone", "--no-local", "--no-checkout", "-c", "core.longpaths=true",
                    "--", args.source, clone), check=True)
    commit = git(clone, "rev-parse", "--verify", "--end-of-options", f"{args.commit}^{{commit}}")
    subprocess.run(("git", "-C", clone, "checkout", "--detach", commit), check=True)
    subprocess.run(("git", "-C", clone, "submodule", "update", "--init", "libDaisy", "DaisySP"),
                   check=True)
    subprocess.run(("git", "-C", clone / "libDaisy", "submodule", "update", "--init",
                    "--recursive", "--jobs", "4"), check=True)
    reverse_ids = {new: old for old, new in mapping["rename"]["project_ids"].items()}
    toolchain = subprocess.check_output(("arm-none-eabi-gcc", "--version"), text=True).splitlines()[0]
    submodules = git(clone, "submodule", "status", "--recursive")
    rows = []
    with TemporaryDirectory(dir=out) as temp:
        section_file = Path(temp) / "section.bin"
        for variant in variants:
            project = reverse_ids.get(variant, variant) if args.side == "base" else variant
            print(f"Building {variant} ({project})", flush=True)
            before = snapshot(clone)
            log_path = out / f"{variant}.log"
            with log_path.open("w", encoding="utf-8") as log:
                result = subprocess.run(("make", f"PROJECT={project}", "-j4"), cwd=clone,
                                        stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise ValueError(f"{project}: make failed ({result.returncode}); see {log_path}")
                images = sorted(path for path, state in snapshot(clone).items()
                                if state != before.get(path))
                if not images or len({path.name for path in images}) != len(images):
                    raise ValueError(f"{project}: expected new or changed images with unique names")
                for image in images:
                    row = (variant, image.name, *check_image(clone, image, section_file, log))
                    rows.append(row)
                    print(f"{row[-1]} {image.name} {row[2]}", flush=True)

    manifest = out / "manifest.tsv"
    with manifest.open("w", encoding="utf-8", newline="") as file:
        writer = csv.writer(file, delimiter="\t", lineterminator="\n")
        writer.writerow(COLUMNS)
        writer.writerows(rows)
    meta = out / "meta.txt"
    pending = meta.with_suffix(".tmp")
    submodule_commits = "\n".join(f"submodule.{path}={sha}" for sha, path, *_ in
                                  (line.split() for line in submodules.splitlines()))
    pending.write_text(f"side={args.side}\ncommit={commit}\nmap_sha256={map_hash}\n"
                       f"toolchain={toolchain}\nmanifest_sha256={digest(manifest)}\n{submodule_commits}\n",
                       encoding="utf-8")
    pending.replace(meta)
    return all(row[-1] == "PASS" for row in rows)


def renamed_image(image, prefixes):
    for old in sorted(prefixes, key=len, reverse=True):
        if image == f"{old}.bin" or image.startswith(f"{old}-"):
            return prefixes[old] + image[len(old):]
    return image


def compare(base_out, head_out, mapping, map_hash):
    variants = mapping["variants"]
    base_meta, base_rows = read_manifest(base_out, "base", map_hash, variants)
    _, head = read_manifest(head_out, "head", map_hash, variants)
    if base_meta["commit"] != mapping["base_commit"]:
        raise ValueError(f"{base_out}: baseline commit does not match base_commit in map")
    prefixes = mapping["rename"]["image_prefixes"]
    base = {(variant, renamed_image(image, prefixes)): row
            for (variant, image), row in base_rows.items()}
    if len(base) != len(base_rows):
        raise ValueError(f"{base_out}: image prefix mappings collide")
    identical = 0
    for key in sorted(base.keys() | head.keys()):
        label = "/".join(key)
        if key not in base or key not in head:
            print(f"FAIL {label}: missing from {'base' if key not in base else 'head'}")
            continue
        old, new = base[key], head[key]
        differences = [field for field in HASHES[:-1] if old[field] != new[field]]
        differences.extend(f"{side} checker" for side, row in (("base", old), ("head", new))
                           if row["checker"] != "PASS")
        elf_info = " (whole ELF differs)" if old["elf_sha256"] != new["elf_sha256"] else ""
        if differences:
            print(f"FAIL {label}: {', '.join(differences)}{elf_info}")
        else:
            identical += 1
            print(f"PASS {label}: {new['bin_sha256']}{elf_info}")
    total = len(base.keys() | head.keys())
    print(f"{identical}/{total} images identical with passing checkers")
    return identical == total


def main():
    parser = argparse.ArgumentParser(prog="build_compare.sh", description=__doc__)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--commit")
    parser.add_argument("--side", choices=("base", "head"))
    parser.add_argument("--out", type=Path)
    parser.add_argument("--source", default=SOURCE)
    parser.add_argument("--compare", type=Path, nargs=2, metavar=("BASE_OUT", "HEAD_OUT"))
    args = parser.parse_args()
    if args.compare and any((args.commit, args.side, args.out, args.source != SOURCE)):
        parser.error("--compare is only used with --map")
    if not args.compare and not all((args.commit, args.side, args.out)):
        parser.error("a build requires --commit, --side and --out")
    if args.commit and not re.fullmatch(r"[0-9a-fA-F]{4,40}", args.commit):
        parser.error("--commit must be a Git commit SHA")

    try:
        raw_map = args.map.read_bytes()
        map_hash = hashlib.sha256(raw_map).hexdigest()
        mapping = json.loads(raw_map)
        variants = mapping["variants"]
        if (not variants or len(set(variants)) != len(variants)
                or any(not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", v) for v in variants)):
            raise ValueError("map must specify unique, nonempty project IDs")
        result = compare(*args.compare, mapping, map_hash) if args.compare else build(args, mapping, map_hash)
        return 0 if result else 1
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"build_compare.sh: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
