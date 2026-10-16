# Prüfbericht — Toy Piano v0.1

Stand: 16.10.2026, Linux x86_64, Cloud-Umgebung ohne angeschlossenes Daisy-Kit.

## Tatsächlich ausgeführt

| Prüfung | Ergebnis |
| --- | --- |
| Offizielle Desktop-API und Hardwaretabellen | `Hardware`, Button-/LED-Indizes, VR1/VR2 und Bootloader-Auswahl abgeglichen |
| Echter ARM-Cross-Compile | Erfolgreich mit xPack GCC 13.2.1, Cortex-M7 Hard Float |
| BOOT_SRAM-BIN | 106.308 Bytes; Stack `0x20020000`, Reset-Vektor `0x24000691` |
| ELF | ELF32, ARM, EABI5, Hard Float; Startadresse im SRAM |
| Speicherbudget | SRAM 106.308 / 491.520 Bytes; DTCM 62.812 / 131.072 Bytes; DMA-RAM 17.096 / 32.768 Bytes |
| Alle 16 MIDI-Zuordnungen | Exakt gegen die angeforderte Tabelle geprüft, einschließlich doppeltem C5 |
| Alle 16 tatsächlichen Oszillatorfrequenzen | Host-Audiorender: Nulldurchgangszählung über eine Sekunde, Abweichung < 1,1 Hz |
| AD-Hüllkurven | Min./Mitte/Max. für Attack × Decay: samplegenaue Laufzeit, monotoner Verlauf, exakter Endwert null |
| Retrigger | Pegel bleibt beim Trigger erhalten; erneuter Attack während Attack und Decay geprüft |
| Zeitkennlinien | Endwerte, geometrische Mitte und monotoner Verlauf über 1001 Positionen geprüft |
| Poti-Rauschfilter | ±0,05 % simuliertes Rauschen unterdrückt, beide Endanschläge erreichbar |
| 16 unabhängige Stimmen | Polyphoner Render gegen Summe von 16 Einzelinstanzen verglichen, einschließlich Retrigger einer C5-Taste |
| Pegel | Test-Peak 0,279373; keine nichtendlichen Werte; mathematische Obergrenze 0,8 |
| Speicher-/Zugriffsfehler im DSP | Derselbe Host-Test mit AddressSanitizer + UndefinedBehaviorSanitizer bestanden |

Prüfsumme der geprüften Firmware:

```text
995ccc4f8a7fc60c8b5ea224810cadd275175ea014186b4829b1d6ecbbbcc2a6  ToyPiano-v0.1.bin
```

Host-Tests: `make test`. Binärprüfung:
`python3 scripts/check-firmware.py build/ToyPiano-v0.1.bin`.
Sanitizer-Aufruf:

```bash
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Isrc -IDaisySP/Source tests/test_toy_piano.cpp src/toy_piano.cpp \
    -o build/host/test_sanitized
./build/host/test_sanitized
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

## GitHub

Die Repository-Erstellung wurde versucht. Die verbundene Integration verweigert
`createRepository` mit „Resource not accessible by integration“. Das Ziel bleibt
das dedizierte private `sscheidl/Daisy-Seed`; kein anderes Projekt wurde verwendet.
Solange es nicht angelegt/freigegeben ist, sind Push, Pull Request und ein
GitHub-gehostetes Actions-Artefakt nicht möglich. Der Workflow ist vorbereitet,
aber noch nicht auf GitHub ausgeführt.
