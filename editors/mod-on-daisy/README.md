# Mod on Daisy Editor

Grafischer Editor für die Modulationsmatrix [Mod on Daisy](../../modules/mod-on-daisy/).
Zentrales Bedienelement ist die vollständig sichtbare **8×8-Rastermatrix** nach dem
Vorbild von MatrixBrute/MiniFreak. Dazu kommen die Parameter der Quellen. Die
Verbindung zum Daisy läuft über USB-MIDI.

| Fassung | Stand |
| --- | --- |
| **JUCE-Editor** ([`src/`](src/)) | **nutzbar**: Standalone für Windows (`.exe` aus der CI), Linux und macOS per CMake. Mit eingebautem Simulator |
| **Browser-Editor** ([`web/`](web/)) | **nutzbar**: Schnellvariante für Chrome/Edge (Web MIDI), ohne Installation |
| VST3 für Studio One | später; der JUCE-Editor ist dafür vorbereitet (Kern ohne GUI, [docs/GUI.md](docs/GUI.md) § 3) |

Beide Fassungen lesen und schreiben dieselben `.modpatch`-Dateien.

## JUCE-Editor

### Windows-Programm holen

Die CI baut bei jeder Änderung `ModOnDaisyEditor.exe`:

1. Auf GitHub unter **Actions → Editor** den letzten grünen Lauf öffnen. Der Lauf
   startet nur von Hand („Run workflow“, Branch wählen).
2. Unten bei **Artifacts** `ModOnDaisyEditor-windows-x64-…` herunterladen und entpacken.
3. `ModOnDaisyEditor.exe` starten. Eine Installation ist nicht nötig, die
   Visual-C++-Laufzeit ist eingebaut.

Windows SmartScreen kann beim ersten Start warnen, weil die Datei nicht signiert ist.
Dann „Weitere Informationen“ und „Trotzdem ausführen“ wählen.

### Bedienung

1. Daisy mit Mod-on-Daisy-Firmware an J7 anschließen, zum Beispiel `ToyPiano-v0.3.bin`
   oder `PhysicalModelingMod-v0.1.0.bin`.
2. **Studio One schließen** oder dort den Port „Daisy Seed External“ deaktivieren.
   Unter Windows kann nur eine Anwendung den Port öffnen. Ist er belegt, sagt der
   Editor das.
3. Oben das Gerät wählen (ein Daisy-Port wird vorgeschlagen) und **Verbinden** klicken.
   Der Editor liest Instrument, Spalten und den aktuellen Zustand vom Gerät.

**Ohne Daisy:** „Simulator: Toy Piano“ oder „Simulator: Physical Modeling“ wählen. Das
ist der Controller- und Engine-Code der Firmware, auf dem PC übersetzt. Er antwortet wie
das Gerät, und die LFO- und Random-Anzeigen bewegen sich. Ein Simulator-Patch lässt sich
als Datei speichern und später ins echte Gerät laden.

* **Raster**:
  * Zeilen sind die 8 Quellen, Spalten die Ziele des Instruments. Unter jedem
    Spaltennamen steht, was Tiefe 100 bedeutet; „Anschlag“ heißt, das Ziel wirkt erst
    beim nächsten Ton.
  * Schraffierte Zellen sind nicht verfügbar; der Tooltip nennt den Grund.
  * Ein gelber Punkt in der Zelle heißt: noch nicht vom Gerät bestätigt.
* **Tiefe ändern**:
  * Zelle senkrecht ziehen (mit `Shift` fein) oder das Mausrad benutzen (±1, mit
    `Shift` ±10).
  * Oder den Regler bzw. das Zahlenfeld rechts.
  * Tastatur: Pfeiltasten wählen, `+`/`−` ±1, `Bild↑`/`Bild↓` ±10, `0` auf null,
    `Leertaste` ein/aus.
  * Rechtsklick: auf 0, invertieren, ein/aus.
* **Doppelklick** schaltet eine Verbindung aus und wieder ein, die Tiefe bleibt
  erhalten (Anzeige in Klammern, gestrichelter Rand).
* **Grenze**: Höchstens 8 aktive Verbindungen. Eine weitere Zelle bekommt ihren Wert,
  bleibt aber ausgeschaltet; der Editor meldet das.
* **Quellen**:
  * LFO 1/2: Form (Sinus, Dreieck, Sägezahn ↑/↓, Rechteck, S&H).
  * Sync: Frei (dann Rate und Retrigger), MIDI-Clock (dann Teilung) oder Gate (dann
    Eingang und Zyklen pro Impuls).
  * Random: Modus Stufe oder geglättet, Sync wie bei den LFOs.
* **Live-Anzeige**:
  * Die Balken unter den Quellen zeigen deren aktuellen Wert.
  * Abgedunkelt: Die Quelle hat keine aktive Verbindung; die Firmware berechnet sie dann
    nicht.
  * Volles helles Band: Ein freier LFO oder Random läuft schneller als 5 Hz. Die Anzeige
    kommt mit 20 Bildern/s und könnte ihn nur stehend zeigen.
  * Unter den Spaltennamen steht der gesamte Offset. Meldet das Instrument seine
    Potistellungen (Physical Modeling `0.1.0-mod2`), zeigt ein weißer Strich die
    Potistellung und der Balken den Weg der Modulation von dort aus. So sieht man, ob
    eine Modulation an 0 oder 1 anstößt.
  * „keine Live-Daten“ oben links: Es kommen keine Monitordaten an, meist weil eine
    andere Anwendung den Eingang hält (siehe unten).
* **Dateien**:
  * „Datei speichern …“ legt eine `.modpatch`-Datei an (JSON).
  * „Datei öffnen …“ prüft die Datei zuerst und überträgt dann das ganze Raster in einem
    Schritt und danach die Quellparameter. Ist die Datei für ein anderes Instrument,
    fragt der Editor nach. Lehnt das Gerät unterwegs etwas ab, lädt der Editor den
    Zustand neu vom Gerät.
  * Im Gerät gibt es noch keine Speicherplätze; nach dem Ausschalten ist die Matrix
    leer.
* **Kontrolle**: Jede Änderung beantwortet das Gerät mit der Prüfsumme seines Patches.
  Der Editor vergleicht sie mit seinem eigenen Stand und lädt bei einer Abweichung neu.
  Ohne Antwort wiederholt er eine Anfrage dreimal.
* **Verbindungsverlust**: Wird der Daisy abgezogen, meldet der Editor das und arbeitet
  offline weiter. Beim erneuten Verbinden gilt der Zustand des Geräts.

### Studio One und der Editor gleichzeitig

Unter Windows öffnet bisher nur **ein** Programm den Port „Daisy Seed External“.
Studio One nimmt ihn exklusiv, sobald er dort aktiv ist. Das wurde am 23.10.2026 am Gerät
beobachtet:

* Ein schon verbundener Editor steuert weiter. Er bekommt aber keine Antworten und keine
  Live-Daten mehr („keine Antwort vom Gerät“, „keine Live-Daten“).
* Nach dem Trennen lässt sich nicht neu verbinden, solange Studio One den Port hält.

**Prüfung: Windows MIDI Services.** Mit den neuen Windows MIDI Services können mehrere
Programme denselben Port gleichzeitig nutzen. Microsoft verteilt sie für Windows 11.
Ob sie auf diesem Rechner schon aktiv sind, zeigt am zuverlässigsten der Versuch:

1. Windows-Version notieren (`winver`) und die Windows-Updates einspielen.
2. Daisy an J7, den Editor schließen.
3. Studio One öffnen, „Daisy Seed External“ dort als Ein- und Ausgang aktiv lassen und
   prüfen, dass Noten vom Daisy ankommen.
4. Jetzt den Editor starten und **Verbinden**.
5. Ergebnis notieren (Laufzettel E7b):
   * **Geht**: Status grün, Live-Anzeige läuft, Änderungen werden bestätigt, und
     Studio One empfängt weiter Noten. Dann laufen beide nebeneinander.
   * **Geht nicht**: Der Editor meldet den Port als belegt.

Geht es nicht, bleibt vorerst die Umgehung: den Daisy-Port in Studio One deaktivieren,
solange der Editor läuft. Als dauerhafte Lösung kann der Editor den Daisy-Port halten
und Noten, Clock und CC von einem virtuellen Port durchreichen, den Studio One nutzt,
zum Beispiel loopMIDI. Das ist noch nicht umgesetzt.

### Selbst bauen

Voraussetzungen: CMake ≥ 3.22 und ein C++17-Compiler. Unter Windows Visual Studio 2022
mit „Desktopentwicklung mit C++“, unter Linux die JUCE-Pakete aus
`.github/workflows/editor.yml`. JUCE 9.0.4 lädt CMake selbst.

```bash
cmake -S editors/mod-on-daisy -B build/editor -DCMAKE_BUILD_TYPE=Release
cmake --build build/editor --config Release
```

Das Programm liegt dann unter
`build/editor/ModOnDaisyEditor_artefacts/Release/`.

### Tests

```bash
make -C editors/mod-on-daisy test             # Kern gegen den Firmware-Controller, ohne JUCE
make -C editors/mod-on-daisy test-sanitize    # dasselbe mit ASan/UBSan
"…/Mod on Daisy Editor" --selftest out        # Fenster gegen den Simulator, Screenshots in out/
```

* **Kerntest** (`tests/test_core.cpp`): Er läuft gegen den Controller und die Engine der
  Firmware. Geprüft werden Verbinden, Spalten, Bearbeiten, das Zusammenfassen beim
  Ziehen, die Grenze 8, Quellen, Name, Leeren, Dateien und Trennen. Dazu kommt ein Lauf
  mit 400 zufälligen Änderungen bei 5 % verlorenen und verfälschten Rahmen.
* **Selbsttest** (`--selftest`): Er bedient das echte Fenster mit Tastatur, Ziehen,
  Mausrad, Regler, Doppelklick, Quellen, Grenze, Name und Dateien. Nach jedem Schritt
  vergleicht er die Patch-Prüfsumme von Fenster und simuliertem Gerät. Ergebnis und
  Screenshots schreibt er nach `out/`; unter Windows steht das Ergebnis in
  `out/selftest.txt`.

Die CI (`.github/workflows/editor.yml`) führt beides unter Linux (Xvfb) und Windows aus
und stellt die `.exe` bereit. Sie startet nur von Hand: GitHub → Actions → **Editor** →
„Run workflow“.

### Lizenz

Der Editor nutzt [JUCE](https://juce.com). JUCE steht unter der AGPLv3 oder einer
kommerziellen JUCE-Lizenz. Für den eigenen Gebrauch spielt das keine Rolle. Bevor die
`.exe` an andere weitergegeben wird, ist zu entscheiden, ob der Editor unter AGPLv3
veröffentlicht oder eine JUCE-Lizenz genutzt wird.

## Browser-Editor

1. Daisy anschließen und Studio One schließen, wie oben.
2. `web/index.html` in **Chrome oder Edge** öffnen. Beide Dateien (`index.html` und
   `mod_protocol.js`) müssen im selben Ordner liegen.
   * Falls der Browser bei einer lokal geöffneten Datei keinen MIDI-Zugriff anbietet,
     über einen lokalen Server öffnen: `python -m http.server 8000` im Ordner `web/`,
     dann `http://localhost:8000`.
3. Die Frage nach dem **MIDI-Zugriff mit SysEx** erlauben.
4. Ein- und Ausgang „Daisy Seed External“ wählen (wird vorgeschlagen) und
   **Verbinden** klicken.

Firefox und Safari werden nicht unterstützt. Die Bedienung entspricht dem JUCE-Editor,
ohne Rechtsklickmenü. **Demo (offline)** (oder `index.html#demo`) zeigt das Raster mit den
Toy-Piano-Spalten und einem Beispiel-Patch.

```bash
node editors/mod-on-daisy/web/tests/protocol.test.cjs   # Codec gegen die C++-Referenz (auch in CI)
make -C modules/mod-on-daisy test-editor                  # Editor in Headless-Chromium gegen mod_sim
```

`mod_sim` (`modules/mod-on-daisy/tools/mod_sim.cpp`) ist der echte Controller- und
Engine-Code der Firmware auf dem PC, mit den Spalten von Toy Piano bzw. Physical
Modeling. Der End-to-End-Test bedient den Editor wie ein Mensch: Tastatur, Ziehen,
Mausrad, Doppelklick, Quellen, Grenze, Dateien. Nach jedem Schritt prüft er, dass
Editor und simuliertes Gerät dieselbe Patch-Prüfsumme haben.
