# Mod on Daisy — Speicherung im QSPI-Flash (Spezifikation)

**Stand:** 23. Oktober 2026 · **Status:** reine Spezifikation (D8). **Es werden keine
Flash-Schreibfunktionen implementiert**, bis Aufteilung und Verfahren freigegeben sind.
Bis dahin antwortet die Firmware auf `STORE`/`RECALL` mit `NAK 9` (PROTOCOL § 3.4).

## 1. Ausgangslage (`BELEGT`)

| Fakt | Quelle |
| --- | --- |
| libDaisy konfiguriert für **alle** Seed-Versionen, also auch Seed3, den Baustein **IS25LP064A** (8 MiB) im Memory-Mapped-Modus ab `0x90000000` | `libDaisy/src/daisy_seed.cpp` `ConfigureQspi()`, `system.cpp` `QSPI_SIZE 0x800000` |
| Bei BOOT_SRAM initialisiert die Anwendung QSPI selbst (`qspi.Init`, weil das Programm nicht aus QSPI läuft) | `daisy_seed.cpp` |
| Der Bootloader lässt die **ersten vier 64-KiB-Sektoren** (`0x90000000–0x9003FFFF`) unberührt und behält sie künftigen Funktionen vor | `libDaisy/doc/md/_a7_Getting-Started-Daisy-Bootloader.md` |
| Programme liegen ab `0x90040000`. Beim Flashen löscht der Bootloader in **64-KiB-Blöcken**. Daten hinter dem Programm müssen auf einer 64-KiB-Grenze jenseits des Programmcodes beginnen | ebd. |
| BOOT_SRAM-Programme sind höchstens **480 KiB** groß (SRAM 512 KiB − 32 KiB für den Bootloader) | ebd., `STM32H750IB_sram.lds` |
| Lösch-Höchstzeiten: 4-KiB-Sektor **400 ms**, 64-KiB-Block **1000 ms** | `flash_IS25LP064A.h` |
| `QSPIHandle::Erase()` nimmt 64-KiB-Blöcke, wenn ausgerichtet und groß genug, sonst 4-KiB-Sektoren; `EraseSector()` löscht genau 4 KiB | `qspi.cpp` |
| `PersistentStorage` löscht und schreibt ohne CRC und ohne Doppelablage | `util/PersistentStorage.h` |
| libDaisy legt **keine MPU-Region** für `0x90000000` an | `sys/system.cpp` |

`OFFEN`: Ob jeder Seed3 tatsächlich einen IS25LP064A trägt, wird in Phase 2 über die
JEDEC-ID geprüft. Das ist ein Lesekommando, kein Schreibzugriff.

## 2. Speicheraufteilung

```text
0x90000000 ┌───────────────────────────────┐
           │ Bootloader-Reserve (256 KiB)  │  nie benutzen
0x90040000 ├───────────────────────────────┤
           │ Firmware-Image (BOOT_SRAM)    │  ≤ 480 KiB → belegt höchstens bis 0x900BFFFF
0x900C0000 ├───────────────────────────────┤  (in 64-KiB-Blöcken gelöscht)
           │ frei, ≈ 7,1 MiB               │  nicht von Mod on Daisy benutzt
0x907E0000 ├───────────────────────────────┤
           │ MOD-Block P: 8 Patch-Slots    │  64 KiB = 16 × 4-KiB-Sektor (je Slot A/B)
0x907F0000 ├───────────────────────────────┤
           │ MOD-Block S: Einstellungen    │  2 × 4 KiB (A/B) + 56 KiB Reserve
0x90800000 └───────────────────────────────┘
```

| Bereich | Adresse | Inhalt |
| --- | --- | --- |
| Slot k, Sektor A | `0x907E0000 + k · 0x2000` | Patch-Datensatz |
| Slot k, Sektor B | `0x907E1000 + k · 0x2000` | Patch-Datensatz |
| Einstellungen A | `0x907F0000` | Geräte-ID, CV-Kalibrierung, Einschalt-Slot |
| Einstellungen B | `0x907F1000` | wie A |
| Reserve | `0x907F2000–0x907FFFFF` | künftige Erweiterungen, z. B. mehr Slots |

**Begründung**: Die beiden obersten 64-KiB-Blöcke haben den größten Abstand zu jedem
Image, liegen auf 64-KiB-Grenzen und werden beim Flashen einer BOOT_SRAM-Firmware nie
berührt. Die Bootloader-Reserve bleibt frei.

**Absicherung beim Bau** (Teil der späteren Implementierung): Das ELF-Prüfskript bricht
ab, wenn

* das Ende des Images im QSPI-Adressraum ≥ `0x907E0000` liegt (für BOOT_SRAM durch die
  480-KiB-Grenze ausgeschlossen, für künftige BOOT_QSPI-Images relevant), oder
* eine `.qspiflash_text`/`.qspiflash_data`-Sektion nicht leer ist. Während einer
  Lösch- oder Schreiboperation ist QSPI nicht lesbar; Code oder Daten dort würden
  einfrieren.

**Gemeinsamer Bereich aller Instrumente**: Jede Firmware sieht dieselben 8 Slots. Jeder
Datensatz trägt deshalb Instrument-Key-CRC und Schema-Hash. Ein fremder Datensatz wird
nie geladen (`RECALL` → Status 3). `STORE` auf einen Slot mit fremdem Datensatz
verlangt ein Bestätigungsbit, das der Editor erst nach Rückfrage setzt. Vor dem Wechsel
auf ein anderes Instrument kann der Editor alle Slots als Dateien sichern.

## 3. Datensätze

Jeder 4-KiB-Sektor enthält **genau einen** Datensatz am Sektoranfang. Der Rest bleibt
gelöscht (`0xFF`).

| Offset | Größe | Feld |
| ---: | ---: | --- |
| 0 | 4 | Magic `'M','O','D','R'` |
| 4 | 1 | Typ: 1 = Patch, 2 = Einstellungen |
| 5 | 1 | Formatversion des Datensatzes (1) |
| 6 | 2 | Länge der Nutzdaten n |
| 8 | 4 | Sequenznummer (u32, je Slot fortlaufend) |
| 12 | 4 | CRC-32 des Instrument-Keys (Einstellungen: 0) |
| 16 | 4 | Schema-Hash (Einstellungen: 0) |
| 20 | 4 | CRC-32 der Nutzdaten |
| 24 | 4 | CRC-32 der Bytes 0–23 (Kopf) |
| 28 | n | Nutzdaten: serialisierter `Patch` (109 B; Format 2 mit LFO-Quermodulation 111 B, ARCHITECTURE § 3.3) bzw. Einstellungen |
| 252 | 4 | **Commit-Wort** `0x434F4D54` („COMT“), wird zuletzt geschrieben |

Der Datensatz passt in eine 256-Byte-Programmierseite; das Commit-Wort steht am Ende
dieser Seite. **Einstellungen (Nutzdaten 16 B)**: `u8` Geräte-ID, `u8` Einschalt-Slot
(0xFF = Standardpatch), `u16` reserviert, `float` CV-Offset 1 und 2, `u32` reserviert.

**Gültig** ist ein Datensatz nur, wenn **alle** Bedingungen zutreffen:

* Magic stimmt,
* Kopf-CRC stimmt,
* Länge ≤ 224,
* Nutzdaten-CRC stimmt,
* Commit-Wort stimmt,
* Typ und Formatversion sind bekannt.

## 4. Verfahren

### 4.1 Lesen (beim Start und bei `RECALL`)

1. Beide Sektoren des Slots prüfen.
2. Gültig ist der Datensatz mit der **höheren** Sequenznummer. Der Vergleich ist
   überlaufsicher: `int32_t(a − b) > 0`.
3. Sind beide ungültig, ist der Slot leer.
4. Bei einem Patch zusätzlich prüfen: Instrument-Key-CRC und Schema-Hash müssen
   passen, und `Validate(patch, info)` muss bestehen. Sonst wird nicht geladen.

Gelesen wird über den Memory-Mapped-Zeiger (`qspi.GetData`), nur im Main-Loop. Nach
eigenen Schreibvorgängen wird vorher der D-Cache des Sektors invalidiert (§ 5).

### 4.2 Schreiben (`STORE`)

```text
Vorbedingung: Main-Loop, ausdrücklicher Befehl, Patch validiert.
1. Zielsektor = der Sektor des Slots, der NICHT den aktuell gültigen Datensatz enthält
   (sind beide ungültig: A). seq = höchste gültige Sequenz + 1 (sonst 1).
2. EraseSector(Ziel)                            ≤ 400 ms; löscht nur den veralteten Datensatz
3. D-Cache des Sektors invalidieren; prüfen, dass die ersten 256 Byte = 0xFF sind.
4. Kopf + Nutzdaten programmieren (Bytes 0 … 27+n), Commit-Wort noch NICHT.
5. Cache invalidieren; Kopf-CRC und Nutzdaten-CRC zurücklesen und prüfen.
6. Commit-Wort programmieren (eine 4-Byte-Programmierung, 1→0-Übergänge).
7. Cache invalidieren; Datensatz vollständig nach § 3 prüfen.
8. Antwort STORE|0x40 mit Status 0 und CRC; bei einem Fehler in 2–7: NAK 10.
   Der alte Datensatz ist dann weiterhin gültig.
```

**Stromausfallsicherheit**: Der jeweils gültige Datensatz wird **nie** im selben Vorgang
gelöscht oder überschrieben. Bricht der Strom in Schritt 2–6 ab, ist der neue Datensatz
unvollständig (CRC oder Commit fehlt) und wird ignoriert; der alte bleibt gültig. Nach
Schritt 6 ist der neue Datensatz gültig und hat die höhere Sequenz. Zu keinem Zeitpunkt
sind beide Sektoren gleichzeitig ungültig, es sei denn, der Slot war schon vorher leer.

### 4.3 Abnutzung

Jedes Speichern löscht einen Sektor, abwechselnd A und B. Bei der typischen
Lebensdauer von NOR-Flash (≥ 100 000 Löschzyklen pro Sektor; Datenblattwert `OFFEN`,
prüfen) ergibt das ≥ 200 000 Speichervorgänge pro Slot. Weil nur auf ausdrücklichen
Befehl gespeichert wird (**kein Autosave**), ist das unkritisch.

### 4.4 Einschalten

1. Einstellungen lesen (A/B). Ungültig → Standard: Geräte-ID 0, Kalibrierung 0, kein
   Einschalt-Patch.
2. Ist ein Einschalt-Slot gesetzt und nach § 4.1 gültig und passend → laden, sonst den
   leeren Standardpatch (keine aktive Verbindung). Damit klingt das Instrument wie ohne
   Mod on Daisy.
3. Der Ladezustand (geladen, leer, fremd, ungültig) wird für `IDENTIFY` und den Editor
   festgehalten. Nichts wird stillschweigend umgedeutet.

## 5. Laufzeitbedingungen und Risiken

| Thema | Festlegung |
| --- | --- |
| Kontext | Nur im Main-Loop, nie im Audio-IRQ |
| Audio während des Löschens | läuft weiter, weil Code und Daten bei BOOT_SRAM in SRAM/DTCM liegen. Die Messfirmware prüft den Überlaufzähler während `STORE` (Abnahme) |
| QSPI während des Vorgangs | libDaisy schaltet für Erase/Write vom Memory-Mapped- in den indirekten Modus und danach zurück. In dieser Zeit darf **nichts** den Bereich `0x90000000` lesen, auch kein Interrupt (ELF-Prüfung § 2) |
| D-Cache | Keine MPU-Region für QSPI; nach der ARMv7-M-Standardkarte ist der Bereich cachebar (Write-Through). Deshalb `SCB_InvalidateDCache_by_Addr` über den Sektor nach dem Löschen und nach dem Programmieren, vor jedem Zurücklesen |
| Spekulative Zugriffe | `OFFEN`: Beim STM32H7 können spekulative Lesezugriffe auf den QSPI-Adressraum im indirekten Modus Probleme verursachen. Vor der Implementierung prüfen; mögliche Abhilfe ist eine MPU-Region, die den Bereich während des Vorgangs sperrt |
| Blockierter Main-Loop | bis zu ~400 ms (Höchstwert, typisch weniger). USB-MIDI wird so lange nicht abgeholt; die libDaisy-FIFO (256 Ereignisse) kann überlaufen und **verwirft dann neue Ereignisse**. LED-Dienst und USB-Senden pausieren. → Speichern nur auf Befehl; der Editor zeigt „Speichere … keine MIDI-Daten senden“ und wartet bis zu 2 s auf die Antwort |
| Firmware-Wechsel | Flashen per DFU, Web-Programmer oder SD löscht nur die Blöcke des neuen Images (`OFFEN`, im Hardwaretest bestätigen: Patch speichern, andere Firmware flashen, Patch noch vorhanden) |

## 6. Verworfene Alternativen

| Alternative | Grund |
| --- | --- |
| `daisy::PersistentStorage` | keine CRC, löscht vor dem Schreiben den einzigen Datensatz (nicht stromausfallsicher), Größe an eine Struktur gebunden |
| Bootloader-Reserve `0x90000000–0x9003FFFF` | vom Bootloader für künftige Funktionen reserviert |
| Bereich direkt hinter dem Image | hängt von der Image-Größe ab und verschiebt sich zwischen Instrumenten |
| Backup-SRAM (4 KiB) | ohne Batterie nicht dauerhaft |
| Interner Flash (128 KiB) | enthält den Bootloader |
| microSD | nicht bei jedem Instrument genutzt und nicht immer eingesteckt; später als zusätzliches Backend hinter derselben Schnittstelle möglich |

## 7. Prüfplan für die spätere Umsetzung

1. **Host, ohne Hardware**: Ein NOR-Modell erzwingt die Löschgranularität und erlaubt
   beim Programmieren nur 1→0. Bei jedem einzelnen Lösch- und Programmierschritt wird
   ein Stromausfall eingespeist. Nach jedem simulierten Neustart muss **genau der alte
   oder der neue** Datensatz gültig sein. Dazu Bit-Kippen in jedem Feld (Erkennung durch
   CRC) und Überlauf der Sequenz.
2. **Hardware**:
   * 1000 × Speichern und Laden;
   * Strom während `STORE` trennen (20 Versuche);
   * Überlaufzähler und Callback-Spitzen während `STORE`;
   * andere Firmware per DFU flashen, Slots müssen erhalten bleiben;
   * JEDEC-ID lesen.
3. Erst danach wird `STORE` im Protokoll freigeschaltet (IDENTIFY, Bit 0).
