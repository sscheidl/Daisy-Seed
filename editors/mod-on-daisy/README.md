# Mod on Daisy Editor

Grafischer Editor für die Modulationsmatrix [Mod on Daisy](../../modules/mod-on-daisy/).
Zentrales Bedienelement ist die vollständig sichtbare **8×8-Rastermatrix** nach dem
Vorbild von MatrixBrute/MiniFreak. Dazu kommen die Parameter der Quellen. Die
Verbindung zum Daisy läuft über USB-MIDI.

| Fassung | Stand |
| --- | --- |
| **JUCE-Editor** ([`src/`](src/)) | **nutzbar**: Standalone für Windows (`.exe` aus der CI), Linux und macOS per CMake. Simulator nur für Entwicklung und Selbsttest (`--simulators`) |
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
3. Oben das Gerät wählen (ein Daisy-Port wird vorgeschlagen, auch wenn das Kit erst nach
   dem Start des Editors erscheint) und **Verbinden** klicken. Der Editor liest Instrument,
   Spalten und den aktuellen Zustand vom Gerät.

**Ohne Daisy:** Die Liste enthält nur MIDI-Ports; ohne Gerät lässt sich offline bearbeiten.
Die Simulatoren („Simulator: Toy Piano“, „Simulator: Physical Modeling“) erscheinen nur mit
`"Mod on Daisy Editor" --simulators` und im Selbsttest. Sie sind der Controller- und
Engine-Code der Firmware, auf dem PC übersetzt, und melden sich wie das Gerät
(z. B. `physical-modeling 0.1.0-mod3`), haben aber keine Potis. Im Hardwaretest vom
24.10.2026 war deshalb nicht zu erkennen, dass der Simulator statt des Kits verbunden war;
seitdem stehen sie nicht mehr in der normalen Geräteliste.

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
  * **FM ← L2 / FM ← L1** (nur Geräte mit LFO-Quermodulation, z. B. Physical Modeling
    `0.1.0-mod3`): Der andere LFO verändert die Rate dieses LFOs, ±100 = ±2 Oktaven.
    Wirkt nur bei Sync = Frei; bei Clock oder Gate ist der Regler gesperrt, der Wert
    bleibt erhalten.
  * Bei Sync = MIDI-Clock oder Gate zeigt die Zeile **Takt**, ob der Takt ankommt
    („Kein MIDI-Clock – LFO steht“). Ohne Takt hält der LFO an.
* **Live-Anzeige** (20 Bilder/s):
  * **Quellen**: Mit erweitertem Monitor (Physical Modeling `0.1.0-mod3`) meldet das
    Gerät, welchen Bereich jede Quelle in den letzten 50 ms durchlaufen hat. Das helle
    Band ist dieser Bereich, der Strich der aktuelle Wert. Langsame Quellen zeigen ein
    schmales Band, schnelle das, was sie wirklich überstreichen – auch mit MIDI-Clock
    oder Gate. Ältere Firmware (Toy Piano v0.3) meldet nur den Momentanwert; dann
    zeigt ein volles Band einen freien LFO über 5 Hz.
  * Abgedunkelt: Die Quelle hat keine aktive Verbindung; die Firmware berechnet sie dann
    nicht.
  * **Modell** (oben neben dem Status): bei Physical Modeling ab `0.1.0-mod4` das Modell, das
    gerade klingt, z. B. „Modell: Western Chords“; „(MIDI)“, wenn es zuletzt per CC28 gewählt
    wurde. Der Tooltip nennt die Schalterstellung (Bank A = SW17 nicht gedrückt, B = gedrückt;
    SW18 oben/Mitte/unten) und alle Modelle.
  * **Spaltenköpfe**: Meldet das Instrument Grundwerte, steht unter dem Namen der **aktuelle
    Wert** der Poti- bzw. CC-Stellung (z. B. „63 %“, Pitch in Halbtönen um die Mitte, Level
    „+0 %“ = Verstärkung 1). Was Tiefe 100 bewirkt, steht im Tooltip und in der Zellenkarte.
  * **Ziele**: Der weiße Strich ist der **Grundwert** der Spalte vor der Modulation:
    die Poti- oder CC-Stellung, bei Physical Modeling „Level“ ein fester Wert 0,5
    (= Verstärkung 1, kein Poti; Poti 7 regelt weiter die Hallzeit). Das Band zeigt, wo
    die Modulation den **Sollwert** hinführt, der Strich in Akzentfarbe, wo er gerade
    steht. Rote Enden: Die Modulation stößt an 0 bzw. 1 an. Der Sollwert ist der Wert
    vor der Glättung im Instrument und nur der **globale Anteil**: Poly AT auf einer
    Spalte „pro Stimme“ wirkt zusätzlich je Stimme und ist nicht enthalten. Spalten mit
    „Anschlag“ übernehmen den Wert beim Notenanfang; klingende Noten ändern sich nicht
    mehr. Meldet ein Instrument keinen Grundwert, steht dort nur der Offset.
  * **Aftertouch** (oben links): eine gemeinsame Anzeige für die zuletzt empfangene Art,
    mit dem Zusatz „Kanal“ oder „Poly · Note“. Auch eine Nachricht mit Wert 0 zählt.
    Bei Poly AT zeigt sie die zuletzt gedrückte gehaltene Note; nach dem Loslassen 0.
    In der Matrix bleiben Channel AT und Poly AT getrennte Zeilen, ein Punkt markiert
    die angezeigte. Ältere Firmware meldet die Art nicht; dann steht dort „Kanal/Poly
    getrennt“ und es gelten die beiden Zeilen.
  * **Verbindungszustand** im Status oben: grün = alles kommt an; gelb „keine
    Live-Daten“ = seit 1,5 s keine Monitordaten, Antworten können noch gehen; rot
    „keine Antwort“ = seit 5 s nichts vom Gerät, Änderungen bleiben unbestätigt. Sobald
    das Gerät wieder antwortet, lädt der Editor dessen Zustand neu. Meist hält eine
    andere Anwendung den Eingang (siehe unten).
* **Dateien**:
  * „Datei speichern …“ legt eine `.modpatch`-Datei an (JSON).
  * „Datei öffnen …“ prüft die Datei zuerst und überträgt dann das ganze Raster in einem
    Schritt (auch Nullen und ausgeschaltete Zellen), die Quellparameter und den Namen.
    Lehnt das Gerät unterwegs etwas ab oder antwortet nicht, lädt der Editor den
    Zustand neu vom Gerät und meldet die Übertragung als unvollständig.
  * **Passt die Datei nicht genau** zum verbundenen Gerät, fragt der Editor vor dem
    Übertragen. Das ist der Fall bei einem anderen Instrument, bei einer Spalte, die in
    der Datei anders hieß (Beispiel: alte Datei mit „Reverb“, Gerät mit „Level“), bei
    Dateien ohne Spalteninformation und bei LFO-Quermodulation, die das Gerät nicht
    kennt. Die Liste nennt jede betroffene Spalte. Wahl:
    * **Ohne betroffene laden** (Vorgabe): Diese Verbindungen werden ausgeschaltet,
      ihre Tiefe bleibt erhalten; alles andere wird geladen.
    * **Trotzdem vollständig**: alles wie in der Datei.
    * **Abbrechen**: Das Gerät bleibt unverändert.
  * Offline geöffnete Dateien behalten beim Speichern Instrument, Schema und Spalten
    der Quelldatei, damit die Prüfung später noch möglich ist.
  * **Matrix leeren** setzt alle Tiefen auf 0 und schaltet alle Verbindungen aus. LFOs,
    Random und der Name bleiben. Tiefe 0 heißt: keine Modulation über diese Verbindung,
    nicht „Parameter auf 0“.
  * Im Gerät gibt es noch keine Speicherplätze; nach dem Ausschalten ist die Matrix
    leer.
* **Kontrolle**: Jede Änderung beantwortet das Gerät mit der Prüfsumme seines Patches.
  Der Editor vergleicht sie mit seinem eigenen Stand und lädt bei einer Abweichung neu.
  Ohne Antwort wiederholt er eine Anfrage dreimal.
* **Verbindungsverlust**: Wird der Daisy abgezogen (Ein- oder Ausgang verschwindet),
  meldet der Editor das und arbeitet offline weiter. Beim erneuten Verbinden gilt der
  Zustand des Geräts; offline Bearbeitetes wird dabei nicht übertragen. Bricht eine
  Verbindung ab (keine Antwort auf die Kennung, Senden verweigert), gibt der Editor den
  Port sofort frei, damit ihn ein anderes Programm oder ein neuer Versuch öffnen kann.

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

E7b in beiden Startreihenfolgen prüfen: erst Studio One, dann Editor – und umgekehrt.

Geht es nicht, bleibt vorerst die Umgehung: den Daisy-Port in Studio One deaktivieren,
solange der Editor läuft.

**Spezifikation für ein späteres Paket „Durchleitung“** (nicht umgesetzt; erst nach
einem negativen E7b-Ergebnis und eigener Freigabe):

* Der Editor öffnet den physischen Daisy-Port allein. Studio One nutzt ein virtuelles
  Portpaar (z. B. loopMIDI „Daisy via Editor“ hin und zurück).
* Studio One → Daisy: Noten, CC, Pitchbend, Channel AT, Poly AT, Programmwechsel und
  Echtzeit (Clock, Start, Stop, Continue) werden unverändert und ohne Pufferung
  weitergereicht. SysEx mit Kennung `0x7D 0x4D 0x44` von Studio One wird verworfen.
* Daisy → Studio One: alles außer Mod-on-Daisy-SysEx (Antworten und Monitorrahmen),
  die beim Editor bleiben.
* Keine Schleifen: Der Editor reicht nichts von einem Port auf denselben zurück; der
  virtuelle Eingang ist nicht zugleich Ausgang.
* Abnahme: Clock-Jitter und Latenz gegen direkten Anschluss messen, Notenverlust bei
  dichter Folge 0, Editor-Bedienung gleichzeitig, Trennen und Neuverbinden ohne
  hängende Noten.

Der heutige JUCE-Port verwirft alles außer SysEx und ist **keine** Durchleitung.

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
  mit 400 zufälligen Änderungen bei 5 % verlorenen und verfälschten Rahmen. Seit dem
  Korrekturauftrag vom 24.10.2026 außerdem: Kompatibilitätsprüfung (alte Reverb-Datei,
  gleiches Schema, Datei ohne Angaben, anderes Instrument, offline gespeicherte
  Herkunft, LFO-FM), Verbindungszustand (keine Live-Daten, keine Antwort, Wiederkehr),
  Nullsetzen, Leeren und Laden laut Auftrag § 7, erweiterter Monitor (Bereich, Takt,
  Aftertouch-Fälle), Zielbereich 0,1/0,5/0,9 ± 0,5 und LFO-FM.
* **Selbsttest** (`--selftest`): Er bedient das echte Fenster mit Tastatur, Ziehen,
  Mausrad, Regler, Doppelklick, Quellen, Grenze, Name und Dateien, dazu FM-Regler,
  Bereichsanzeige, Taktzustand, Verbindungszustand, Kompatibilitätsfrage (Abbrechen,
  ohne betroffene) und die Portfreigabe nach fehlgeschlagenem Verbinden. Nach jedem
  Schritt vergleicht er die Patch-Prüfsumme von Fenster und simuliertem Gerät. Ergebnis
  und Screenshots schreibt er nach `out/`; unter Windows steht das Ergebnis in
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
Mausrad, Doppelklick, Quellen, Grenze, Dateien, dazu Portfreigabe (belegter Eingang,
Zeitüberschreitung beim Verbinden, Trennen – mit gezählten `close()`-Aufrufen),
Kompatibilitätsdialog, Verbindungszustand, Bereichs- und Aftertouch-Anzeige, FM,
Nullsetzen, Leeren und Standardpatch. Nach jedem Schritt prüft er, dass Editor und
simuliertes Gerät dieselbe Patch-Prüfsumme haben.
