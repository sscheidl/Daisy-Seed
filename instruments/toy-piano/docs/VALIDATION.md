# Prüfbericht — Toy Piano v0.1

Stand: 17.10.2026, Linux x86_64, Cloud-Umgebung ohne angeschlossenes Daisy-Kit.

Projektpfad: `instruments/toy-piano/`. Nach der Umstrukturierung erneut über den
Root-Dispatcher und den Projekt-Makefile gebaut/getestet: Die BIN ist bytegleich
mit dem Stand vor dem Verzeichnisumzug (106.388 Bytes, Prüfsumme unten).
DSP und Callback wurden beim Umzug nicht verändert.

## Tatsächlich ausgeführt

| Prüfung | Ergebnis |
| --- | --- |
| Offizielle Desktop-API und Hardwaretabellen | `Hardware`, Button-/LED-Indizes, VR1/VR2 und Bootloader-Auswahl abgeglichen |
| Echter ARM-Cross-Compile | Erfolgreich mit xPack GCC 13.2.1, Cortex-M7 Hard Float |
| BOOT_SRAM-BIN | 106.388 Bytes; Stack `0x20020000`, Reset-Vektor `0x24000691` |
| ELF | ELF32, ARM, EABI5, Hard Float; Startadresse im SRAM |
| Speicherbudget | SRAM 106.388 / 491.520 Bytes; DTCM 62.812 / 131.072 Bytes; DMA-RAM 17.096 / 32.768 Bytes |
| Alle 16 MIDI-Zuordnungen | Exakt gegen die angeforderte Tabelle geprüft, einschließlich doppeltem C5 |
| Alle 16 tatsächlichen Oszillatorfrequenzen | Host-Audiorender: Nulldurchgangszählung über eine Sekunde, Abweichung < 1,1 Hz |
| AD-Hüllkurven | Min./Mitte/Max. für Attack × Decay: samplegenaue Laufzeit, monotoner Verlauf, exakter Endwert null |
| Retrigger | Pegel bleibt beim Trigger erhalten; erneuter Attack während Attack und Decay geprüft |
| Zeitkennlinien | Endwerte, geometrische Mitte und monotoner Verlauf über 1001 Positionen geprüft |
| Poti-Rauschfilter | ±0,05 % simuliertes Rauschen unterdrückt, beide Endanschläge erreichbar |
| Callback/Entprellung | Echter Callback mit gepinntem DigitalControl: Kontaktaussetzer von 1–6 ms an jeder der 16 Tasten lösen keinen Doppelanschlag aus; nach bestätigtem Loslassen funktioniert der nächste Anschlag |
| Callback/LED-Mailbox | Einschaltanschlag nach sieben Abtastungen, alle 16 Bits gleichzeitig, LED-Maske bleibt nach AD-Ende gesetzt, Stereo identisch; keine LED-Übertragung im Callback |
| 16 unabhängige Stimmen | Polyphoner Render gegen Summe von 16 Einzelinstanzen verglichen, einschließlich Retrigger einer C5-Taste |
| Pegel | Test-Peak 0,279373; keine nichtendlichen Werte; mathematische Obergrenze 0,8 |
| Speicher-/Zugriffsfehler | DSP- und Callback-Test mit AddressSanitizer + UndefinedBehaviorSanitizer bestanden, Fehlerbehandlung mit `-fno-sanitize-recover=all` |

Prüfsumme der geprüften Firmware:

```text
a37bddb180ea530e58d2f3c5b4c061c3d5660bae900c8ef93a92e6101346ffe0  ToyPiano-v0.1.bin
```

Host-Tests: `make PROJECT=toy-piano test`. Binärprüfung:
`python3 scripts/check-firmware.py instruments/toy-piano/build/ToyPiano-v0.1.bin`
(im Repository-Hauptverzeichnis).
Sanitizer-Aufruf (auch im CI-Workflow enthalten):

```bash
make PROJECT=toy-piano test-sanitize
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
Anschlag, auch wenn alle 16 Tasten gleichzeitig anschlagen. Oszillatorphasen und
Hüllkurvenpegel bleiben beim Retrigger kontinuierlich. Loslassflanken verändern
nur die LED-Maske. Links und rechts erhalten denselben berechneten Samplewert.

Das ist eine Codeprüfung, keine Messung von CPU-Auslastung oder Worst-Case-Laufzeit
auf dem Seed3. Ein fehlender Hardwaretest wird durch Host-Tests nicht ersetzt.

## Build-Warnungen

Die unveränderten offiziellen Makefiles/Startup-Dateien und Newlib erzeugen mit
dieser Toolchain Warnungen zu unimplementierten `nosys`-Dateideskriptorfunktionen,
einem RWX-SRAM-Ladesegment sowie beim Startup zur FPU/Interrupt-Attributkombination.
Der Link und die Binärerzeugung sind erfolgreich. Die Anwendung nutzt keine
Dateideskriptoren. Das SRAM-Ausführungsmodell stammt aus dem offiziellen
BOOT_SRAM-Linkerskript. Eigener DSP-Code kompiliert im Host-Test mit
`-Wall -Wextra -Werror`.

## Noch am Gerät zu prüfen

* Externer Bootloader vorhanden und Flash über den Träger-USB-Port erfolgreich.
* Physisch jede Taste S1–S16 mit LED D1–D16, keine verdrehte Reihenfolge am eigenen Board.
* Tasterprellen, Mehrfachdrücke und schnelle Wiederanschläge bei realen Schaltern.
* VR1/VR2: Drehrichtung, reale ADC-Endwerte, Störgeräusche und subjektiv passende Zeitverläufe.
* Stereo-/Kopfhörerausgabe, Stimmung, Klang, Lautstärke und Klickarmut.
* 16-Stimmen-Betrieb unter gleichzeitiger Bedienung ohne Aussetzer;
  tatsächliche Callback-Laufzeit und CPU-Reserve.

Die genaue Testfolge steht in der README. Keine bekannten Fehler im geprüften
DSP-Verhalten; analoge Eigenschaften und die reale Audiowiedergabe bleiben ungetestet.
Weitere bekannte Grenzen (sehr kurze LED-Anzeigen, mögliche Blockade bei einem
LED-DMA-Startfehler) stehen in [HARDWARE.md](../../../docs/HARDWARE.md).
