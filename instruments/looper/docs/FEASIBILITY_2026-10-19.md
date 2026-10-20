# Stardust on Daisy — Machbarkeitsstudie der Original-Firmware

**Prüfstand:** 19. Oktober 2026  
**Untersuchungsgegenstand:** `Stardust_v1_1_1.bin`, Qu-Bit Stardust (Referenzhardware: Daisy Seed2 DFM), Zielhardware: Electro-Smith Seed3 Desktop Dev Kit  
**Status:** **Statische Offline-Analyse abgeschlossen; DSP-Innereien und Hardwareport bleiben offen.**

> **Entscheidung:** **GO** für eine eigenständige C++-Nachbildung auf dem Seed3 mit der Originalfirmware als Referenz; **HOLD** für unverändertes Flashen oder „binären Direktport“ der Stardust-BIN auf den Seed3. Die Studie weist eine passende Prozessor-/Speicherarchitektur nach, **nicht** die Kompatibilität des Originalprogramms mit dem Desktop-Board.

## 1. Prüfumfang und Beweiskraft

Untersucht wurden das vom Nutzer bereitgestellte Binary, das Handbuch v1.1.0 (24 Seiten), die zwei Board-Fotos sowie die aktuellen öffentlichen Dokumentationen von Qu-Bit und Daisy. **Tatsächlich ausgeführt:** SHA-256, Dateigrößen- und Strukturprüfung, Cortex-M-Vektortabelle, Disassemblierung des Reset-Pfads mit LLVM (`thumbv7em`, Cortex-M7), Prüfung des Initialisierungs-Literalpools, Analyse lesbarer Zeichenketten und Vergleich der Hardwareschnittstellen.

**Nicht ausgeführt:** Hardwareausführung, automatisierte Gesamtrekonstruktion aller Funktionen, vollständige Datenflussanalyse, exakte Rekonstruktion der Looper-/Effektalgorithmen, Audio-Referenzmessungen, Überprüfung des Original-Quellcodes (nicht vorhanden) oder ein Flash-Versuch. Die Grenzen sind absichtlich streng angegeben.

**Evidenzklassen:** `NACHGEWIESEN` = direkt im Binary/Disassembly oder Handbuch belegt; `PLAUSIBEL` = technische Interpretation; `OFFEN` = mit den bisherigen Mitteln nicht entschieden.

## 2. Prüfsumme und Firmwareformat

| Prüfpunkte | Befund | Evidenz |
|---|---|---|
| Datei | `Stardust_v1_1_1.bin` | NACHGEWIESEN |
| Größe | **221.280 Byte** (216,09 KiB) | NACHGEWIESEN |
| SHA-256 | `e7556ffd3bbfa578b7061e4b06de6d010d4842115439121eea458e2e6cf1a48a` | NACHGEWIESEN |
| Eingebetteter Versionsstring | `v1.1.1`, bei Offset **0x35344** | NACHGEWIESEN |
| Produktstring | `Qu-Bit Stardust`, bei **0x3534C** | NACHGEWIESEN |
| Container | **Roher Cortex-M-Programmabbildtyp**, kein ELF-Header | NACHGEWIESEN |
| Cortex-M Stackpointer | **0x20020000** | NACHGEWIESEN |
| Reset-Vektor | **0x24001895**, Thumb-Bit gesetzt | NACHGEWIESEN |
| Tatsächlicher Start der Reset-Routine | **0x24001894** = Datei-Offset 0x1894 | NACHGEWIESEN |
| Wahrscheinliche Ladebasis | **0x24000000** (AXI-SRAM) | SEHR GUT BEGRÜNDET |
| Äußere Kompression/Verschlüsselung | Kein Anzeichen im Startheader; Thumb-Code lesbar | NACHGEWIESEN nur für die äußere Struktur |
| Unverändertes Flashen auf Seed3 | **Nicht freigegeben** | HOLD |

Die Vektortabelle ist konsistent: `0x20020000` ist die Obergrenze des 128-KiB-DTCM-Bereichs im STM32H7-Kontext; der Reset-Eintrag zeigt mit gesetztem Thumb-Bit an eine tatsächlich disassemblierbare Routine innerhalb der Datei. **Das Binary ist nicht „nur Daten“ und nicht von außen verschlüsselt.** Ob einzelne Tabellen komprimiert sind, wurde nicht gesondert geprüft.

### 2.1 Disassemblierter Boot-Pfad

Der ARM-Thumb-Disassembler rekonstruiert am Reset-Vektor die folgende nachvollziehbare Sequenz:

```
0x24001894  LDR  r0, [PC,...]            ; Zielanfang initialisierter Daten
0x24001896  LDR  r2, [PC,...]            ; Zielende
...
0x240018A0  BL   0x24032DDC              ; Kopierfunktion, memcpy-artig
...
0x240018AE  MOVS r1, #0
0x240018B0  BL   0x24032E2C              ; Nullsetzen, memset-artig
0x240018B4  BL   0x24032D84              ; Startup-Initialisierung
0x240018B8  BL   0x24016008              ; main-artiger Anwendungseinstieg
0x240018BC  B    0x240018BC              ; Falls Anwendung zurückkehrt
```

Die Bezeichnungen „memcpy-artig“, „memset-artig“ und „main-artig“ sind **Funktionsinterpretationen** anhand der übergebenen Register, nicht im Original vorhandene Symbols. Das Firmware-Binary ist ohne nützliche Funktionsnamen ausgeliefert. Das separate, eigens für die Analyse erzeugte ELF ist **ausschließlich ein Disassemblierungs-Wrapper**, kein Build und keine lauffähige Portierung.

### 2.2 Rekonstruierter interner RAM-Bedarf

Aus fünf 32-Bit-Konstanten im Reset-Literalpool, an Datei-Offsets `0x18C0–0x18D0`, ergibt sich:

| Bereich | Startadresse | Endadresse | Größe | Aussage |
|---|---|---|---:|---|
| Initialisierte Daten (Ziel) | `0x24044000` | `0x24044414` | **1.044 B** | Startup-Kopie plausibel belegt |
| Datenquelle innerhalb Image | `0x24035C40` | `0x24036054` | **1.044 B** | Liegt innerhalb der BIN |
| Nullinitialisierter Bereich | `0x24044420` | `0x24070838` | **181.272 B** | Startup-Zeroing plausibel belegt |
| Rest bis AXI-SRAM-Ende | `0x24070838` | `0x24080000` | **63.432 B** | **Nicht** pauschal „freier RAM“ (Stack/Heap/sonstige Bereiche offen) |

Die Binärdatei endet bei `0x24036060`. Sie enthält damit ein konsistentes Initialisierungsabbild. **Das Startup-Schema unterstützt klar die These einer für den SRAM-Boot vorbereiteten Daisy-Anwendung.** Der genaue verwendete Bootloader und die gewählte Linker-Konfiguration sind durch diese Beobachtung allein noch nicht identifiziert.

### 2.3 Externes SDRAM: Initialisierung sichtbar, Looper-Aufteilung nicht

In der Hauptinitialisierung um `0x240160A0–0x240160C0` ist diese Folge erkennbar:

```
0x240160A0  MOV.W r0, #0xC0000000
0x240160A8  MOV.W r2, #0x04000000  ; 64 MiB
0x240160C0  BL    0x24032E2C     ; zuvor als Nullsetzfunktion identifiziert
```

Zusammen mit `r1 = 0` spricht die Sequenz **stark dafür, dass beim Start die gesamte 64-MiB-SDRAM-Region genullt wird**. Das ist ein wesentlich stärkerer Hardwarebeleg als bloßes Vorkommen einer zufälligen Speicheradresse im Binary.

**Wichtig: Ein Warnsignal gegen Scheinbefunde.** Die Wörter `0xC2400000` und `0xC2700000` könnten auf den ersten Blick als externe RAM-Pointer erscheinen. Die Disassemblierung zeigt, dass sie hier **Float32-Konstanten −48.0 und −60.0** darstellen. Deshalb dürfen rohe 32-Bit-Trefferlisten nicht als Speicherbeleg ausgegeben werden. Eine **konkrete Zuordnung** von Aufnahme-, Playback-, Undo-/Redo- und Arbeitsbuffers lässt sich aus dem bisher rekonstruierten Bootcode **nicht** ableiten.

## 3. Rückschlüsse auf Bibliotheken und Datenverwaltung

Lesbare Zeichenketten im rohen Binärimage (jeweils **Dateioffset**, nicht RAM-Adresse):

| Offset | Nachgewiesene Zeichenkette | Eng zulässiger Rückschluss |
|---:|---|---|
| `0x3467C` | `../../libs/coreJSON/source/core_json.c` | `coreJSON` wurde für die Firmware eingebunden |
| `0x34C88` | `/loop.wav` | WAV-Datei wird referenziert |
| `0x34C94` | `/temp.wav` | Temporärer Dateiname vorhanden |
| `0x34CA4` | `0:/test.txt` | Pfad auf logischem Volume `0:` |
| `0x34E14` | `1:/options.json` | JSON-Einstellungen auf Volume `1:` |
| `0x34E24` | `stardust[%d].name` | Nummerierte Optionsnamen |
| `0x34E38` | `stardust[%d].value` | Nummerierte Optionswerte |
| `0x34E4C` | `1:/StardustFileLog.txt` | Logging-Pfad vorhanden |
| `0x34E78` | `No WAV files found...` | UI-/Fehlerfall für leere WAV-Auswahl |
| `0x34E90` | `1:/loop.wav` | Loop-Dateipfad |
| `0x34E9C` | `~zzz-StardustLoop-` | Basisname für gespeicherte Loops |
| `0x34EB0` | `1:/%s%03u.wav` | Formatvorlage für fortlaufend nummerierte WAV-Dateien |
| `0x34EC0` | `temp.wav` | Zweiter temporärer Dateiname |

Ferner tauchen die Optionen `Mix Mode`, `Constant Power`, `Transition`, `Linear`, `Nova Output`, `Effect CV Lock`, `Reverb Type`, `Erase Button Response`, `Undo Button Response`, `Smooth Size Control`, `Start CV Destination` und `Frippertronics Decay` auf. Die Gruppen `Analog`, `Digital`, `Reverb`, `Filter` sind ebenfalls vorhanden.

**Schluss:** Datei- und Konfigurationslogik sind **statisch greifbar**. Die Zeichenketten beweisen **nicht**, welcher FatFs-Volume-Index welchem physischen USB-/SD-Gerät entspricht, wie viele Dateien gleichzeitig offen sind oder ob Dateien im Audiothread gelesen werden. Diese Zuordnung erfordert Datenflussanalyse oder Laufzeitbeobachtung.

## 4. Looper-/DSP-Architektur: Stand der Rekonstruktion

Was man sicher aus dem Handbuch v1.1.0 weiß, ist **kein** bereits disassemblierter Algorithmus. Diese Trennung ist zentral:

| Funktion | Beleg für Verhalten | Algorithmus im Binary rekonstruiert? | Portierungsrisiko |
|---|---|---|---|
| Stereo-Record / Playback | Handbuch S. 4, 9 | **Nein** | Mittel |
| Sound on Sound / Replace | Handbuch S. 15 | **Nein** | Mittel |
| Frippertronics / Resample | Handbuch S. 15 | **Nein** | Hoch (Rückkopplung, Reihenfolge) |
| Undo / Redo | Handbuch S. 11 | **Nein** | Hoch (Speicher und Atomizität) |
| Varispeed 0,25–8× | Handbuch S. 11 | **Nein** | Mittel (Interpolation, Aliasing) |
| Inertia | Handbuch S. 11 | **Nein** | Mittel (Rampe, Kurvenform) |
| Slice / Skip | Handbuch S. 13 | **Nein** | Hoch (Wahrscheinlichkeitslogik) |
| Wow/Flutter, Hiss, Saturation | Handbuch S. 8, 14 | **Nein** | Mittel bis hoch (Klangtreue) |
| Downsample/Bitcrush, Filter, Reverb | Handbuch S. 14 | **Nein** | Mittel |
| WAV-Import/Export | Handbuch S. 17–20 + Strings | **Nur Datenpfade sichtbar** | Mittel |
| Clock-Sync und Freeze | Handbuch S. 14, 18 | **Nein** | Mittel |

Damit ist die wichtigste Grenze erreicht: **Es wäre unseriös, aus diesen Befunden behaupten zu wollen, die Undo-Struktur oder die Effektalgorithmen seien bereits dekompiliert.** Dafür braucht man Funktionsanalyse, Cross-References, Tracing und idealerweise Vergleichsaufnahmen des echten Geräts.

### 4.1 43/87-Sekunden-Hypothese richtig einordnen

Die Qu-Bit-Release-Notes v1.1.1 dokumentieren einen Fehler: Der Import von Loops länger als ungefähr **43 Sekunden** konnte Audio durch Inhalt des Undo-Buffers ersetzen; Version 1.1.1 soll das beheben. Das Handbuch v1.1.0 nennt eine WAV-Importgrenze von **1:27 Minuten** bei 16 Bit/48 kHz.

Rechnerisch gilt bei **48 kHz, Stereo, Float32 intern (8 Byte pro Frame)**:

| Speicher | Audiodauer |
|---|---:|
| 16 MiB | **43,69 Sekunden** |
| 32 MiB | **87,38 Sekunden** |
| 64 MiB | **174,76 Sekunden** |

Das ist eine **interessante Übereinstimmung**, aber **kein Beweis** für interne Puffergrößen. Zusätzlich beansprucht eine **16-Bit-WAV-Datei** für 87 Sekunden nur circa **16 MiB**; Import-Dateigröße und interner Float32-Bedarf dürfen nicht verwechselt werden. Loop-Persistenz, Undo-Differenzen, Resampling, Alignment und mehrere Working-Buffer verändern die Bilanz. Die bisherige Hypothese „32-MiB-Aufnahme + 32-MiB-Undo“ bleibt **unbestätigt**.

**Gezielter nächster RE-Schritt:** Funktions-/Speicherzugriffspfad der WAV-Importfunktion um die nachgewiesenen Dateinamen finden; `Read`-Längen, Speichergrenzen und SDRAM-Zielzeiger verfolgen; erst anschließend das 43-Sekunden-Thema bewerten.

## 5. Seed2 DFM → Seed3: tatsächliche Portierungsrisiken

Die fotografierte Stardust-Platine trägt „Daisy Seed2-DFM“; libDaisy nennt dafür den **PCM3060** als Audio-Codec. Das Seed3 verwendet **TAC5242**. Beide verwenden die Daisy-Plattform, aber die physischen Erweiterungsplatinen sind verschieden.

| Teilsystem | Vergleich | Bewertung |
|---|---|---|
| CPU-ISA / Rechenleistung | Cortex-M7 / 480 MHz | **GO** für eigenständigen Build |
| Externes RAM | 64-MiB-Klasse | **GO** für Buffer-Prototyp; Aufteilung testen |
| Prozessoradresse / Startup | SRAM-basierte Applikation bei `0x24000000` | **GO** für Seed3 **mit neuem Build/Linkerskript**, nicht für fremde Original-BIN |
| Audio-Codec | Seed2 DFM PCM3060 vs. Seed3 TAC5242 | **HOLD** für direkte BIN-Verwendung |
| GPIO/ADC/LEDs | Stardust-Trägerplatine != Desktop-Kit | **HOLD** für direkte BIN-Verwendung |
| Datenträger | Stardust USB-A-Host vs. Desktop microSD | Eigenständige Datei- und UI-Anpassung nötig |
| Stereo-I/O | Beide haben Stereo-I/O, aber andere Beschaltung | **GO** nach Pegel-/Routingtest |
| Clock/CV/Gate | Weniger dedizierte Eingänge am Desktop | Funktionsabbildung auf MIDI/Bedienelemente nötig |
| Bootloader | SRAM-Boot-Image erkennbar | Für Seed3 Boot-Pfad separat bauen und prüfen |
| Effekttreue | Original DSP nicht rekonstruiert | **OFFEN**, nur mit Hör-/Referenztests bewertbar |

DaisyBootloader unterstützt laut offizieller Dokumentation Anwendungsimages, die aus dem SRAM ausgeführt werden, sowie die Speicherung der Firmware im QSPI-Flash. Der Reset-Vektor der Original-BIN ist damit **vereinbar**, beweist jedoch keine konkrete Bootloader-Version. **Niemals das Stardust-Binary direkt auf dem Seed3 flashen.**

## 6. Drei technische Wege und Entscheidung

| Weg | Technischer Aufwand | Risiko | Empfehlung |
|---|---|---|---|
| **A: Original-BIN unverändert flashen** | Geringer Versuch, hoher Fehlerrisiko | Falsche Codec-, GPIO- und Dateitreiberinitialisierung | **NO-GO** |
| **B: Binary-Patching/Rehosting** | Sehr hoch: H7-Peripherie, Codec, Dateisystem, Bedienung, Pointer/Interrupts | Kaum Wartbarkeit, keine Quellen | **HOLD** – höchstens als Forschungsprojekt |
| **C: Neue libDaisy-C++-Implementierung** | Mittel bis hoch | Kontrollierbar, hardwaregerecht, testbar | **GO** |

**Fazit:** Der Nachweis einer Daisy-basierten Originalfirmware mit identischer CPU-Klasse und vergleichbarem SDRAM reduziert das **Plattformrisiko** stark. Das 216-KiB-Binary liefert wertvolle Referenzen für Bootmodell, Datenverwaltung, Optionen und spätere Reverse-Engineering-Fragen. Es liefert aktuell **keine extrahierten, wiederverwendbaren DSP-Algorithmen**. Ein sauberer Stardust-on-Daisy-Clone ist der effektivste Entwicklungsweg; das Binary bleibt Forschungs- und Verhaltensreferenz.

## 7. Konkreter Entwicklungs- und Prüfplan (mit Gates)

1. **S0 – Referenzstand fixieren (PASS):** SHA-256 und Versionskennung protokolliert; Analyse-Skripte reproduzierbar; kein Hardwareflash durchgeführt.
2. **S1 – Zielboard-Hardware (HOLD bis getestet):** Mit bestehender funktionierender Seed3-Umgebung Stereo-Loopback bei 48 kHz, ADC/Taster, SD und Speicherzuweisung einzeln testen. PASS nur bei störungsfreier Audiowiedergabe und reproduzierbarer SDRAM-Nutzung.
3. **S2 – Minimal-Looper:** Aufnahme, kontinuierliche Wiedergabe, Overdub und Reverse, noch ohne Dateien; Testgenerator und Block-Grenzfälle. PASS: klickfreie Loop-Grenzen, keine Bufferüberläufe, konstante DSP-Deadline.
4. **S3 – Speicher und Undo:** Zweiten Puffer / Änderungsverlauf mit explizitem Budget statt Annahmen entwickeln. PASS: Aufnahme/Undo/Redo bei 42, 44, 86 und 88 Sekunden und vollständige Speichergrenztests; Tests dürfen auch bewusst abgewiesene Längen als PASS einstufen, **sofern** Daten und Zustand unbeschädigt bleiben.
5. **S4 – Effekte / Loop-Modi:** Frippertronics, Replace, Resample, Slice/Skip, Varispeed und FX inkrementell. PASS: deterministische Referenztests, kein Echtzeit-Allocation und keine Blocküberschreitungen.
6. **S5 – microSD / MIDI / Bedienung:** Dateizugriffe vom Audio-Callback trennen; Dateiformatvalidierung, Hot-/Cold-Boot und Fehlerpfade; 16 Tasten / 8 Potis / Kippschalter abbilden. PASS: kein Audioaussetzer während SD-Operationen; sichere Behandlung beschädigter und überlanger Dateien.

**Stoppregel:** Falls Seed3 die notwendige Aufnahme-/Undo-Qualität bei 48 kHz trotz vorsichtiger SDRAM-Aufteilung oder sicheren Dateizugriffen nicht erreicht, Speicherkonzept ändern, bevor zusätzliche Effektfunktionen implementiert werden. Die Original-BIN wird unter keinen Umständen als vermeintlicher schneller Ersatz verwendet.

## 8. Reproduzierbarkeit

Das Analysepaket enthält zwei **Standard-Python-Skripte ohne Drittanbieterabhängigkeiten**:

- `stardust_static_analysis.py`: Vektortabelle, SHA-256, Initialisierungsadressen, Strings, Entropie → `stardust_evidence.json`.
- `stardust_wrap_elf.py`: Erstellt **nur** einen Analysecontainer mit ARM-ELF-Metadaten und identischem Payload; optional zur Disassemblierung mit `llvm-objdump`. **Nicht flashen.**

```bash
python stardust_static_analysis.py Stardust_v1_1_1.bin -o stardust_evidence.json
python stardust_wrap_elf.py Stardust_v1_1_1.bin -o analysis_only.elf
llvm-objdump -d --triple=thumbv7em --mcpu=cortex-m7 \
    --start-address=0x24001890 --stop-address=0x240018c0 analysis_only.elf
```

Die gelieferten Skripte und Metadaten enthalten **keine Kopie des proprietären Firmware-Binaries**. Die vom Nutzer bereitgestellte BIN ist unverändert geblieben.

## 9. Referenzen

**Primärquellen:** vom Nutzer gelieferte Datei `Stardust_v1_1_1.bin` (SHA-256 oben), Fotos Vorder-/Rückseite, `QB_Stardust_v1-1-0.pdf` (Handbuch v1.1.0), insbesondere S. 4, 9–20.

**Offizielle externe Dokumentation (eingesehen 19.10.2026):**

- Qu-Bit, Stardust-Produktbeschreibung: https://www.qubitelectronix.com/shop/p/stardust
- Qu-Bit, Firmware v1.1.1 inkl. 43-Sekunden-Bugfix: https://www.qubitelectronix.com/alternate-firmware/p/stardust-v111
- Electro-Smith, Daisy Seed3: https://docs.daisy.audio/hardware/Seed3/
- Electro-Smith, Seed3 Desktop Dev Kit: https://docs.daisy.audio/product/Seed3-Desktop-Dev-Kit/
- libDaisy, Board-Versionen und Codec-Zuordnung: https://github.com/daisyaudio/libDaisy/blob/master/src/daisy_seed.h
- DaisyBootloader: https://github.com/daisyaudio/DaisyBootloader

---
**Prüfurteil:** **GO für eigenständige Portierung des Funktionsumfangs. HOLD für Direktübernahme der Originalfirmware.** Der nächste entscheidende Erkenntnisgewinn ist nicht ein weiterer ungezielter Binärscan, sondern entweder (1) gezieltes Reverse Engineering der WAV-/Undo-Pfade mit einem Decompiler oder (2) ein erster Seed3-Looper mit belastbaren Audiotests.
