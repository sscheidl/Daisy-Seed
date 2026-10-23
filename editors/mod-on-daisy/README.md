# Mod on Daisy Editor

Grafischer Editor für die Modulationsmatrix [Mod on Daisy](../../modules/mod-on-daisy/).
Zentrales Bedienelement ist die vollständig sichtbare **8×8-Rastermatrix** nach dem
Vorbild von MatrixBrute/MiniFreak. Dazu kommen die Parameter der Quellen. Die
Verbindung zum Daisy läuft über USB-MIDI.

| Fassung | Stand |
| --- | --- |
| **Browser-Editor** ([`web/`](web/)) | **nutzbar**: Schnellvariante für Chrome/Edge (Web MIDI), ohne Installation |
| JUCE (Standalone, später VST3 für Studio One) | Konzept in [docs/GUI.md](docs/GUI.md); nächster Schritt nach dem Browser-Editor |

## Browser-Editor starten

1. Daisy mit Mod-on-Daisy-Firmware an J7 anschließen, zum Beispiel `ToyPiano-v0.3.bin`
   oder `PhysicalModelingMod-v0.1.0.bin`.
2. **Studio One schließen** oder dort den Port „Daisy Seed External“ deaktivieren.
   Unter Windows kann nur eine Anwendung den Port öffnen.
3. `web/index.html` in **Chrome oder Edge** öffnen. Beide Dateien (`index.html` und
   `mod_protocol.js`) müssen im selben Ordner liegen.
   * Falls der Browser bei einer lokal geöffneten Datei keinen MIDI-Zugriff anbietet,
     über einen lokalen Server öffnen: `python -m http.server 8000` im Ordner `web/`,
     dann `http://localhost:8000`.
4. Die Frage nach dem **MIDI-Zugriff mit SysEx** erlauben.
5. Ein- und Ausgang „Daisy Seed External“ wählen (wird vorgeschlagen) und
   **Verbinden** klicken. Der Editor liest Instrument, Spalten und aktuellen Zustand
   vom Gerät.

Firefox und Safari werden nicht unterstützt.

## Bedienung

* **Raster**:
  * Zeilen sind die 8 Quellen, Spalten die Ziele des Instruments. Unter jedem
    Spaltennamen steht, was Tiefe 100 bedeutet.
  * Schraffierte Zellen sind nicht verfügbar; der Tooltip nennt den Grund.
* **Tiefe ändern**:
  * Zelle senkrecht ziehen (mit `Shift` fein) oder das Mausrad benutzen (±1, mit
    `Shift` ±10).
  * Oder den Regler bzw. das Zahlenfeld rechts.
  * Tastatur: Pfeiltasten wählen, `+`/`−` ±1, `Bild↑`/`Bild↓` ±10, `0` auf null,
    `Leertaste` ein/aus.
* **Doppelklick** schaltet eine Verbindung aus und wieder ein, die Tiefe bleibt
  erhalten (Anzeige in Klammern).
* **Grenze**: Höchstens 8 aktive Verbindungen. Eine weitere Zelle bekommt ihren Wert,
  bleibt aber ausgeschaltet; der Editor meldet das.
* **Quellen**:
  * LFO 1/2: Form (Sinus, Dreieck, Sägezahn ↑/↓, Rechteck, S&H).
  * Sync: Frei (dann Rate und Retrigger), MIDI-Clock (dann Teilung) oder Gate (dann
    Eingang und Zyklen pro Impuls).
  * Random: Modus Stufe oder geglättet, Sync wie bei den LFOs.
* **Live-Anzeige**: Die Balken unter den Quellen zeigen deren aktuellen Wert, die unter
  den Spalten den gesamten Offset.
* **Dateien**:
  * „Datei speichern …“ legt eine `.modpatch`-Datei an (JSON).
  * „Datei öffnen …“ überträgt das ganze Raster in einem Schritt und danach die
    Quellparameter.
  * Im Gerät gibt es noch keine Speicherplätze; nach dem Ausschalten ist die Matrix
    leer.
* **Kontrolle**: Jede Änderung beantwortet das Gerät mit der Prüfsumme seines Patches.
  Der Editor vergleicht sie mit seinem eigenen Stand. Bei einer Abweichung lädt er
  den Zustand neu vom Gerät.
* **Ohne Gerät** lässt sich offline bearbeiten und als Datei speichern. **Demo (offline)**
  (oder `index.html#demo`) zeigt das Raster mit den Toy-Piano-Spalten und einem
  Beispiel-Patch.

## Tests

```bash
node editors/mod-on-daisy/web/tests/protocol.test.cjs   # Codec gegen die C++-Referenz (auch in CI)
make -C modules/mod-on-daisy test-editor                  # Editor in Headless-Chromium gegen mod_sim
```

`mod_sim` (`modules/mod-on-daisy/tools/mod_sim.cpp`) ist der echte Controller- und
Engine-Code der Firmware auf dem PC, mit den Spalten von Toy Piano bzw. Physical
Modeling. Der End-to-End-Test bedient den Editor wie ein Mensch: Tastatur, Ziehen,
Mausrad, Doppelklick, Quellen, Grenze, Dateien. Nach jedem Schritt prüft er, dass
Editor und simuliertes Gerät dieselbe Patch-Prüfsumme haben.
