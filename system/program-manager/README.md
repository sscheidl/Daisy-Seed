# Daisy Program Manager

Programmverwaltung für das Seed3 Desktop Kit: eine dauerhaft im QSPI installierte
Anwendung hinter dem **unveränderten Daisy-Bootloader**, die bis zu 15 Programme von
der SD-Karte auswählt, prüft, ins SRAM lädt und startet. Platz 16 ist die residente
Diagnose (dieselbe Diagnoseschicht wie `diagnostics/`). Normale Programmwechsel
schreiben weder internen Flash noch QSPI. Entwurf E aus
[Konzept v2](../../docs/SD_RAM_BOOT_CONCEPT_V2.md); Verträge in [docs/CONTRACT.md](docs/CONTRACT.md).

**Status 01.11.2026: Version 0.1.0, Software vollständig getestet, Hardware
experimentell – NICHT GEMESSEN** ([docs/HARDWARE_TEST.md](docs/HARDWARE_TEST.md)).

## Aufbau

| Teil | Ort | Prüfung |
| --- | --- | --- |
| Portabler Kern: `.dsy`-Header, Katalog, Loader-Zustandsautomat, Modi/Tasten/LEDs, PM-Protokoll | `src/pm_*.{h,cpp}`, `src/pm_sha256.c` | `make test`, `make test-sanitize` |
| Hardware-Glue (libDaisy, `common/sd`, Cache, BootInfo) | `src/pm_main.cpp`, `ld/program_manager_qspi.lds` | ARM-Build, `make check` |
| Geteilter Kit-Adapter (Tasten, LEDs, CV, Ton, CDC) | `diagnostics/firmware/src/devkit_board.h` | Diagnose-Tests/-Builds |
| Rückkehrdienst für Anwendungen | `common/bootloader/return_entry.h`, `return_daisy.h` | `test_return_entry` |
| PC-Werkzeuge: Paketierung, Kartenvorlage, Image-Check, CDC-CLI | `tools/` | `make test-python` |
| HTML-Panel (Web Serial) | `web/` | Browsertest manuell |

## Bauen

```bash
make -C system/program-manager PROFILE=probe     # build/probe/DaisyProgramManagerProbe-v0.1.0.bin
make -C system/program-manager                   # build/manager/DaisyProgramManager-v0.1.0.bin
make -C system/program-manager check PYTHON=python SD_CHECK_PYTHON=python
make PROJECT=program-manager                     # über die Projektauswahl (auch program-manager-probe)
```

Windows: xPack-GCC und Build-Tools aus `.tools` in den PATH ([docs/BUILD.md](../../docs/BUILD.md));
Host-Tests mit `HOST_CXX=clang++ HOST_CC=clang`. Beide Profile haben getrennte
Buildverzeichnisse; der `build=`-Wert in `PMINFO` ist der kurze Quellcommit.

**Probe** (LED, CDC, Diagnose, Service, kein SD-Start) ist der erste Hardwarekandidat:
sie klärt den XIP-Start aus QSPI. **Manager** ist Version 1 mit SD-Katalog und Start.

## Speicher- und Startvertrag (Kurzfassung)

* Verwaltung: Code in QSPI ab `0x90040000` (unter `0x907E0000`), `.data`/`.bss`/Stack in
  DTCM, Heap in D2, 4-KiB-IDMA-Bounce in den oberen 32 KiB AXI. `0x24000000`–`0x24078000`
  bleibt frei – `tools/check_qspi_image.py` erzwingt das aus dem ELF.
* Start: Paket prüfen (Header, Länge, SHA-256, Vektoren) → Ziel genullt und befüllt →
  Audio/LED/SD stillgelegt → D-Cache zurückgeschrieben → `boot_info = JUMP(0x24000000)`
  → Software-Reset. Der Bootloader springt wie bei jedem heutigen BOOT_SRAM-Start.
* Rückkehr: Anwendung beendet sich geordnet und ruft `ResetToBootloader(DAISY_SKIP_TIMEOUT)`.
  Service/DFU unverändert (S9+S16, BL-SysEx, DDP `BOOT`).

## Bedienung

| Modus | Tasten | LEDs |
| --- | --- | --- |
| Auswahl | Taste 1–15 einmal drücken und loslassen startet den Platz; Taste 16 öffnet die Diagnose. Beim Einschalten gehaltene Tasten zählen nicht; Akkorde und Halten ≥ 3 s wählen nie | belegte Plätze und 16 gedimmt, gedrückte Taste hell; Fehler: alle blinken 1,5 s (betroffener Platz dauerhaft an) |
| Diagnose | wie Diagnosefirmware; **S1+S16 3 s** zurück zur Auswahl | Diagnoseschicht |
| Laden | keine | Balken mit Fortschritt |
| Service | – | alle an |

Kein Autostart eines Programms. Ohne Karte bleiben Diagnose und Service nutzbar.
CV OUT und Testton nur in der Diagnose während einer Hostsitzung; elektrischer
Ruhezustand NICHT GEMESSEN.

## Karte

```text
/DAISY/catalog.json
/DAISY/PROGRAMS/<produkt-id>/<Image>-v<Version>.dsy
```

**Kartenregel:** keine Datei mit `.bin` im Namen im Wurzelverzeichnis – der
Bootloader würde sie vor der Verwaltung flashen. Vorlage am PC aufbauen und prüfen,
dann kopieren ([docs/PACKAGE.md](docs/PACKAGE.md)):

```bash
python tools/dsy_pack.py pack --elf instruments/toy-piano/build/ToyPiano-v0.4.0.elf \
    --bin instruments/toy-piano/build/ToyPiano-v0.4.0.bin --project toy-piano --out out/
python tools/pm_card.py init out/card
python tools/pm_card.py add out/card out/ToyPiano-v0.4.0.dsy --slot 1 --name "Toy Piano" --category instrument
python tools/pm_card.py check out/card
```

## Updates und Wiederherstellung

| Vorgang | Weg |
| --- | --- |
| Programm aktualisieren | neues `.dsy` auf die Karte, Katalog mit `pm_card.py add`, Karte auswerfen; kein Flashen |
| Verwaltung aktualisieren | wie bisher ein Firmware-Update über den Bootloader nach `0x90040000` ([docs/FLASHING.md](../../docs/FLASHING.md)); **Achtung:** ein dort geflashtes Instrument-BIN ersetzt die Verwaltung |
| Verwaltung hängt/defekt | Reset, dann BOOT innerhalb ~2 s → DFU; erneut flashen. Letzter Weg: ROM-DFU am Modulport. Nie `program-boot` (installiert einen Bootloader; nicht Teil dieses Projekts) |

## Host

* CLI: `python tools/pm_cli.py [-p COMx] info|list|status|start|mode|rescan|service` (pyserial). Lesende Befehle ändern nichts; `start`/`service` sind ausdrückliche Aktionen.
* HTML-Panel: `web/panel.html` in Chrome/Edge öffnen (Web Serial); Simulator nur über die sichtbar gekennzeichnete Schaltfläche.
* Protokoll: [docs/PROTOCOL.md](docs/PROTOCOL.md). JUCE Standalone/VST3 folgen später mit derselben Semantik.

## Grenzen

MSC/Kartenleser bleibt eigenes Diagnose-Image; keine Prüfung nach dem Reset, kein
Flash-Schreibschutz, 2-s-DFU-Fenster, kein Watchdog. Produkte ohne Rückkehrdienst
sind als Pakete startbar, kehren aber nur über Service zurück.
