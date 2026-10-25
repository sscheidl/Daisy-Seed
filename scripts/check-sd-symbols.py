#!/usr/bin/env python3
"""Symbol policy for images and objects that use the shared read-only SD driver.

Fails if anything that could write, erase or lock a card is defined or referenced
(SDMMC commands, HAL, libDaisy BSP/disk driver, FatFs write APIs), or if libDaisy's
FatFs R0.12c glue is linked next to common/sd. --require guards against a vacuous
check: the named symbols must be defined. --allow removes names from the ban list
(e.g. the Diagnostics card-reader image's polled block writes).

  check-sd-symbols.py [--nm NM] [--require SYM,SYM] [--allow SYM,SYM] FILE...
"""
import argparse
import subprocess
import sys

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--require", default="", help="comma-separated")
    ap.add_argument("--allow", default="", help="comma-separated")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    args.require = [n for n in args.require.split(",") if n]
    banned = FORBIDDEN - {n for n in args.allow.split(",") if n}
    defined, referenced = set(), set()
    for f in args.files:
        d, r = symbols(args.nm, f)
        defined |= d
        referenced |= r
    errors = []
    for name in sorted(defined | referenced):
        if name in banned or any(s in name for s in FORBIDDEN_SUBSTRINGS):
            errors.append(f"forbidden symbol {'defined' if name in defined else 'referenced'}: {name}")
    errors += [f"required symbol missing: {n}" for n in args.require if n not in defined]
    for e in errors:
        print(e, file=sys.stderr)
    if errors:
        return 1
    print(f"PASS sd symbol policy: {len(args.files)} file(s), {len(banned)} banned, "
          f"{len(args.require)} required")
    return 0


if __name__ == "__main__":
    sys.exit(main())
