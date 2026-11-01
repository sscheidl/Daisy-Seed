# Paketformat `.dsy`, Katalog und Kartenaufbau

**Format 1, Stand 01.11.2026.** Implementiert in `src/pm_image.*`, `src/pm_catalog.*`
(Gerät) und `tools/pmtools.py`, `tools/dsy_pack.py`, `tools/pm_card.py` (PC).

## 1. Kartenaufbau

```text
/DAISY/catalog.json                       Katalog (≤ 16 KiB, UTF-8, BOM erlaubt)
/DAISY/PROGRAMS/<produkt-id>/<Image>-v<Version>.dsy
/NEBULAE/…, /looper/…                     Anwendungsdaten, unverändert, nicht Sache der Verwaltung
```

**Kartenregel:** keine Datei mit `.bin` im Namen (beliebige Groß-/Kleinschreibung)
im Wurzelverzeichnis – der vorgeschaltete Daisy-Bootloader würde sie flashen
([CONTRACT.md](CONTRACT.md) §9). FAT32 und exFAT werden von der Verwaltung gelesen
(FatFs R0.16, `common/sd`); der Bootloader selbst liest nur FAT. Die Karte wird von
der Verwaltung nie beschrieben.

## 2. `.dsy`-Header (160 Byte, Little-Endian)

| Offset | Typ | Feld | Wert / Prüfung |
| --- | --- | --- | --- |
| 0 | `char[8]` | magic | `DSYIMAGE` |
| 8 | `u16` | header_version | 1 |
| 10 | `u16` | header_size | 160 |
| 12 | `u32` | flags | 0; jedes gesetzte Bit → Ablehnung (`FLAGS`) |
| 16 | `char[16]` | hardware | `seed3-desktop`, NUL-gefüllt |
| 32 | `char[24]` | profile | `axi-sram-480k-v1` |
| 56 | `char[32]` | product | Produkt-ID `[a-z0-9-]`, muss der Katalog-ID entsprechen |
| 88 | `char[16]` | version | `[0-9A-Za-z.-]` |
| 104 | `u32` | load_address | `0x24000000` |
| 108 | `u32` | payload_offset | 160 |
| 112 | `u32` | payload_length | 8 … 491 520 (480 KiB) |
| 116 | `u8[32]` | payload_sha256 | SHA-256 über genau `payload_length` Payloadbytes |
| 148 | `u32[2]` | reserved | 0 |
| 156 | `u32` | header_crc32 | CRC-32/ISO-HDLC über Byte 0–155 |
| 160 | – | payload | ein zusammenhängendes BOOT_SRAM-Image |

Stringfelder: nicht leer, NUL-terminiert innerhalb des Feldes, nur Nullbytes
danach. Die Dateigröße muss exakt `160 + payload_length` sein. Prüfreihenfolge im
Gerät: `SHORT`, `MAGIC`, `HDRCRC`, `HDRVER`, `HDRSIZE`, `FLAGS`, `HARDWARE`, `PROFILE`,
`PRODUCT`, `VERSION`, `LOADADDR`, `OFFSET`, `LENGTH`, `RESERVED`; nach dem Laden
`HASH`, dann Vektoren `VECSP` (MSP in `(0x20000000, 0x20020000]`, 8-Byte-ausgerichtet)
und `VECPC` (Thumb, innerhalb des Payloads). Der Pakethash (`file_sha256`) ist ein
anderer Wert als der Payloadhash und dient nur Werkzeugen. Ein Hash beweist
Integrität, keine Herkunft.

## 3. Katalog `catalog.json`

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
      "version": "0.4.0",
      "path": "PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy",
      "payload_sha256": "…64 Hexziffern…",
      "file_sha256": "…64 Hexziffern…"
    }
  ]
}
```

| Feld | Regel |
| --- | --- |
| `format` | 1 |
| `generation` | u32; vom Werkzeug bei jeder Änderung erhöht; das Gerät meldet es als `cgen` und führt daneben eine eigene Laufzeitgeneration `gen` (jede Karten-/Katalogänderung erhöht sie) |
| `hardware` | `seed3-desktop` |
| `programs` | 0–15 Objekte; Plätze 1–15, eindeutig; IDs eindeutig |
| `slot` | 1–15 (16 = residente Diagnose) |
| `id` | `[a-z0-9-]{1,32}`, = Header-Produkt |
| `name` | 1–64 UTF-8-Bytes, keine Steuerzeichen; JSON-Escapes `\uXXXX` (BMP) erlaubt |
| `category` | `instrument`, `effect`, `sequencer`, `other` (Standard) |
| `version` | `[0-9A-Za-z.-]{1,16}` |
| `path` | ≤ 160 Byte, Zeichen `[A-Za-z0-9._/-]`, beginnt mit `PROGRAMS/`, endet `.dsy` mit Namen, kein `..`, `//`, `./`, führender/abschließender `/` |
| `payload_sha256` | 64 Hexziffern, = Header-Hash (sonst `CATHASH`) |
| `file_sha256`, `profile` | optional; `profile` muss `axi-sram-480k-v1` sein |

Unbekannte Schlüssel werden übersprungen (bis Verschachtelungstiefe 6), doppelte
Schlüssel, Zahlen mit Vorzeichen/Nachkommastellen/führenden Nullen, trailing commas
und Text nach dem Objekt werden abgewiesen. Fehlercodes: `TOOLARGE`, `SYNTAX`, `DEPTH`,
`DUPKEY`, `MISSING`, `FORMAT`, `GENERATION`, `HARDWARE`, `PROGRAMS`, `TOOMANY`, `SLOT`,
`DUPSLOT`, `ID`, `DUPID`, `NAME`, `CATEGORY`, `VERSION`, `PATH`, `HASH`, `PROFILE`.

## 4. Werkzeuge (nur lokale Dateien, kein Gerätezugriff)

```bash
# Image prüfen und paketieren (Produkt/Version/Marker aus components/registry.json)
python tools/dsy_pack.py check --elf instruments/toy-piano/build/ToyPiano-v0.4.0.elf \
    --bin instruments/toy-piano/build/ToyPiano-v0.4.0.bin --project toy-piano
python tools/dsy_pack.py pack  --elf … --bin … --project toy-piano --out out/ --report out/toy-piano.json
python tools/dsy_pack.py info  out/ToyPiano-v0.4.0.dsy

# Kartenvorlage lokal aufbauen und prüfen; auf eine echte Karte kopiert der Mensch
python tools/pm_card.py init  out/card
python tools/pm_card.py add   out/card out/ToyPiano-v0.4.0.dsy --slot 1 --name "Toy Piano" --category instrument
python tools/pm_card.py check out/card
```

`dsy_pack.py` lehnt ab: Sektionen außerhalb der Regionen, QSPI-/Flash-Abhängigkeiten
(`.qspiflash_*` nicht leer), `.data`/`.bss` nicht in DTCM, falsche Vektoren, BIN ≠
Ladeausdehnung, > 480 KiB, falscher Dateiname/Marker, **undeklarierte NOLOAD-Sektionen
im Ziel** (`--noload .axi_bss` für Toy Piano `AMB_COMPACT=1` und Physical Modeling
`PM_MEMORY=axi`, `--noload .neb_sd` für Nebulae). `pm_card.py init/add/remove` verweigern
Laufwerkswurzeln ohne `--allow-drive`; `check` prüft Wurzel-`.bin`, Katalogregeln,
Dateiexistenz, Header, Hashes, Produkt und Version.

## 5. Kompatibilität bestehender Images

Das BIN eines heutigen BOOT_SRAM-Builds ist unverändert der Payload. Ob ein Produkt
sich als Paket eignet, ist je Variante mit `dsy_pack.py check` zu belegen; eine
Übersicht der auditierten Produkte steht in [SOFTWARE_LOG.md](SOFTWARE_LOG.md).
Images ohne den Rückkehrdienst (`common/bootloader/return_entry.h`) sind startbar,
kehren aber nur über Service/Reset zur Verwaltung zurück („Legacy").
