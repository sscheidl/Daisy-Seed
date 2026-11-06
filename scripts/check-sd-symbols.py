#!/usr/bin/env python3
"""Symbol policy for images and objects that use the shared SD driver (common/sd).

Fails if anything that could write, erase or lock a card is defined or referenced
(SDMMC commands, HAL, libDaisy BSP/disk driver, FatFs write APIs), or if libDaisy's
FatFs R0.12c glue is linked next to common/sd. --require guards against a vacuous
check: the named symbols must be defined. --allow removes names from the ban list
(e.g. the Diagnostics card-reader image's polled block writes; the policies of
common/sd's profiles are in common/sd/sd_policy.mk).

--probe is the negative check of the same policy: for each named symbol it compiles an
object that calls it with --probe-cc and checks it again together with FILE...; the
policy must then reject exactly that symbol. It proves that the allow list in use still
catches an injected writer in these very files.

  check-sd-symbols.py [--nm NM] [--require SYM,SYM] [--allow SYM,SYM]
                      [--probe SYM,SYM --probe-cc CC] FILE...
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

FORBIDDEN = {
    # SDMMC commands
    "SDMMC_CmdWriteSingleBlock", "SDMMC_CmdWriteMultiBlock", "SDMMC_CmdSDEraseStartAdd",
    "SDMMC_CmdSDEraseEndAdd", "SDMMC_CmdEraseStartAdd", "SDMMC_CmdEraseEndAdd",
    "SDMMC_CmdErase", "SDMMC_CmdLockUnlock",
    # HAL and libDaisy BSP
    "HAL_SD_WriteBlocks", "HAL_SD_WriteBlocks_IT", "HAL_SD_WriteBlocks_DMA", "HAL_SD_Erase",
    "HAL_SDEx_WriteBlocksDMAMultiBuffer", "BSP_SD_WriteBlocks", "BSP_SD_WriteBlocks_DMA",
    "BSP_SD_Erase",
    # Disk layer and FatFs write/format APIs
    "disk_write", "f_write", "f_mkfs", "f_unlink", "f_rename", "f_mkdir", "f_truncate",
    "f_sync", "f_chmod", "f_utime", "f_expand", "f_setlabel", "f_fdisk", "f_putc", "f_puts",
    "f_printf", "f_forward",
    # libDaisy FatFs R0.12c glue (must not be linked next to common/sd)
    "SD_Driver", "SD_write", "FATFS_LinkDriver", "FATFS_LinkDriverEx", "FATFS_UnLinkDriver",
    "ff_convert",
}
FORBIDDEN_SUBSTRINGS = ("FatFSInterface",)
PROBE_FLAGS = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard", "-O2", "-c"]


def symbols(nm, path):
    out = subprocess.run([nm, path], check=True, capture_output=True, text=True).stdout
    defined, referenced = set(), set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 2 or line.endswith(":"):
            continue
        kind, name = parts[-2], parts[-1]
        (referenced if kind == "U" else defined).add(name)
    return defined, referenced


def violations(nm, files, banned, require):
    defined, referenced = set(), set()
    for f in files:
        d, r = symbols(nm, f)
        defined |= d
        referenced |= r
    errors = []
    for name in sorted(defined | referenced):
        if name in banned or any(s in name for s in FORBIDDEN_SUBSTRINGS):
            errors.append(f"forbidden symbol {'defined' if name in defined else 'referenced'}: {name}")
    errors += [f"required symbol missing: {n}" for n in require if n not in defined]
    return errors


def probe_errors(args, banned):
    errors = []
    with tempfile.TemporaryDirectory() as folder:
        source, target = pathlib.Path(folder, "sd_probe.c"), pathlib.Path(folder, "sd_probe.o")
        for name in args.probe:
            source.write_text(f"extern int {name}(void);\nint sd_probe(void) {{ return {name}(); }}\n")
            subprocess.run([args.probe_cc, *PROBE_FLAGS, str(source), "-o", str(target)], check=True)
            found = violations(args.nm, args.files + [str(target)], banned, args.require)
            if f"forbidden symbol referenced: {name}" not in found:
                errors.append(f"injected writer not rejected: {name}")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--require", default="", help="comma-separated")
    ap.add_argument("--allow", default="", help="comma-separated")
    ap.add_argument("--probe", default="", help="comma-separated, must be rejected when injected")
    ap.add_argument("--probe-cc", default="arm-none-eabi-gcc")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    args.require = [n for n in args.require.split(",") if n]
    args.probe = [n for n in args.probe.split(",") if n]
    banned = FORBIDDEN - {n for n in args.allow.split(",") if n}
    errors = violations(args.nm, args.files, banned, args.require)
    if not errors:
        errors = probe_errors(args, banned)
    for e in errors:
        print(e, file=sys.stderr)
    if errors:
        return 1
    print(f"PASS sd symbol policy: {len(args.files)} file(s), {len(banned)} banned, "
          f"{len(args.require)} required, {len(args.probe)} injected writers rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
