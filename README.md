# Daisy Seed — Toy Piano v0.1

Spielbare C++-Firmware für das **Electro-Smith Seed3 Desktop Development Kit**:
16 unabhängige Toy-Piano-Stimmen, Taster-LEDs und zwei Regler für die AD-Hüllkurve.
48 kHz, mono auf beiden Stereoausgängen und am Kopfhörerausgang.

Außerdem im Repository: die [Hardware-Diagnose](diagnostics/README.md)
(eigene Firmware + Windows-Anwendung) unter `diagnostics/`.

## Spielen

| Taster | Ton | MIDI-Note | Taster | Ton | MIDI-Note |
| --- | --- | --- | --- | --- | --- |
| S1 | C4 | 60 | S9 | C5 | 72 |
| S2 | D4 | 62 | S10 | D5 | 74 |
| S3 | E4 | 64 | S11 | E5 | 76 |
| S4 | F4 | 65 | S12 | F5 | 77 |
| S5 | G4 | 67 | S13 | G5 | 79 |
| S6 | A4 | 69 | S14 | A5 | 81 |
| S7 | B4 (deutsch: H4) | 71 | S15 | B5 (deutsch: H5) | 83 |
| S8 | C5 | 72 | S16 | C6 | 84 |

Die erste Zeile am Gerät ist S1–S8, die zweite S9–S16. S8 und S9 spielen bewusst
denselben Ton, besitzen aber eigene Stimmen. Die Noten stehen in
[`src/config.h`](src/config.h) und können dort geändert werden.

* **VR1:** Attack von 2 bis 300 ms, links kurz, rechts lang.
* **VR2:** Decay von 50 bis 3000 ms, links kurz, rechts lang.
* Beide Zeitkennlinien sind logarithmisch. Der ADC verwendet 32-faches Oversampling;
  der eigene PotFilter glättet über 20 ms und ergänzt Hysterese.
  Neue Einstellungen gelten für den nächsten Anschlag. Bereits laufende Hüllkurven
  behalten ihre Zeiten, damit keine Parametersprünge entstehen.
* Jeder Druck startet die eigene AD-Hüllkurve. Loslassen unterbricht sie nicht.
  Halten erzeugt keinen Sustain und keine Wiederholungen.
  Eine beim Einschalten gehaltene Taste löst nach etwa 7 ms ab Audiostart einen Ton aus.
* D1–D16 zeigen die entprellten Tastenzustände an, unabhängig vom Ausklingen.
* VR3–VR8, Kippschalter, MIDI, SD, CV/Gates und Audioeingänge haben keine musikalische Funktion.
  **RV1** ist der separate analoge Kopfhörer-Lautstärkeregler, nicht VR1.

Der Klang besteht aus einem Sinus mit 12 % zweiter Harmonischer. Ein erneuter
Anschlag setzt weder Oszillatorphase noch aktuellen Hüllkurvenpegel abrupt zurück.
Die feste Pegelskalierung schützt auch bei 16 gleichphasigen Stimmen vor digitalem
Clipping; einzelne Noten sind dadurch vergleichsweise leise.
S8 und S9 haben unabhängige Oszillatorphasen: Beim gemeinsamen Spielen können
sich Grundtöne abhängig vom Anschlagzeitpunkt teilweise auslöschen.

## Bauen

Benötigt: Git, GNU Make, ARM-GCC einschließlich Newlib und C++-Bibliothek;
für die optionalen Tests zusätzlich g++, für die Binärprüfung Python 3.

```bash
git clone https://github.com/sscheidl/Daisy-Seed.git
cd Daisy-Seed
git switch feat/toy-piano-v0.1
git submodule update --init libDaisy DaisySP
git -C libDaisy submodule update --init --recursive --jobs 4

# Linux x86_64: festgelegte Toolchain lokal installieren, ohne sudo
bash scripts/install-toolchain.sh
export PATH="$PWD/.tools/xpack-arm-none-eabi-gcc-13.2.1-1.1/bin:$PATH"

make -j4
make test
python3 scripts/check-firmware.py build/ToyPiano-v0.1.bin
```

`make` baut fehlende Bibliotheken automatisch. Ergebnis:
**`build/ToyPiano-v0.1.bin`**, dazu ELF, HEX und Linker-Map.
Die Bibliotheken sind offizielle Git-Submodule mit festgelegten Commits;
bitte kein `git submodule update --remote` für einen reproduzierbaren Build verwenden.
Weitere Informationen: [Build und Abhängigkeiten](docs/BUILD.md).

## Flashen

Die Anwendung verwendet wie das offizielle Desktop-Template **`APP_TYPE=BOOT_SRAM`**.
Sie benötigt den externen Daisy-USB-DFU-Bootloader. Die beiden USB-C-Anschlüsse
haben unterschiedliche Aufgaben:

1. **Nur bei fehlendem/falschem Bootloader:** einmalige Installation über den
   USB-C-Anschluss **direkt am Seed3-Modul**.
2. **Firmware-Updates und normaler Betrieb:** USB-C-Anschluss **J7 am Desktop-Träger**.
   RESET drücken, danach BOOT innerhalb der zweisekündigen Bootloader-Wartezeit.
   Im [Daisy Web Programmer](https://flash.daisy.audio/) die `.bin` unter
   „Custom“ auswählen und programmieren.

Die vollständigen getrennten Schritte stehen in [FLASHING.md](docs/FLASHING.md).
Ein bereits passender Bootloader muss nicht neu installiert werden.
In diesem Cloud-Auftrag wurde keine Hardware geflasht.

## Erster Hardwaretest

1. Kopfhörer an J5 oder einen Verstärker an die Line-Ausgänge anschließen.
   Lautstärke zunächst niedrig einstellen. Träger über J7 versorgen und Firmware flashen.
2. VR1 und VR2 nach links drehen. S1 bis S16 einzeln drücken:
   aufsteigende Töne und jeweils die zugehörige LED erwarten. S8/S9 müssen gleich hoch sein.
3. VR2 nach rechts drehen, kurz anschlagen und loslassen:
   LED erlischt nach der Entprellung, Klang klingt weiter aus.
4. VR1 nach rechts drehen und neu anschlagen: deutlich langsamerer Anschwellvorgang.
5. Mehrere Tasten gleichzeitig spielen; S8/S9 auch nacheinander anschlagen.
   Stimmen sollen unabhängig ausklingen. Eine Taste länger als 3,3 s halten:
   Klang endet, LED bleibt bis zum Loslassen an.
6. Beide Audioausgänge vergleichen, schnelle Wiederholungen und Potidrehungen testen.
   Auf Knacken, Störgeräusche, falsche LEDs und Aussetzer achten.

## Prüfstatus

Ein echter ARM-Cross-Compile sowie Host-Tests wurden in der Cloud ausgeführt.
Die Binärdatei hat eine geprüfte BOOT_SRAM-Vektortabelle.
[Prüfbericht](docs/VALIDATION.md) trennt die Softwareprüfungen von noch offenen
Hardwaretests. Die tatsächliche Audioqualität, ADC-Stabilität und Callback-Laufzeit
am Seed3 wurden mangels angeschlossener Hardware nicht gemessen.

Der GitHub-Actions-Workflow baut und testet das Projekt nach dem Push und stellt
BIN, ELF und SHA-256 als Build-Artefakt bereit. Der lokale Build benötigt kein GitHub Actions.

## Aufbau und Referenzen

* `src/main.cpp`: offizielle Desktop-Hardware, Flankenerkennung, Audio und LED-Mailbox.
* `src/toy_piano.*`: unabhängige Stimmen, AD-Hüllkurven und interpolierte Sinustabelle.
* `src/config.h`: Noten, Zeiten, Klang und Pegel.
* `tests/`: Host-Tests ohne zusätzliches Testframework.
* [Hardwaredetails und offizielle Quellen](docs/HARDWARE.md).

Keine Samples, Effekte, MIDI-Steuerung, Presets, Display- oder Netzwerkfunktionen.
