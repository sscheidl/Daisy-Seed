# Mod on Daisy Editor — Konzept der Raster-GUI und Editor-Architektur

**Stand:** 23. Oktober 2026 · **Phase:** 1 (Konzept) · Umsetzung in Phase 3.
**Plattform:** JUCE, zunächst Standalone für Windows; VST3 für Studio One später (D6).
**Kommunikation:** USB-MIDI mit dem Protokoll aus
[modules/mod-on-daisy/docs/PROTOCOL.md](../../../modules/mod-on-daisy/docs/PROTOCOL.md).

> **Stand 23.10.2026:** Als Schnellvariante gibt es einen **Browser-Editor**
> ([`../web/`](../web/), Bedienung im [README](../README.md)). Er setzt das Raster, den
> Zelleneditor, die Quellenleiste, die Live-Anzeige und die Dateien dieses Konzepts um.
> Der JUCE-Editor folgt danach; VST3 kommt später.

## 1. Leitlinien

* **Das Raster ist die Oberfläche.** Alle 64 Kreuzungspunkte sind immer sichtbar, ohne
  Menüs und ohne Seitenwechsel, wie bei MatrixBrute/MiniFreak (D2).
* Was das Gerät nicht kann, ist **sichtbar ausgegraut** und nicht versteckt (D4).
* Das Gerät ist die Instanz der Wahrheit: Der Editor zeigt, was das Gerät mit CRC
  bestätigt hat.
* Der Editor enthält **keinen instrumentspezifischen Code**. Spaltenköpfe, Umfänge und
  gesperrte Zellen liest er vom Gerät.

## 2. Fensteraufbau

```text
┌────────────────────────────────────────────────────────────────────────────────┐
│ Mod on Daisy   Gerät: Daisy Seed External · ID 0 · physical-modeling 0.2.0  ● │  Statuszeile
├──────────────┬─────────────────────────────────────────────────────────────────┤
│              │ Pitch  Struct Bright Damp  Posit Excit Reverb Dry/Wet          │  Spaltenköpfe
│              │ ±12st  100%   100%   100%  100%  ⚡100% 100%  100%              │  vom Gerät
├──────────────┼─────────────────────────────────────────────────────────────────┤
│ LFO 1    ∿ ▮ │ [+35]  ·      ·      ·     ·     ·     ·      ·                │
│ LFO 2    △ ▮ │  ·     ·     [-20]   ·     ·     ·     ·      ·                │
│ Random   ⊓ ▮ │  ·     ·      ·      ·    [+50]  ·     ·      ·                │
│ CV 1       ▮ │  ·     ·      ·      ·     ·     ·     ·      ·                │
│ CV 2       ▮ │  ·     ·      ·      ·     ·     ·     ·      ·                │
│ Modwheel   ▮ │ ( 0)   ·      ·      ·     ·     ·    [+40]   ·                │  ( ) = aus, Wert behalten
│ Channel AT ▮ │  ·     ·     [+60]   ·     ·     ·     ·      ·                │
│ Poly AT  ◎ ▮ │  ·    [+25]   ·      ·     ·    ░░░    ·      ·                │  ░ = ausgegraut
├──────────────┴─────────────────────────────────────────────────────────────────┤
│ Auswahl: LFO 2 → Brightness   Tiefe [ -20 ]  ◄━━━━━━━●━━━━━━━━━► An/Aus [x]   │  Zelleneditor
│ Aktive Verbindungen: 5 / 8                                                     │
├────────────────────────────────────────────────────────────────────────────────┤
│ LFO 1: ∿ Sinus ▾ [Frei|Clock|Gate] Rate 2,40 Hz ☐Retrig │ LFO 2 … │ Random …   │  Quellenleiste
├────────────────────────────────────────────────────────────────────────────────┤
│ Patch „Pad Drift“  [Öffnen…] [Speichern…] [Vom Gerät laden] [Zum Gerät senden]   │  Dateien
│ Geräte-Slots 1–8 (ausgegraut bis Speicher freigegeben)                         │
└────────────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Raster

* **Zeilen** sind die 8 festen Quellen in der Reihenfolge D3, **Spalten** die 8
  Instrumentziele aus `GET_TARGET`. Unter dem Spaltennamen steht der Umfang
  (`span_text`) bei Tiefe 100, damit klar ist, wie viel „+35“ bedeutet (E5).
* **Zellenzustände**:

| Zustand | Darstellung |
| --- | --- |
| leer (Tiefe 0) | Punkt `·` |
| aktiv, positiv | gefülltes Feld, warme Farbe, Deckkraft ∝ \|Tiefe\|, Zahl |
| aktiv, negativ | gefülltes Feld, kalte Farbe, Deckkraft ∝ \|Tiefe\|, Zahl mit Vorzeichen |
| ausgeschaltet mit Tiefe | nur Umriss, Zahl in Klammern |
| nicht erlaubt (`GET_ALLOWED`) | schraffiert, nicht wählbar, Tooltip mit Grund: „Instrument hat kein Ziel“, „wirkt nur beim Anschlag“, „Quelle vom Instrument belegt“ |
| Grenze erreicht (8/8) | Leere Zellen bleiben wählbar, können aber nicht eingeschaltet werden; Hinweis „Höchstens 8 aktive Verbindungen“ |
| nicht bestätigt | kurzzeitig gestrichelter Rand, bis die Geräteantwort mit passender CRC eintrifft |

* **Spaltenmarken**:
  * `◎` am Poly-AT-Kreuzungspunkt eines `per_voice`-Ziels: „pro Stimme“.
  * `⚡` am Spaltenkopf: `latched`, „wirkt beim nächsten Anschlag“.
* **Live-Aktivität** (optional über `MONITOR`):
  * ein Balken `▮` am Zeilenkopf für den aktuellen Quellwert;
  * eine dünne Linie unter dem Spaltenkopf für den globalen Offset.
  * Damit sieht man sofort, ob das Keyboard überhaupt Poly AT sendet.

### 2.2 Bedienung

| Aktion | Wirkung |
| --- | --- |
| Klick auf Zelle | auswählen; der Zelleneditor zeigt Quelle → Ziel |
| Ziehen auf der Zelle senkrecht | Tiefe ändern (1 Schritt je 2 px; mit Shift fein, 1 Schritt je 8 px) |
| Mausrad | ±1, mit Shift ±10 |
| Doppelklick | ein/aus (Tiefe bleibt erhalten) |
| Rechtsklick | Menü: auf 0 setzen, invertieren, ein/aus |
| Tastatur | Pfeiltasten wählen, `+`/`−` ±1, Bild ↑/↓ ±10, `0` auf null, Leertaste ein/aus, Zahl eintippen + Enter |
| Zelleneditor | Schieberegler −100…+100 mit Rastung bei 0 und Zahlenfeld |
| Strg+Z / Strg+Y | Rückgängig/Wiederholen (nur im Editor; jeder Schritt erzeugt ein `SET_CELL`) |

Beim Ziehen sendet der Editor höchstens alle 20 ms ein `SET_CELL` mit dem jeweils
neuesten Wert (PROTOCOL § 4).

### 2.3 Quellenleiste

* **LFO 1/2**:
  * **Wellenform** als Auswahl mit Symbol: Sinus, Dreieck, Sägezahn steigend,
    Sägezahn fallend, Rechteck, Sample & Hold.
  * **Sync** als Umschalter mit drei Stellungen: *Frei*, *MIDI-Clock*, *Gate* (N4).
  * Bei *Frei* ein Drehregler **Rate** (0,02–20 Hz, logarithmisch, Anzeige in Hz) und
    der Schalter **Retrigger**.
  * Bei *MIDI-Clock* eine Auswahl **Teilung** (4 Takte … 1/32, mit Triolen).
  * Bei *Gate* die Auswahl **Gate 1/2** und eine **Teilung** (1/4, 1/2, 1/1, 2/1, 4/1
    Zyklen pro Impuls).
  * Der Editor zeigt nur die Bedienelemente, die zur gewählten Sync-Art passen.
* **Random**: Modus (Stufe/geglättet) sowie dieselben Sync-Möglichkeiten und Rate bzw.
  Teilung wie die LFOs.
* **CV 1/2, Modwheel, Channel AT, Poly AT**: keine Parameter in v0.1, nur die
  Live-Anzeige. Die CV-Kalibrierung liegt in einem gesonderten Dialog „Gerät“, weil
  sie zum Gerät gehört und nicht zum Patch.

### 2.4 Verbinden und Synchronisieren

1. Beim Start: MIDI-Ports auflisten und „Daisy Seed External“ vorschlagen.
2. `IDENTIFY` an alle Geräte senden; bei mehreren Antworten die Geräte-ID wählen lassen.
3. Ist der Schema-Hash unbekannt: Zielbeschreibungen holen und speichern, je Hash eine
   Datei. Dadurch funktioniert die Offline-Bearbeitung später auch ohne Gerät.
4. Zustand holen (`GET_MATRIX`, `GET_SOURCE` × 3, `GET_NAME`) und die CRC vergleichen.
5. Weichen Editor und Gerät ab, zum Beispiel weil offline bearbeitet wurde, fragt der
   Editor einmal: **„Vom Gerät laden“** oder **„Zum Gerät senden“**.
6. Verbindungsverlust: Die Statuszeile wird rot, die Bearbeitung geht offline weiter.
   Beim Wiederverbinden gilt Schritt 5.
7. **Portkonflikt** (E10): Kann der Port nicht geöffnet werden, erscheint ein klarer
   Hinweis, zum Beispiel: „Port ist von einer anderen Anwendung belegt (z. B. Studio
   One). Dort ‚Daisy Seed External‘ deaktivieren.“

### 2.5 Dateien

* **Format**: JSON (`.modpatch`). Darin stehen `format`, `instrument_key`,
  `schema_hash`, `name`, `matrix` (8 × 8 Tiefen), `enabled` (8 × 8 Bool), die
  Quellparameter und zur Lesbarkeit die Spaltennamen.
* **Laden**:
  * Passt der `instrument_key` nicht → Warnung und Laden verweigern.
  * Passt nur der Schema-Hash nicht → Spalten über die Namen zuordnen und das Ergebnis
    vor dem Senden anzeigen.
* **Senden** einer Datei: `SET_MATRIX` atomar, danach Quellparameter und Name.
* **Geräte-Slots** bleiben ausgegraut, bis die Firmware Speicherung meldet (IDENTIFY,
  Bit 0; STORAGE.md).

## 3. Editor-Architektur

```text
editors/mod-on-daisy/
├── CMakeLists.txt          JUCE per CMake (FetchContent oder Submodul, Version fest)
├── README.md
├── docs/GUI.md
└── src/
    ├── core/               ohne JUCE-GUI-Abhängigkeit, testbar
    │   ├── PatchModel      Raster, Quellparameter, Undo-Stapel; nutzt modules/…/patch.h
    │   ├── DeviceLink      MIDI-Ein/Aus, Anfragewarteschlange, Zeitüberschreitung, Wiederholung, CRC-Abgleich
    │   ├── Schema          Zielbeschreibungen, Cache je Schema-Hash
    │   └── PatchFile       JSON lesen/schreiben
    ├── ui/                 MatrixGrid, CellEditor, SourceBar, StatusBar, DeviceDialog
    └── app/                Standalone-Anwendung (später zusätzlich AudioProcessor für VST3)
```

* `core/` bindet `modules/mod-on-daisy/include/mod_on_daisy/protocol.h` und `patch.h`
  **direkt** ein. Kodierung, Validierung und CRC sind dadurch dieselben wie in der
  Firmware. Die Validierung läuft vor dem Senden zusätzlich im Editor, zum Beispiel
  „9. Verbindung nicht möglich“.
* **VST3 später**: Die Bedienoberfläche hängt nur von `core/` ab. Für das Plugin kommt
  ein `AudioProcessor` hinzu, der den Patch als Plugin-State im Studio-One-Projekt
  speichert und beim Laden zum Gerät sendet. Der Portzugriff im Plugin ist die
  wichtigste offene Frage (E10). Sie wird mit dem Ergebnis von H4 (TEST_PLAN) zu Beginn
  von Phase 3 entschieden.
* **Tests**: `core/` wird mit einem simulierten Gerät getestet, das denselben
  `Controller` aus `modules/mod-on-daisy` auf dem Host nutzt. Dazu Verlust, Verzögerung
  und Verfälschung von Rahmen.
* `OFFEN` vor Phase 3: Lizenzbedingungen von JUCE (AGPLv3 oder kommerziell mit
  kostenloser Einstiegsstufe) und die JUCE-Version.

## 4. Bewusst nicht in v0.1

Kurven pro Zelle, Via-Modulation, mehrere Seiten, Hüllkurven, Drag-and-Drop auf
Instrumentregler, Audio-Rate, MPE. Das Raster bleibt **die eine** Ansicht.
