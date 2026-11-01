# Speicher-, Start- und Commit-Vertrag der Daisy-Programmverwaltung

**Stand 01.11.2026, Version 0.1.0.** Verbindliche Regeln für Firmware, Linker,
Werkzeuge und Tests. Jede Aussage ist als **[B]** belegt (Datei/Zeile oder
Upstream-Quelle), **[P]** Praxis am Kit (datierte Berichte anderer Teile des Repos)
oder **[A]** Annahme markiert, die erst ein Hardwaretest bestätigt
([HARDWARE_TEST.md](HARDWARE_TEST.md)). Grundlage: [Konzept v2](../../../docs/SD_RAM_BOOT_CONCEPT_V2.md),
[Review](../../../docs/SD_RAM_BOOT_REVIEW_CLAUDE.md), [Entwicklungsauftrag](../../../docs/SD_RAM_BOOT_CLAUDE_AUFTRAG.md).

## 1. Quellenblatt

| Gegenstand | Quelle | Stand |
| --- | --- | --- |
| Tatsächlicher Startcode | `libDaisy/core/startup_stm32h750xx.c` (Zeilen 1535–1558): bei `BOOT_APP` kein `SystemInit()`, kein eigenes Setzen von SP | [B], libDaisy `f715e487` |
| BOOT_QSPI/BOOT_SRAM-Auswahl | `libDaisy/core/Makefile` 229–241 (`-DBOOT_APP`, Linker) | [B] |
| Bootloader-JUMP-Pfad | DaisyBootloader `shared/bootloader.cpp` `startup_process()`, Tag v6.4.0 (gelesen 31.10.2026): `InitBackupSram`, `version = v6_1`, bei `JUMP` → `status = INVALID`, SRAM-Ziel: `SCB->VTOR = data`, `__set_MSP(*data)`, Sprung; QSPI-Ziel: `HAL_Init`, QSPI memory-mapped, Sprung | [B] (Quelle); dass das installierte Binary diesem Tag entspricht: [A] |
| Rückkehr/Service | `libDaisy/src/sys/system.cpp` 301–353 (`ResetToBootloader`: `boot_info.status`, `RCC->CIER = 0`, `HAL_NVIC_SystemReset`); Upstream: `SKIP_TIMEOUT` → 100 ms, `INF_TIMEOUT` → `UINT32_MAX` | [B] |
| Täglicher Reset→SRAM-Start | alle Instrumente sind BOOT_SRAM (`common/desktop.mk:9`); Diagnose H1–H10 GEMESSEN 2026-10-17 | [P] |
| Cache-Funktionen | CMSIS 5.9 `cachel1_armv7.h` 358–377: `SCB_CleanDCache_by_Addr(addr, dsize)` verarbeitet ab `addr` in 32-Byte-Zeilen, `op_size = dsize + (addr & 31)` → Rundung auf volle Zeilen, `DSB/ISB` am Ende | [B] |
| SDMMC1-IDMA-Ziele | libDaisy `src/hid/fatfs.h:21-25` (nur AXI SRAM, nicht DTCM); Diagnose-Praxis: Puffer in AXI/SDRAM | [B]/[P]; D2 nicht freigegeben |
| **RM0433** (System-Reset, SRAM-Erhalt, RSR-Flags, RAMECC, Bus-Matrix, BOOT_ADD) | **nicht gelesen**: der Download von st.com scheitert in dieser Umgebung (kein Browser-Download ohne Freigabe, curl liefert 0 Byte). Keine Abschnittsnummern erfunden. | offen – siehe Abschnitt 11 |

## 2. Geerbter Übergabezustand (Bootloader → Anwendung)

Die Verwaltung baut kein eigenes Sprungtrampolin. Beim Eintritt in den Reset-Handler
einer gestarteten BOOT_SRAM-Anwendung gilt der Zustand des Upstream-JUMP-Pfads [B]:
RCC im Reset-Zustand (HSI), SysTick aus, Caches aus, MPU aus, CPACR (FPU) durch den
Startcode des Bootloaders freigegeben, `SCB->VTOR = 0x24000000`, MSP = Wort 0 des
Images, Backup-SRAM zugänglich, `boot_info.version = v6_1`, `boot_info.status =
INVALID`, QSPI **nicht** memory-mapped. Das ist identisch mit dem heutigen Start
jeder Anwendung über DFU/QSPI-Kopie; nur die Quelle der Bytes (SD statt QSPI-Kopie)
und der Weg dorthin (CPU-Kopie mit Cache) sind neu.

Für die Verwaltung selbst (QSPI-Ziel) tut der Bootloader zusätzlich `HAL_Init()`
und richtet QSPI memory-mapped ein [B]; `DaisySeed::Init` überspringt deshalb
`qspi.Init` (`daisy_seed.cpp:121`), und `QSPIHandle` verweigert Schreib-/Löschzugriffe
aus QSPI-Ausführung (`qspi.cpp:971-980`). Die Taktumschaltung in `System::Init`
während XIP ist upstream vorgesehen, am Seed3 aber **[A]** (erster Probe-Test).

## 3. Speicherplan der Verwaltung

Linker: [`ld/program_manager_qspi.lds`](../ld/program_manager_qspi.lds). Prüfung:
`tools/check_qspi_image.py` (ELF-Sektionen VMA **und** LMA gegen die Regionen,
`.qspiflash_*` leer, Vektoren, Marker, BIN = QSPI-Ausdehnung).

| Region | Adressen (Ende exklusiv) | Inhalt der Verwaltung | Gemessen im Build 0.1.0 (Manager / Probe) |
| --- | --- | --- | --- |
| QSPI | `0x90040000`–`0x907E0000` | Vektoren, Code, Rodata, Init-Arrays, `.data`-Ladebild | 156 752 B / 141 128 B |
| SRAM_TARGET | `0x24000000`–`0x24078000` | **nichts** (Imageziel) | 0 B / 0 B, vom Checker erzwungen |
| SRAM_HELPER | `0x24078000`–`0x24080000` | `.pm_axi_bounce`: 4 KiB SDMMC1-IDMA-Bounce, 32-Byte-ausgerichtet | 4 096 B / 0 B |
| DTCM | `0x20000000`–`0x20020000` | `.data`, `.bss` (FatFs-Objekte, Katalogtext 16 KiB, Diagnosepuffer), Stack bis `_estack = 0x20020000`; Linker-ASSERT: ≥ 16 KiB Stackreserve | 104 200 B / 101 360 B |
| RAM_D2_DMA | `0x30000000`–`0x30008000` | libDaisy-DMA-Puffer (LED, ADC, Audio, MIDI) | 17 096 B |
| RAM_D2 | `0x30008000`–`0x30048000` | Heap (`end`); kein `malloc` in eigenem Code | 0 B belegt |
| Backup-SRAM | `0x38800000`–`0x38801000` | libDaisys `boot_info` (12 B) | 12 B |
| SDRAM | `0xC0000000`–`0xC4000000` | unbenutzt (initialisiert durch `DaisySeed::Init`) | 0 B |

Regeln: kein DMA-Ziel in DTCM (SDMMC1 erreicht es nicht [B]); D2 nicht als
SD-IDMA-Ziel ohne RM0433-Nachweis; Bounce-Puffer besitzt eigene Cachezeilen; der
logische FatFs-/Katalogpuffer in DTCM wird nur per CPU-`memcpy` aus dem Bounce
gefüllt (`common/sd/ro_sd.c:102-109` [B]).

## 4. Imageprofil `axi-sram-480k-v1`

Ein `.dsy`-Payload ist ein unverändertes BOOT_SRAM-BIN. `tools/dsy_pack.py check`
verlangt vor dem Paketieren (Begründung in Klammern):

* `.isr_vector` bei `0x24000000`; jede Sektion mit Dateiinhalt wird innerhalb
  `0x24000000`–`0x24078000` geladen (Ziel ist genau dieser Bereich);
* `.data` liegt in DTCM mit Ladeadresse im Ziel, `.bss` und `.reserved_for_stack`
  in DTCM, `_estack = 0x20020000` (Startcode kopiert `.data` aus dem Ziel, zählt
  `.bss` selbst; MSP aus Wort 0 muss in DTCM liegen);
* keine Sektion in internem Flash oder QSPI, `.qspiflash_*` leer (ein Image, das
  QSPI-Daten erwartet, hinge von der geflashten Verwaltung ab);
* NOLOAD-Sektionen innerhalb des Ziels (`.axi_bss` von Toy Piano/Physical Modeling,
  `.neb_sd` von Nebulae) enden unter `0x24078000` **und** werden mit `--noload`
  benannt: der Paketierer bestätigt damit, dass das Produkt sie selbst
  initialisiert. Der Loader nullt zwar das ganze Ziel vor dem Kopieren, das
  ersetzt aber keine Produktinitialisierung von D2-/SDRAM-/ITCM-Bereichen;
* BIN = Ladeausdehnung, 8 B ≤ Länge ≤ 491 520 B; Wort 0 in `(0x20000000, 0x20020000]`
  und 8-Byte-ausgerichtet, Wort 1 Thumb und im Payload, ELF-Entry = Resetvektor;
* Produkt-ID/Version/Marker aus `components/registry.json` (`--project`).

Der Laufzeitloader prüft unabhängig davon Header (CRC, Felder), Länge, SHA-256 und
dieselben Vektorbedingungen (`pm_image.cpp`, `pm_loader.cpp`).

## 5. Ladevertrag (`pm_loader.cpp`)

Reihenfolge je `Step()` (ein Chunk von 4 096 B Karten-I/O pro Hauptschleifendurchlauf,
Gesamtbudget 60 s, Kartenanwesenheit vor jedem Schritt):

1. **Opening**: Datei öffnen; Dateigröße muss in `160+8 … 160+491520` liegen.
2. **Header**: 160 B lesen, parsen (Magic → CRC → Felder); Produkt-ID = Katalog-ID;
   Header-Hash = Katalog-Hash; Dateigröße = 160 + Payloadlänge; dann wird das
   **ganze** 480-KiB-Ziel genullt und der SHA-256 begonnen.
3. **Payload**: Chunks direkt in das Ziel (CPU-Kopie aus dem Bounce), Hash fortlaufend;
   EOF vor der Länge → `SHORT`, Lesefehler → `READ`; danach ein Probelesen: weitere
   Bytes → `LONG`.
4. **Verify**: Hash = Header-Hash, Vektoren prüfen, Datei schließen.
5. **Finalize**: `Quiesce()` → `Clean(480 KiB)` → `Commit(0x24000000)`.

Bei jedem Fehler bleibt `boot_info` unberührt, die Datei wird geschlossen und die
Verwaltung zeigt den Fehler (`PMSTATUS e=…`, LED-Blinken). Host-Tests decken alle
Fehlerpfade, Teilreads, Abbruch, Budget und die Commit-Reihenfolge ab
(`tests/test_loader.cpp`, 77 Prüfungen).

## 6. Commit-Vertrag (`pm_main.cpp`, `DaisyBootCommit`, `SramTarget`)

| Schritt | Code | Begründung |
| --- | --- | --- |
| Quiesce | `hw.seed.StopAudio()`; LEDs aus, `UpdateLeds()` zweimal (wartet den letzten DMA-Transfer ab); DAC-Codes 0/0; `ro_sd_unmount()`, `HAL_SD_DeInit` | nach diesem Punkt schreibt kein Interrupt/DMA mehr in RAM; DAC-Code 0 ist kein elektrischer Nachweis (NICHT GEMESSEN) |
| Clean | `SCB_CleanDCache_by_Addr(0x24000000, 480 KiB)`, `__DSB(); __ISB();` | Image und Nullung wurden über den D-Cache geschrieben; der Reset verwirft den Cache. Clean (nicht Invalidate) schreibt schmutzige Zeilen zurück; CMSIS rundet auf Zeilen [B]. Der Stock-Bootloader kopiert mit **abgeschaltetem** D-Cache (`bootloader/main.cpp`: `SCB_DisableDCache()`), diese Pflicht ist daher neu und wird vom Host-Test nur als Reihenfolge, nicht als Registerwirkung bewiesen |
| Commit | `__disable_irq()`; `boot_info.data = 0x24000000; DSB; boot_info.status = JUMP; DSB; RCC->CIER = 0; HAL_NVIC_SystemReset()` | gleiche Reihenfolge wie `ResetToBootloader()` [B]; Ziel zuerst, Status zuletzt; **nicht** `ResetToBootloader()` rufen (würde den Status überschreiben). Backup-SRAM-Zugriff ist seit dem Bootloader aktiv (`InitBackupSram`) und per MPU nicht cachebar (`system.cpp:548-556`) |

Restrisiken (akzeptiert, Konzept v2 §6): der Bootloader prüft nach dem Reset weder
Hash noch Resetursache; `boot_info.data` ist dort ein freier Zeiger; nur die
Verwaltung schreibt `JUMP`, Anwendungen nie.

## 7. Rückkehr und Service

* Rückkehr aus einer Anwendung: `common/bootloader/return_entry.h` (S1+S16 3 s, SysEx
  `F0 7D 50 4D 01 01 4F 50 45 4E F7`) → Anwendung beendet Audio/MIDI/CV/Speicherung
  selbst → `manager_return::Enter()` → `ResetToBootloader(DAISY_SKIP_TIMEOUT)` →
  Bootloader startet nach 100 ms die QSPI-Anwendung (Verwaltung) über einen zweiten
  Reset. Zwischen beiden erscheint kurz DFU `0483:DF11` auf J7.
* Service/DFU: unverändert S9+S16 5 s, BL-SysEx, DDP `BOOT` → `DAISY_INFINITE_TIMEOUT`.
  In der Verwaltung ist `BOOT` während `LOADING` mit `ERR e=BUSY` gesperrt.
* Verwaltungsupdate = heutiger DFU-Weg nach `0x90040000` (`docs/FLASHING.md`); es
  läuft aus internem Flash, nie aus dem zu schreibenden QSPI.

## 8. Bedienung und Modi (`pm_manager.cpp`)

`SELECT` (Tasten 1–15 wählen, 16 öffnet Diagnose), `DIAG` (Diagnoseschicht besitzt
Tasten/LEDs/CV/Ton; S1+S16 3 s oder `PMMODE m=SELECT` zurück), `LOADING` (Tasten
ignoriert), `SERVICE`. Auswahl nur durch einen vollständigen Druck-/Loslass-Zyklus
genau einer Taste; beim Start oder Moduswechsel gehaltene Tasten müssen erst
losgelassen werden; Akkorde und Haltezeiten ≥ 3 s wählen nie. Kein Autostart eines
Programms; der Bootloader startet nach dem Einschalten die Verwaltung (2 s Fenster).
CV OUT und Testton nur in `DIAG` und nur in einer Hostsitzung; beim Verlassen von
`DIAG` werden DAC 0/0 und Ton aus gesetzt.

## 9. Kartenregel

Der unveränderte Bootloader flasht beim Start eine Datei des Wurzelverzeichnisses,
deren Name `.bin`/`.BIN` enthält, nach `0x90040000` und überschreibt damit die
Verwaltung (Upstream `SearchBin` [B]). Deshalb: **keine Datei mit `.bin` im Namen
(beliebige Schreibung) im Wurzelverzeichnis**; Programme nur als `.dsy` unter
`/DAISY/PROGRAMS/`. `tools/pm_card.py check` prüft das am PC vor dem Einlegen;
`PMINFO rootbin=1` meldet es nachträglich (für den aktuellen Start zu spät).

## 10. Grenzen von Version 1

Kein MSC in der Verwaltung (Reader bleibt separates Image), kein Autostart, kein
Watchdog (HardFault = Hänger bis zum manuellen Reset), keine Prüfung nach dem Reset,
kein Flash-Schreibschutz, DFU-Fenster 2 s bleibt, Single-Slot-Update (abgebrochenes
DFU → erneut flashen; ROM-DFU am Modulport als letzter Weg). JUCE/VST3 nachgelagert.

## 11. Offene Nachweise

| ID | Nachweis | Stand |
| --- | --- | --- |
| C1 | RM0433: System-Reset-Quellen, SRAM-Erhalt über Software-Reset, RSR-Flags, RAMECC-Verhalten, SDMMC1-Bus-Matrix, BOOT_ADD-Bereich | Dokument nicht beschaffbar in dieser Umgebung; vor dem ersten SD→SRAM-Test vom Menschen bereitstellen oder nachschlagen |
| C2 | installierter Bootloader ≙ Tag v6.4.0 (`SKIP_TIMEOUT`/`INF_TIMEOUT`-Verhalten) | NICHT GEMESSEN (B1–B14 in `docs/HARDWARE_TEST_BOOTLOADER.md`) |
| C3 | XIP-Start der Probe am Seed3 inkl. Taktumschaltung, QSPI-Chip | NICHT GEMESSEN |
| C4 | Cache-Clean + Commit: erstes SD→SRAM-Image startet und meldet sich | NICHT GEMESSEN |
| C5 | elektrischer Ruhezustand CV OUT über alle Übergänge | NICHT GEMESSEN (H14/H15 offen) |
