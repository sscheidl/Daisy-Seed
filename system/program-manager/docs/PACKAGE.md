# Package format `.dsy`, catalog and card layout

**Format 1, status 2026-11-01.** Implemented in `src/pm_image.*`, `src/pm_catalog.*`
(device) and `tools/pmtools.py`, `tools/dsy_pack.py`, `tools/pm_card.py` (PC).

## 1. Card layout

```text
/DAISY/catalog.json                       catalog (≤ 16 KiB, UTF-8, BOM allowed)
/DAISY/PROGRAMS/<product-id>/<Image>-v<version>.dsy
/NEBULAE/…, /looper/…                     application data, unchanged, not the manager's business
```

**Card rule:** no file whose name contains `.bin` (any case) in the root directory – the
Daisy bootloader in front of the manager would flash it ([CONTRACT.md](CONTRACT.md) §9).
The manager reads FAT32 and exFAT (FatFs R0.16, `common/sd`); the bootloader itself reads
FAT only. The manager never writes to the card.

## 2. `.dsy` header (160 bytes, little-endian)

| Offset | Type | Field | Value / check |
| --- | --- | --- | --- |
| 0 | `char[8]` | magic | `DSYIMAGE` |
| 8 | `u16` | header_version | 1 |
| 10 | `u16` | header_size | 160 |
| 12 | `u32` | flags | 0; any set bit → rejected (`FLAGS`) |
| 16 | `char[16]` | hardware | `seed3-desktop`, NUL padded |
| 32 | `char[24]` | profile | `axi-sram-480k-v1` |
| 56 | `char[32]` | product | product id `[a-z0-9-]`, must equal the catalog id |
| 88 | `char[16]` | version | `[0-9A-Za-z.-]` |
| 104 | `u32` | load_address | `0x24000000` |
| 108 | `u32` | payload_offset | 160 |
| 112 | `u32` | payload_length | 8 … 491 520 (480 KiB) |
| 116 | `u8[32]` | payload_sha256 | SHA-256 over exactly `payload_length` payload bytes |
| 148 | `u32[2]` | reserved | 0 |
| 156 | `u32` | header_crc32 | CRC-32/ISO-HDLC over bytes 0–155 |
| 160 | – | payload | one contiguous BOOT_SRAM image |

String fields: non-empty, NUL terminated inside the field, only zero bytes after it. The
file size must be exactly `160 + payload_length`. Check order on the device: `SHORT`,
`MAGIC`, `HDRCRC`, `HDRVER`, `HDRSIZE`, `FLAGS`, `HARDWARE`, `PROFILE`, `PRODUCT`, `VERSION`,
`LOADADDR`, `OFFSET`, `LENGTH`, `RESERVED`; after loading `HASH`, then the vectors `VECSP`
(MSP in `(0x20000000, 0x20020000]`, 8-byte aligned) and `VECPC` (Thumb, inside the
payload). The package hash (`file_sha256`) is a different value from the payload hash and
serves tools only. A hash proves integrity, not origin.

## 3. Catalog `catalog.json`

```json
{
  "format": 1,
  "generation": 7,
  "hardware": "seed3-desktop",
  "programs": [
    {
      "slot": 1,
      "id": "toy-piano",
      "name": "Toy Piano",
      "category": "instrument",
      "version": "0.4",
      "path": "PROGRAMS/toy-piano/ToyPiano-v0.4.dsy",
      "payload_sha256": "…64 hex digits…",
      "file_sha256": "…64 hex digits…"
    }
  ]
}
```

| Field | Rule |
| --- | --- |
| `format` | 1 |
| `generation` | u32; incremented by the tool on every change; the device reports it as `cgen` and keeps its own runtime generation `gen` (every card/catalog change increments it) |
| `hardware` | `seed3-desktop` |
| `programs` | 0–15 objects; slots 1–15 unique; ids unique |
| `slot` | 1–15 (16 = resident diagnostics) |
| `id` | `[a-z0-9-]{1,32}`, = header product |
| `name` | 1–64 UTF-8 bytes, no control characters; JSON escapes `\uXXXX` (BMP) allowed |
| `category` | `instrument`, `effect`, `sequencer`, `other` (default) |
| `version` | `[0-9A-Za-z.-]{1,16}` |
| `path` | ≤ 160 bytes, characters `[A-Za-z0-9._/-]`, starts with `PROGRAMS/`, ends in `.dsy` with a name, no `..`, `//`, `./`, no leading/trailing `/` |
| `payload_sha256` | 64 hex digits, = header hash (otherwise `CATHASH`) |
| `file_sha256`, `profile` | optional; `profile` must be `axi-sram-480k-v1` |

Unknown keys are skipped (up to nesting depth 6); duplicate keys, numbers with sign /
fraction / leading zeros, trailing commas and text after the object are rejected. Error
codes: `TOOLARGE`, `SYNTAX`, `DEPTH`, `DUPKEY`, `MISSING`, `FORMAT`, `GENERATION`, `HARDWARE`,
`PROGRAMS`, `TOOMANY`, `SLOT`, `DUPSLOT`, `ID`, `DUPID`, `NAME`, `CATEGORY`, `VERSION`, `PATH`,
`HASH`, `PROFILE`.

## 4. Tools (local files only, no device access)

```bash
# check and package an image (product/version/marker from components/registry.json)
python tools/dsy_pack.py check --elf instruments/toy-piano/build/ToyPiano-v0.4.elf \
    --bin instruments/toy-piano/build/ToyPiano-v0.4.bin --project toy-piano
python tools/dsy_pack.py pack  --elf … --bin … --project toy-piano --out out/ --report out/toy-piano.json
python tools/dsy_pack.py info  out/ToyPiano-v0.4.dsy

# build and check a card template locally; copying to a real card is the human's step
python tools/pm_card.py init  out/card
python tools/pm_card.py add   out/card out/ToyPiano-v0.4.dsy --slot 1 --name "Toy Piano" --category instrument
python tools/pm_card.py check out/card
```

`dsy_pack.py` rejects: sections outside the regions, QSPI/flash dependencies
(`.qspiflash_*` not empty), `.data`/`.bss` not in DTCM, wrong vectors, BIN ≠ load extent,
> 480 KiB, wrong file name/marker, **undeclared NOLOAD sections in the target** (`--noload
.axi_bss` for Toy Piano `AMB_COMPACT=1` and Physical Modeling `PM_MEMORY=axi`, `--noload
.neb_sd` for Nebulae). `pm_card.py init/add/remove` refuse drive roots without
`--allow-drive`; `check` verifies the root `.bin` rule, catalog rules, file existence,
headers, hashes, product and version.

## 5. Compatibility of existing images

The BIN of a current BOOT_SRAM build is the unchanged payload. Whether a product is
suitable as a package is proven per variant with `dsy_pack.py check`; the audited products
are listed in [SOFTWARE_LOG.md](SOFTWARE_LOG.md). Images without the return service
(`common/bootloader/return_entry.h`) can be started but return to the manager only through
service/reset ("legacy").
