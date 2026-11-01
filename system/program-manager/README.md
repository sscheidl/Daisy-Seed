# Daisy Program Manager

Program management for the Seed3 Desktop Kit: an application installed permanently in
QSPI behind the **unchanged Daisy bootloader** that selects up to 15 programs from the
SD card, verifies them, loads them into SRAM and starts them. Slot 16 is the resident
diagnostics (the same diagnostics layer as `diagnostics/`). Normal program changes write
neither internal flash nor QSPI. Design E of the
[concept v2](../../docs/SD_RAM_BOOT_CONCEPT_V2.md) (German); contracts in [docs/CONTRACT.md](docs/CONTRACT.md).

**Status 2026-11-01: version 0.1.0, software fully tested, hardware experimental –
NICHT GEMESSEN** ([docs/HARDWARE_TEST.md](docs/HARDWARE_TEST.md)).

## Structure

| Part | Location | Verification |
| --- | --- | --- |
| Portable core: `.dsy` header, catalog, loader state machine, modes/keys/LEDs, PM protocol | `src/pm_*.{h,cpp}`, `src/pm_sha256.c` | `make test`, `make test-sanitize` |
| Hardware glue (libDaisy, `common/sd`, cache, BootInfo) | `src/pm_main.cpp`, `ld/program_manager_qspi.lds` | ARM build, `make check` |
| Shared kit adapter (keys, LEDs, CV, tone, CDC) | `diagnostics/firmware/src/devkit_board.h` | diagnostics tests/builds |
| Return service for applications | `common/bootloader/return_entry.h`, `return_daisy.h` | `test_return_entry` |
| PC tools: packaging, card template, image check, CDC CLI | `tools/` | `make test-python` |
| HTML panel (Web Serial) | `web/` | manual browser test |

## Building

```bash
make -C system/program-manager PROFILE=probe     # build/probe/DaisyProgramManagerProbe-v0.1.0.bin
make -C system/program-manager                   # build/manager/DaisyProgramManager-v0.1.0.bin
make -C system/program-manager check PYTHON=python SD_CHECK_PYTHON=python
make PROJECT=program-manager                     # through the project selection (also program-manager-probe)
```

Windows: put the xPack GCC and build tools from `.tools` on the PATH ([docs/BUILD.md](../../docs/BUILD.md));
host tests with `HOST_CXX=clang++ HOST_CC=clang`. Both profiles have separate build
directories; the `build=` value in `PMINFO` is the short source commit.

**Probe** (LED, CDC, diagnostics, service, no SD start) is the first hardware candidate: it
settles the XIP start from QSPI. **Manager** is version 1 with SD catalog and start.

## Memory and start contract (summary)

* Manager: code in QSPI from `0x90040000` (below `0x907E0000`), `.data`/`.bss`/stack in
  DTCM, heap in D2, a 4 KiB IDMA bounce buffer in the upper 32 KiB of AXI SRAM.
  `0x24000000`–`0x24078000` stays free – `tools/check_qspi_image.py` enforces that from the ELF.
* Start: verify the package (header, length, SHA-256, vectors) → target zeroed and filled →
  audio/LED/SD quiesced → D-cache written back → `boot_info = JUMP(0x24000000)` → software
  reset. The bootloader jumps exactly as for every BOOT_SRAM start today.
* Return: the application shuts down in an orderly way and calls
  `ResetToBootloader(DAISY_SKIP_TIMEOUT)`. Service/DFU unchanged (S9+S16, BL SysEx, DDP `BOOT`).

## Operation

| Mode | Keys | LEDs |
| --- | --- | --- |
| Select | pressing and releasing key 1–15 once starts that slot; key 16 opens the diagnostics. Keys held at power-up do not count; chords and holds ≥ 3 s never select | occupied slots and 16 dimmed, the pressed key bright; error: all blink for 1.5 s (the affected slot stays on) |
| Diagnostics | as in the diagnostics firmware; **S1+S16 for 3 s** returns to Select | diagnostics layer |
| Loading | none | bar growing with progress |
| Service | – | all on |

No program autostarts. Without a card, diagnostics and service remain usable. CV OUT and
the test tone only in Diagnostics during a host session; the electrical rest state is
NICHT GEMESSEN.

## Card

```text
/DAISY/catalog.json
/DAISY/PROGRAMS/<product-id>/<Image>-v<version>.dsy
```

**Card rule:** no file whose name contains `.bin` may lie in the root directory – the
bootloader would flash it before the manager starts. Build and check the template on the
PC, then copy it ([docs/PACKAGE.md](docs/PACKAGE.md)):

```bash
python tools/dsy_pack.py pack --elf instruments/toy-piano/build/ToyPiano-v0.4.elf \
    --bin instruments/toy-piano/build/ToyPiano-v0.4.bin --project toy-piano --out out/
python tools/pm_card.py init out/card
python tools/pm_card.py add out/card out/ToyPiano-v0.4.dsy --slot 1 --name "Toy Piano" --category instrument
python tools/pm_card.py check out/card
```

## Updates and recovery

| Task | Path |
| --- | --- |
| Update a program | new `.dsy` on the card, catalog via `pm_card.py add`, eject the card; no flashing |
| Update the manager | the usual firmware update through the bootloader to `0x90040000` ([docs/FLASHING.md](../../docs/FLASHING.md)); **note:** an instrument BIN flashed there replaces the manager |
| Manager hangs / is damaged | reset, then BOOT within ~2 s → DFU; flash again. Last resort: ROM DFU on the module port. Never `program-boot` (installs a bootloader; not part of this project) |

## Host

* CLI: `python tools/pm_cli.py [-p COMx] info|list|status|start|mode|rescan|service` (pyserial). Reading commands change nothing; `start`/`service` are explicit actions.
* HTML panel: open `web/panel.html` in Chrome/Edge (Web Serial); the simulator only through the visibly marked button.
* Protocol: [docs/PROTOCOL.md](docs/PROTOCOL.md). JUCE standalone/VST3 follow later with the same semantics.

## Limits

MSC/card reader stays a separate diagnostics image; no check after the reset, no flash
write protection, 2 s DFU window, no watchdog. Products without the return service can be
started as packages but return only through the service path.
