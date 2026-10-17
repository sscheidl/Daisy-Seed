# Prüfbericht — Toy Piano v0.2

Stand: 17.10.2026, Linux x86_64, Cloud-Umgebung ohne angeschlossenes Daisy-Kit.

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
| Callback/Entprellung | Echter Callback mit gepinntem DigitalControl: Kontaktaussetzer von 1–6 ms an jeder der 16 Tasten lösen keinen Doppelanschlag aus; nach bestätigtem Loslassen funktioniert der nächste Anschlag |
| Callback/USB-MIDI | Echter Callback und echte Hauptschleifenfunktion gegen Host-Attrappen: Tasten senden `90/80 nn` (Velocity 100, Kanal 1) in Tastenreihenfolge, nichts ohne eingerichteten USB-Host, kein zweites Note On bei Kontaktaussetzern; Empfang nur für Note On mit Velocity > 0 auf jedem Kanal, höchstens 16 Noten je Block in Reihenfolge, Überlauf der Warteschlange ohne Folgen, keine Rückspiegelung empfangener Noten |
| Callback/LED-Mailbox | Einschaltanschlag nach sieben Abtastungen, alle 16 Bits gleichzeitig, LED-Maske bleibt nach AD-Ende gesetzt, Stereo identisch; keine LED-Übertragung im Callback |
| 16 unabhängige Stimmen | Polyphoner Render gegen Summe von 16 Einzelinstanzen verglichen, einschließlich Retrigger einer C5-Taste |
| Pegel (Tasten) | Test-Peak 0,279373; keine nichtendlichen Werte; lineare Obergrenze 0,8 |
| Speicher-/Zugriffsfehler | DSP- und Callback-Test mit AddressSanitizer + UndefinedBehaviorSanitizer bestanden, Fehlerbehandlung mit `-fno-sanitize-recover=all` |
| MIDI-Stimmen | Tonhöhe (Nulldurchgänge, Noten 24–108), lineare Velocity, ungültige Noten/Velocity ignoriert, Zuordnung der 16 Stimmen, Klau der ältesten, Neustart derselben Note auf derselben Stimme, Pegel- und Phasenstetigkeit beim Klau/Retrigger |
| Tasten und MIDI gleichzeitig | Summe aus Tasten- und MIDI-Instanz stimmt mit gemeinsamer Instanz überein (lineare Unabhängigkeit) |
| Begrenzer | Monoton, ungerade, immer unter 0,8 (auch für 1e12), Kleinsignalverstärkung innerhalb 0,5 %, bei 32 Stimmen nie darüber, Stille exakt 0 |
| Aliasing | Zweite Harmonische vorhanden bei Note 100, bei Note 127 (2f über Nyquist) abgeschaltet: Spiegelanteil < 1 % |
| Lockfreie Warteschlange | Reihenfolge, voll/leer, Umlauf; 300.000 Elemente zwischen zwei Threads; ThreadSanitizer sauber, Gegenprobe mit relaxed-Ordnung meldet ein Data Race |
| Versionierung | `make -s version` = `src/version.h`; Versionswechsel ohne `make clean` ändert Dateiname und Image-Eintrag; Skript lehnt falschen Dateinamen und manipulierte Version ab; Rückbau liefert bitgleiches Image |
| Mutationsprüfung der Tests | Sieben gezielte Defekte im DSP (Aliasing-Schutz, Stimmenklau, gleiche Note, Limiter, Velocity, Hüllkurven-Reset, Phasen-Reset) werden jeweils von einem Test erkannt |

Prüfsumme der geprüften Firmware:

```text
5a4efcc26a675cd266b4876e29dfee658f6a6e2575b656f5847580cf59eaf13a  ToyPiano-v0.2.bin
```

Host-Tests: `make test`. Binärprüfung:
`python3 scripts/check-firmware.py` (prüft zusätzlich Dateiname, eingebettete Version und `src/version.h`).
Sanitizer-Aufrufe (auch im CI-Workflow enthalten):

```bash
make test-sanitize
make test-thread
```

Der Callback-Test verwendet den unveränderten Anwendungscode und libDaisys echte
DigitalControl-Klasse. GPIO/ADC, Systemzeit und Audio-/LED-Hardware sind Host-Stubs.
Mit der ursprünglichen Bedingung aus `9e0a975` scheitert der Test am zusätzlichen
Hüllkurvenanstieg nach einem Kontaktaussetzer. Mit der Haltezustandsprüfung besteht er.
Die Tests bilden weder reale Schaltermechanik noch die LED-DMA-Hardware ab.

Die Nulldurchgangsprüfung ist ein grober Frequenztest, kein Präzisions-Stimmungstest.
Die Binärprüfung kontrolliert Größe, Stack- und Reset-Vektor und garantiert keinen
erfolgreichen Start auf Hardware.

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

Für v0.2 (USB-MIDI) am Kit mit Studio One gemeldet: Das Kit erscheint als „Daisy Seed External“,
Studio One spielt das Board (Empfang), und die Tasten S1–S16 kommen als MIDI in Studio One an
(Senden). Zunächst fehlte das Keyboard in der Eingangsliste der Spur; nach dem Löschen und
Neuanlegen des Geräts unter neuem Namen in Studio One funktionierte es. Ursache war die
Geräteeinrichtung in Studio One, nicht die Firmware.

## Noch am Gerät zu prüfen

* **USB-MIDI:** Empfang mit Halbtönen, unterschiedlicher Velocity, 16 und mehr Noten gleichzeitig;
  Akkorde beim Senden (mehrere Tasten in derselben Millisekunde); Verhalten der Tasten ohne
  USB-Verbindung (Netzteil/Ladegerät); erneutes Verbinden im laufenden Betrieb.
* **Callback-Laufzeit und CPU-Reserve** mit 16 Tasten und 16 MIDI-Stimmen gleichzeitig,
  Aussetzer und Knacken bei MIDI-Bursts.
* **Stack-Reserve:** Das DTCM ist zu 88,7 % belegt; es bleiben etwa 14,8 KB für den Stack.
* Tasterprellen bei realen Schaltern. Kontaktaussetzer von 1–6 ms bei gehaltener Taste sind im
  Callback-Test abgedeckt, auch für die MIDI-Ausgabe (ein Druck sendet genau ein Note On).
* Stereo-/Kopfhörerausgabe im Vergleich, Lautstärke durch den Begrenzer bei Akkorden.

Die genaue Testfolge steht in der README. Keine bekannten Fehler im geprüften
DSP-Verhalten; analoge Eigenschaften, die reale Audiowiedergabe und die reale USB-Anbindung
bleiben ungetestet.
Weitere bekannte Grenzen (sehr kurze LED-Anzeigen, mögliche Blockade bei einem
LED-DMA-Startfehler) stehen in [HARDWARE.md](HARDWARE.md).
