# Mod on Daisy Editor

Grafischer Editor für die Modulationsmatrix [Mod on Daisy](../../modules/mod-on-daisy/).
Zentrales Bedienelement ist die vollständig sichtbare **8×8-Rastermatrix** nach dem
Vorbild von MatrixBrute/MiniFreak. Dazu kommen die Parameter der Quellen. Die
Verbindung zum Daisy läuft über USB-MIDI.

| Fassung | Stand |
| --- | --- |
| **JUCE-Editor** ([`src/`](src/)) | **nutzbar**: Standalone für Windows (`.exe` aus der CI), Linux und macOS per CMake. Simulator nur für Entwicklung und Selbsttest (`--simulators`) |
| **Browser-Editor** ([`web/`](web/)) | **nutzbar**: Schnellvariante für Chrome/Edge (Web MIDI), ohne Installation |
| **VST3-Plugin** ([`src/plugin/`](src/plugin/)) | **neu, am Gerät noch nicht geprüft**: derselbe Editor in Studio One, Patch im Song gespeichert ([unten](#vst3-plugin-für-studio-one)) |

Beide Fassungen lesen und schreiben dieselben `.modpatch`-Dateien.

## Acid Seq (SQ-Protokoll)

Seit Acid Seq Phase 5 bedienen alle drei Fassungen auch die Acid-Seq-Firmwares (`acid-seq`,
`step-seq`, `acid-303`, `drive-fx`, `delay-fx`) über das
[SQ-Protokoll](../../sequencers/step-seq/docs/PROTOCOL.md) (`F0 7D 53 51`).

* **Erkennung:** Beim Verbinden fragt der Editor zuerst SQ ab. Antwortet das Gerät, erscheinen
  die Acid-Seq-Seiten, und es wird keine Matrix gelesen. Ohne Antwort (rund 0,4 s) läuft die
  bisherige Mod-on-Daisy-Verbindung, Mod on Daisy v1 bleibt unverändert.
* **Das Gerät hat Vorrang:** Beim Verbinden wird nur gelesen (Komponenten, Parameter,
  Pattern, Zustand, Kalibrierung). Geschrieben wird nur bei einer Änderung oder mit
  „Auf Gerät übertragen“.
* **Seiten:**
  * Status mit Transportzustand (STOP/WARTET/LÄUFT/PAUSE), SW17, SW18, Clock-Quelle,
    Tempo und angewandter bzw. angenommener Revision.
  * Start/Stopp (wirkt nur mit SW17 auf Run).
  * Pattern-Raster mit 16 Steps, Notennamen und Lauflicht, dazu ein Step-Editor.
  * Parameterseiten Sequencer, Stimme, Drive, Delay und MIDI. Sie entstehen aus den
    Deskriptoren des Geräts, die Beschriftungen kommen aus dem Parametervertrag.
  * Kalibrierung beider CV-Ausgänge mit getrennter Freigabe. Die Freigabe wird nie
    gespeichert. Der Browser merkt sich Kalibrierdaten je Kit-UID.
  * Fehlerzähler.
* **Patch** (`.acidpatch`, JSON): Pattern und alle Parameter aller Komponenten, je mit
  Komponenten- und Schema-Version. Er lässt sich ohne Gerät laden (Browser: „Acid Seq
  offline“). Fehlende oder inkompatible Komponenten werden angezeigt und übersprungen.
  Die Kalibrierung gehört zum Kit und steht nicht im Patch. `.modpatch` bleibt
  unverändert lesbar.
* **VST3:** Bei einem Acid-Seq-Gerät ist der Songzustand der `.acidpatch`. Beim Laden eines
  Songs fragt das Plugin, ob der Patch übertragen werden soll: „Übertragen“, „Immer
  übertragen“ (für diese Plugin-Instanz) oder „Nicht übertragen“.
* **Simulator:** Mit `--simulators` gibt es den Port „Acid Seq (Sim)“, die
  SQ-Geräteseite der Firmware auf dem PC.
* **Korrekturen nach der Codex-Fehlersuche** (2026-10-27,
  [Bericht](../../docs/acid-seq/CODEX_FEHLERSUCHE.md)):
  * Browser: Die Timer laufen jetzt auch in Chromium (F16). Vorher scheiterte jede
    Verbindung zu einem SQ-Gerät.
  * JUCE: „Verbunden“ erst, wenn alle Deskriptoren, Werte und Labels gelesen sind (F05).
  * Beide Fassungen:
    * `.acidpatch` mit ungültigen Zahlen wird abgelehnt (F07).
    * Werte werden vor dem Senden auf den Bereich des Geräts begrenzt.
  * JUCE: Die Kalibrierung zeigt und sendet Bruchteile exakt (F13).
  * Browser: Die Kalibrierung zeigt nach dem Senden „gültig“.

## Streichwurst (SQ 1.1 + Mod on Daisy, ab Editor 0.3.0)

Alle drei Fassungen bedienen die [Streichwurst-Firmware](../../instruments/streichwurst/README.md)
über einen Port: Klangparameter über SQ 1.1, die Modulationsmatrix über Mod on Daisy
([SQ § 8](../../sequencers/step-seq/docs/PROTOCOL.md)).

* **Erkennung:** Meldet ein SQ-Gerät Fähigkeit Bit 6, startet der Editor nach dem Lesen
  des SQ-Zustands zusätzlich seine Mod-on-Daisy-Sitzung auf demselben Port. Beide lesen
  zuerst; geschrieben wird nur bei einer Änderung oder ausdrücklichem Übertragen. Die
  Statuszeile zeigt „streichwurst 0.1.0 · SQ + Matrix“. Acid-Seq-Geräte (SQ 1.0) und
  reine Mod-on-Daisy-Geräte verhalten sich wie bisher.
* **Seiten:** Strings, Solo, Tastatur, FX und Global, gebildet aus den Deskriptoren des
  Geräts und den Seiten des Parametervertrags; Statuszeile mit Produkt-, Instrument- und
  FX-Version sowie angewandter/angenommener Revision. „Reglerseite am Kit“ und „RAM-Preset“
  sind nur lesbar. Potibewegungen am Kit erscheinen über den Monitor in der App.
* **Matrix:** Der Knopf „Matrix“ (oben) zeigt die bekannte Rasteransicht mit den acht
  Streichwurst-Zielen, „Klangparameter“ führt zurück.
* **Patch** (`.swpatch`, JSON, Format `streichwurst-patch`): alle schreibbaren Parameter
  von Instrument, FX und Produkt mit Komponenten- und Schemaversion, dazu die Matrix als
  eingebettetes `.modpatch`-Objekt. Beim Übertragen werden fehlende oder inkompatible
  Teile übersprungen; die Matrix durchläuft die bekannte Spaltenprüfung. Im VST3 ist der
  `.swpatch` der Songzustand, mit derselben Rückfrage wie bei Acid Seq. `.modpatch` und
  `.acidpatch` bleiben unverändert.
* **Offline:** Browser „Streichwurst offline“ zeigt die Seiten aus dem Vertrag.
* **Standalone-FX** (`StreichwurstFx-v0.1.0`, SQ 1.0, eine Komponente): nur die Seite FX, keine
  Matrix.
* **Simulator:** Mit `--simulators` gibt es „Streichwurst (Sim)“: SQ-Gerät, Register und
  Mod-on-Daisy-Kern der Firmware auf dem PC (kein Audio-DSP im Editor).

## JUCE-Editor

### Windows-Programm holen

Die CI baut `ModOnDaisyEditor.exe` und das VST3-Plugin `Mod on Daisy.vst3`:

1. Auf GitHub unter **Actions → Editor** den letzten grünen Lauf öffnen. Der Lauf
   startet nur von Hand („Run workflow“, Branch wählen).
2. Unten bei **Artifacts** `ModOnDaisyEditor-windows-x64-…` herunterladen und entpacken.
3. `ModOnDaisyEditor.exe` starten. Eine Installation ist nicht nötig, die
   Visual-C++-Laufzeit ist eingebaut. Im selben Paket liegt der Ordner
   `Mod on Daisy.vst3` für Studio One ([unten](#vst3-plugin-für-studio-one)).
   `SHA256SUMS` nennt die Prüfsummen beider Dateien.

Windows SmartScreen kann beim ersten Start warnen, weil die Datei nicht signiert ist.
Dann „Weitere Informationen“ und „Trotzdem ausführen“ wählen.

### Bedienung

1. Daisy mit Mod-on-Daisy-Firmware an J7 anschließen, zum Beispiel `ToyPiano-v0.3.1.bin`,
   `PhysicalModelingMod-v0.1.1.bin` oder `LooperMod-v0.4.0.bin`.
2. **Studio One schließen** oder dort den Port „Daisy Seed External“ deaktivieren.
   Unter Windows kann nur eine Anwendung den Port öffnen. Ist er belegt, sagt der
   Editor das.
3. Oben das Gerät wählen (ein Daisy-Port wird vorgeschlagen, auch wenn das Kit erst nach
   dem Start des Editors erscheint) und **Verbinden** klicken. Der Editor liest Instrument,
   Spalten und den aktuellen Zustand vom Gerät.

**Firmware-Update …** (oben rechts) startet das Kit im Daisy-Bootloader neu, ohne die
Tasten BOOT und RESET am Seed3 – auch bei geschlossenem Gehäuse. Nach einer Rückfrage
sendet der Editor eine eigene SysEx-Nachricht an den gewählten Port
([common/bootloader](../../common/bootloader/README.md)); das geht mit jeder Firmware dieses
Repositorys (alle Instrumente und Effekte haben USB-MIDI an J7), auch ohne
Mod-on-Daisy-Verbindung, z. B. mit Nebulae, Looper oder Ambient Reverb. Das Kit
antwortet, sobald es die Anfrage angenommen hat; die Antwort ist noch kein Nachweis des
DFU-Eintritts. Danach leuchten alle 16 LEDs, und der Bootloader wartet ohne Zeitlimit
(der Looper beendet vorher laufende Kartenarbeit). Danach die
Firmware mit dem [Daisy Web Programmer](https://flash.daisy.audio/) oder `make program-dfu`
laden ([docs/FLASHING.md](../../docs/FLASHING.md)). Kommt nach 2 s keine Antwort, kennt
die Firmware den Befehl noch nicht: dann S9 und S16 fünf Sekunden halten oder RESET und
BOOT verwenden. Mit einem Simulator ist der Knopf gesperrt.

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

**Stand 24.10.2026: geht** (Laufzettel E7b, Studio One zuerst, dann Editor). Mit den
Windows MIDI Services öffnen beide Programme „Daisy Seed External“ gleichzeitig.

* Voraussetzung ist Physical Modeling ab `0.1.0-mod4`. Studio One schickt per
  Input-Monitoring alles, was vom Daisy kommt, an den Daisy zurück. Ältere Firmware hat
  diese zurückgeschickten Antworten mit NAKs beantwortet; die endlosen NAK-Schleifen
  verdrängten die Antworten an den Editor (Status rot). Seit mod4 ignoriert die Firmware
  Antwortrahmen (PROTOCOL.md § 2, Schritt 2a).
* **Toy Piano v0.3 hat diesen Schutz noch nicht.** Dort im Zweifel den Daisy-Eingang in
  Studio One abschalten oder Input-Monitoring auf der Spur aus.
* Noch nicht geprüft: umgekehrte Reihenfolge (erst Editor, dann Studio One).

Die früher spezifizierte „Durchleitung“ über einen virtuellen Port ist damit nicht mehr
nötig.

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

**Entschieden am 24.10.2026: Open Source.**

* **JUCE-Editor und VST3-Plugin** (`src/`, `tests/`, `CMakeLists.txt`): **AGPLv3**, Text in
  [COPYING.AGPL-3.0.txt](COPYING.AGPL-3.0.txt). JUCE 9 wird unter seiner AGPLv3-Option
  genutzt; die kommerzielle JUCE-Lizenz ist damit nicht nötig. Wer die `.exe` oder das
  Plugin weitergibt, muss den vollständigen Quellcode unter der AGPLv3 mitgeben oder
  zugänglich machen.
* **Browser-Editor** (`web/`): **MIT** wie der Rest des Repos ([LICENSE](../../LICENSE)).
* Eingebundene MIT-Teile (Mod-on-Daisy-Header, Spaltentabellen der Instrumente) bleiben
  MIT; MIT-Code darf in ein AGPL-Programm.
* Die VST3-Schnittstelle (in JUCE enthalten) steht seit 2025 unter **MIT** (Steinberg).
  Das VST-Logo wird nicht verwendet; dafür gälten Steinbergs eigene Regeln.

## VST3-Plugin für Studio One

Derselbe Editor als Plugin „Mod on Daisy“ (Hersteller „Daisy-Seed“, Kategorie
Fx/Tools). Es ist ein **Audio-Effekt, der den Ton unverändert durchlässt**; man legt ihn
auf eine beliebige Spur, z. B. die Daisy-Spur.

* **Verbindet sich selbst** mit „Daisy Seed External“, sobald das Plugin auf einer Spur
  liegt, auch ohne geöffnetes Fenster. Nach „Trennen“ bleibt es getrennt, bis man
  „Verbinden“ klickt. Steckt man das Kit neu ein, verbindet es wieder.
* **Fenster schließen trennt nicht**: Verbindung, Live-Anzeige und Modellanzeige laufen
  weiter, beim Öffnen ist alles wie zuvor.
* **Patch im Projekt**: Studio One speichert den Patch mit dem Song. Beim Laden des Songs
  zeigt das Plugin ihn sofort und **sendet ihn zum Gerät**, sobald es verbunden ist. Der
  Patch im Gerät wird dabei ersetzt. Spalten, die auf dem Gerät etwas anderes bedeuten,
  bleiben ausgeschaltet (wie „Ohne betroffene laden“).
* Den Port teilt sich das Plugin mit Studio One (Windows MIDI Services, siehe oben).
  Zwei Plugin-Instanzen im selben Song würden beide senden; nur eine verwenden.
* Keine Simulatoren im Plugin.

**Installieren** (Windows): Den Ordner `Mod on Daisy.vst3` (aus dem CI-Paket
`ModOnDaisyEditor-windows-x64-…` oder aus dem eigenen Build) nach
`C:\Program Files\Common Files\VST3\` kopieren (Administratorrechte), in Studio One
unter *Optionen → Orte → VST-Plug-Ins* neu scannen lassen.

**Bauen:**

```bash
cmake -S editors/mod-on-daisy -B build/editor
cmake --build build/editor --config Release --target ModOnDaisyPlugin_VST3
```

Das Plugin liegt unter `build/editor/ModOnDaisyPlugin_artefacts/Release/VST3/`. JUCE bringt
die VST3-Schnittstelle mit, ein eigenes VST3-SDK ist nicht nötig.

**Prüfen ohne Studio One** (`tests/plugin_check.cpp`): lädt das `.vst3` wie ein Host,
prüft Effekt mit Stereo-Ein-/Ausgang, unveränderten Ton, den Projekt-State im Hin und
Zurück zwischen zwei Instanzen und das Fenster. `MOD_ON_DAISY_NO_AUTOCONNECT=1` hält das
Plugin dabei von einem angeschlossenen Kit fern:

```bash
cmake --build build/editor --config Release --target ModOnDaisyPluginCheck
MOD_ON_DAISY_NO_AUTOCONNECT=1 build/editor/ModOnDaisyPluginCheck_artefacts/Release/ModOnDaisyPluginCheck "build/editor/ModOnDaisyPlugin_artefacts/Release/VST3/Mod on Daisy.vst3" editors/mod-on-daisy/web/tests/legacy-reverb.modpatch
```

Der Selbsttest des Standalone-Editors prüft zusätzlich den Weg „Patch aus dem Projekt,
offline geladen, beim Verbinden zum Gerät gesendet“ gegen den Simulator.

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
ohne Rechtsklickmenü; **Firmware-Update …** nutzt die gewählten Ein- und Ausgänge, auch
ohne Verbindung. **Demo (offline)** (oder `index.html#demo`) zeigt das Raster mit den
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
