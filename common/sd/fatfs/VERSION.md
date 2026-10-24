# Vendored FatFs

**FatFs R0.16 with patch-1 and patch-2** by ChaN, imported on 2026-10-23.
Update only on purpose, with the reason recorded here.

| Download | SHA256 |
| --- | --- |
| <https://elm-chan.org/fsw/ff/arc/ff16.zip> (2,393,600 B) | `99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e` |
| <https://elm-chan.org/fsw/ff/patch/ff16p1.diff> (2,322 B) | `7996ddc3135f3d534a8153f7d75d587030e5c8551b003c3b7ee823ffb7fc64bf` |
| <https://elm-chan.org/fsw/ff/patch/ff16p2.diff> (2,259 B) | `5cd39f1fc299f0f1dec9fa1ce544dc0bc048b2f6eb3b8161476ed20f9cf1e290` |

Patch-1 (2025-09-13) lowers the minimum volume size to 64 sectors. Patch-2
(2026-07-10) fixes CVE-2026-6682, CVE-2026-6683 and CVE-2026-6687 (malformed FAT/
exFAT headers: file corruption, buffer overflow, divide by zero); it must be applied
after patch-1. Source: <https://elm-chan.org/fsw/ff/patches.html>.

## Import procedure

1. From `ff16.zip` take `source/ff.c`, `ff.h`, `diskio.h`, `ffunicode.c`,
   `00readme.txt`, `00history.txt` and `LICENSE.txt`. The upstream `ffconf.h`,
   `diskio.c` and `ffsystem.c` are not used; `../ffconf.h` replaces the first and
   `../ro_sd.c` the disk layer.
2. Convert CRLF to LF (the repository stores text with LF).
3. `patch ff.c ff16p1.diff`, then `patch ff.c ff16p2.diff`.

No other change was made. Upstream files (CRLF) and the stored files (LF):

| File | Upstream SHA256 | Stored SHA256 |
| --- | --- | --- |
| `ff.c` | `dbeae3d7d22dbc070983e31af897fa5e43d29c726a0a240fe9589ffe2ec0ef1e` (before patches) | `3c30280ffe2d0fb19a5241d9ed9f20a2c3911c2861652ebf6ad57f74fb369250` (patched) |
| `ff.h` | `42920ae4d6080bfc2a72d92a9969b5e94eeaff070e5e71d90da9ab6d7d8e158e` | `86099100a6ef3c3623dfbeaacb3a2613ff266df76aad1386513042b831c5b733` |
| `diskio.h` | `1c2b29f53e061c55d43b2e412c3a61320b2621bac7561a05e275514baa44785a` | `2279ac777bc7a8a317b1b44519df8604e4cdd0cd3b1608861eed3c78045d89c6` |
| `ffunicode.c` | `3f1632c1f1e64b0b683b0d7e51341b9870728f398604869dd8211f1819509a8e` | `6cd4dc4a664386ebd108e56d367582c35f005fc20381486366b5d77ac58119bd` |
| `LICENSE.txt` | `2576183a3e1c2d2f4b3628a7ef93c2f937879698abdd6d554dd718e79062e079` | `b896e32fb2cdb3fc8b3b6fd854706fca4ac55619fbb6d5eb2429cb05ee338966` |

## License

FatFs license (1-clause BSD style), see [LICENSE.txt](LICENSE.txt). Source
redistributions keep the copyright notice in the files; binaries need no notice.
