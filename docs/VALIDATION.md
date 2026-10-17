# Prüfbericht — Toy Piano v0.2

Stand: 16.10.2026, Linux x86_64, Cloud-Umgebung ohne angeschlossenes Daisy-Kit.

## Tatsächlich ausgeführt

| Prüfung | Ergebnis |
| --- | --- |
| Offizielle Desktop-API und Hardwaretabellen | `Hardware`, Button-/LED-Indizes, VR1/VR2 und Bootloader-Auswahl abgeglichen |
| Echter ARM-Cross-Compile | Erfolgreich mit xPack GCC 13.2.1, Cortex-M7 Hard Float |
| BOOT_SRAM-BIN | 125.428 Bytes, Version 0.2 eingebettet; Stack `0x20020000`, Reset-Vektor `0x24000749` |
| ELF | ELF32, ARM, EABI5, Hard Float; Startadresse im SRAM |
| Speicherbudget | SRAM 125.428 / 491.520 Bytes; DTCM 116.208 / 131.072 Bytes (88,7 %, davon 37 KB `MidiUsbHandler`); DMA-RAM 18.084 / 32.768 Bytes |
| Alle 16 MIDI-Zuordnungen | Exakt gegen die angeforderte Tabelle geprüft, einschließlich doppeltem C5 |
| Alle 16 tatsächlichen Oszillatorfrequenzen | Host-Audiorender: Nulldurchgangszählung über eine Sekunde, Abweichung < 1,1 Hz |
| AD-Hüllkurven | Min./Mitte/Max. für Attack × Decay: samplegenaue Laufzeit, monotoner Verlauf, exakter Endwert null |
| Retrigger | Pegel bleibt beim Trigger erhalten; erneuter Attack während Attack und Decay geprüft |
| Zeitkennlinien | Endwerte, geometrische Mitte und monotoner Verlauf über 1001 Positionen geprüft |
| Poti-Rauschfilter | ±0,05 % simuliertes Rauschen unterdrückt, beide Endanschläge erreichbar |
| 16 unabhängige Stimmen | Polyphoner Render gegen Summe von 16 Einzelinstanzen verglichen, einschließlich Retrigger einer C5-Taste |
| Pegel (Tasten) | Test-Peak 0,279373; keine nichtendlichen Werte; lineare Obergrenze 0,8 |
| Speicher-/Zugriffsfehler im DSP | Derselbe Host-Test mit AddressSanitizer + UndefinedBehaviorSanitizer bestanden |
| MIDI-Stimmen | Tonhöhe (Nulldurchgänge, Noten 24–108), lineare Velocity, ungültige Noten/Velocity ignoriert, Zuordnung der 16 Stimmen, Klau der ältesten, Neustart derselben Note auf derselben Stimme, Pegel- und Phasenstetigkeit beim Klau/Retrigger |
| Tasten und MIDI gleichzeitig | Summe aus Tasten- und MIDI-Instanz stimmt mit gemeinsamer Instanz überein (lineare Unabhängigkeit) |
| Begrenzer | Monoton, ungerade, immer unter 0,8 (auch für 1e12), Kleinsignalverstärkung innerhalb 0,5 %, bei 32 Stimmen nie darüber, Stille exakt 0 |
| Aliasing | Zweite Harmonische vorhanden bei Note 100, bei Note 127 (2f über Nyquist) abgeschaltet: Spiegelanteil < 1 % |
| Lockfreie Warteschlange | Reihenfolge, voll/leer, Umlauf; 300.000 Elemente zwischen zwei Threads; ThreadSanitizer sauber, Gegenprobe mit relaxed-Ordnung meldet ein Data Race |
| Versionierung | `make -s version` = `src/version.h`; Versionswechsel ohne `make clean` ändert Dateiname und Image-Eintrag; Skript lehnt falschen Dateinamen und manipulierte Version ab; Rückbau liefert bitgleiches Image |
| Mutationsprüfung der Tests | Sieben gezielte Defekte im DSP (Aliasing-Schutz, Stimmenklau, gleiche Note, Limiter, Velocity, Hüllkurven-Reset, Phasen-Reset) werden jeweils von einem Test erkannt |

Prüfsumme der geprüften Firmware:

```text
6de9be19ea5673b579edbc8b62734d80b4d064327435c204b0b7ae70023fdcd7  ToyPiano-v0.2.bin
```

Host-Tests: `make test`. Binärprüfung:
`python3 scripts/check-firmware.py` (prüft zusätzlich Dateiname, eingebettete Version und `src/version.h`).
Sanitizer-Aufrufe (ASan/UBSan und TSan):

```bash
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Isrc -IDaisySP/Source tests/test_toy_piano.cpp src/toy_piano.cpp \
    -o build/host/test_sanitized
./build/host/test_sanitized
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=thread \
    -Isrc -IDaisySP/Source tests/test_toy_piano.cpp src/toy_piano.cpp \
    -o build/host/test_tsan
./build/host/test_tsan
```

## Echtzeitprüfung am Quelltext

Der Audiocallback besitzt feste Schleifengrenzen und statischen Speicher.
Keine Allokation, Dateizugriffe, Konsolenausgabe oder LED-I²C-Übertragung im Callback.
Die offizielle Shiftregisterabfrage enthält lediglich die kurzen, fest begrenzten
GPIO-Taktwartezeiten des Hardwaretreibers. Die LED-Funktion mit potenziell
wartendem Buffer-Swap läuft im Hauptprogramm.

Sinustabellen und Tonfrequenzen werden vor Audiostart berechnet. Pro Sample gibt
es nur aktive Stimmen, interpolierte Tabellenzugriffe und einfache AD-Arithmetik.
Zwei logarithmische Zeitumrechnungen erfolgen höchstens einmal pro Block bei einem
Anschlag (Taste oder MIDI), auch wenn alle 16 Tasten gleichzeitig anschlagen.
MIDI-Noten kommen über eine lockfreie Warteschlange (höchstens 16 pro Block). USB-Empfang
läuft im USB-Interrupt, Senden und Parsen im Hauptprogramm. Im Audiocallback gibt es
nur Aufrufe der Steuer- und DSP-Funktionen; Wurzel und Division im Begrenzer sind
Hardwarebefehle (Disassembly geprüft, kein libm-Aufruf). Oszillatorphasen und
Hüllkurvenpegel bleiben beim Retrigger kontinuierlich. Loslassflanken verändern
nur die LED-Maske. Links und rechts erhalten denselben berechneten Samplewert.

Das ist eine Codeprüfung, keine Messung von CPU-Auslastung oder Worst-Case-Laufzeit
auf dem Seed3. Grobe Schätzung aus dem Maschinencode: etwa 100–130 Zyklen je aktiver
Stimme, bei allen 32 Stimmen also bis etwa 40 % des 1-ms-Blockbudgets bei 480 MHz. Ein fehlender Hardwaretest wird durch Host-Tests nicht ersetzt.

## Build-Warnungen

Die unveränderten offiziellen Makefiles/Startup-Dateien und Newlib erzeugen mit
dieser Toolchain Warnungen zu unimplementierten `nosys`-Dateideskriptorfunktionen,
einem RWX-SRAM-Ladesegment sowie beim Startup zur FPU/Interrupt-Attributkombination.
Der Link und die Binärerzeugung sind erfolgreich. Die Anwendung nutzt keine
Dateideskriptoren. Das SRAM-Ausführungsmodell stammt aus dem offiziellen
BOOT_SRAM-Linkerskript. Eigener DSP-Code kompiliert im Host-Test mit
`-Wall -Wextra -Werror`.

## Hardwaretest (Betreiber, gemeldet)

Für die Tastenfunktionen (v0.1) am Kit gemeldet und nicht von der Cloud-Prüfung erfasst:
Flashen über J7, alle 16 Tasten, AD-Hüllkurve, Polyphonie, schnelles Spielen,
LED-Zuordnung, gleiche Tonhöhe von S8/S9, Drehrichtung von VR1/VR2, kein Knacken.

## Noch am Gerät zu prüfen

* **USB-MIDI:** Gerät „Daisy Seed External“ erscheint; Tasten senden Note On/Off auf Kanal 1;
  Empfang mit Halbtönen, Velocity, 16 und mehr Noten gleichzeitig; Verhalten der Tasten ohne
  USB-Verbindung (Netzteil/Ladegerät); erneutes Verbinden im laufenden Betrieb; Studio-One-Ablauf
  nach [MIDI.md](MIDI.md).
* **Callback-Laufzeit und CPU-Reserve** mit 16 Tasten und 16 MIDI-Stimmen gleichzeitig,
  Aussetzer und Knacken bei MIDI-Bursts.
* **Stack-Reserve:** Das DTCM ist zu 88,7 % belegt; es bleiben etwa 14,8 KB für den Stack.
* Tasterprellen bei realen Schaltern (siehe Hinweis zu Mehrfachauslösung durch einzelne
  Störimpulse bei gehaltener Taste und zu einer beim Einschalten gehaltenen Taste im Review).
* Stereo-/Kopfhörerausgabe im Vergleich, Lautstärke durch den Begrenzer bei Akkorden.

Die genaue Testfolge steht in der README. Keine bekannten Fehler im geprüften
DSP-Verhalten; analoge Eigenschaften und die reale USB-Anbindung bleiben ungetestet.

## GitHub

Das dedizierte private Repository ist `sscheidl/Daisy-Seed`. Nach der manuellen
Einrichtung und Freigabe ist der Zugriff verfügbar. Der Firmwarebranch basiert auf
dem vorhandenen initialen `main`-Commit; kein anderes Projekt wurde verwendet.
Der Workflow erstellt nach erfolgreichen Prüfungen ein GitHub-Actions-Artefakt
mit BIN, ELF und Prüfsumme. Die Prüfungsergebnisse des lokalen Builds stehen oben.
