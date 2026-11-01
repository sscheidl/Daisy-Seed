# Diagnostic App 0.3.4–0.4.0: Control Panel in Kategorien, Review-Korrekturen, Audio

App 0.3.4 löst den Reviewkandidaten 0.3.3 ab und setzt die Host-/GUI-Befunde des
unabhängigen Meilenstein-Reviews ([Auftrag](CLAUDE_MILESTONE_REVIEW.md)) um; 0.3.5
ergänzt die Buchsenringe, 0.4.0 den Reiter **Audio** zur gleichnamigen
Firmware-Erweiterung.

## 0.4.0: Audio-Reiter, CV-Verlauf, POTDIM (01.11.2026)

Gehört zu Firmware 0.4.0 ([FIRMWARE.md](FIRMWARE.md), [PROTOCOL.md](../protocol/PROTOCOL.md)):

- **Audio-Reiter:** vier Spitzenwertanzeigen (IN L/R, OUT L/R) in dBFS mit
  Peak-Hold, Testtongenerator (Sinus, Frequenz, Pegel −40…−6 dBFS, Kanalwahl; die
  Firmware deckelt bei −6 dBFS), Eingangs-Schnappschuss mit 256–2048 Frames,
  Flankentrigger, Zeitbild beider Kanäle und Messwerten (Frequenz aus
  Nulldurchgängen, Spitzenpegel). Mit Kabel AUDIO OUT → AUDIO IN ist das der
  Audio-Loopback-Test. Ohne `audio` in den Fähigkeiten (Firmware < 0.4.0) bleiben
  die Bedienelemente gesperrt und der Grund steht im Reiter.
- **CV / Gate:** laufender Verlauf der letzten 10 s (CV IN 1/2, CV-OUT-1-Echo
  gestrichelt, Gates als Bänder) aus den Stream-Rahmen; Abtastung 50–100 Hz,
  kein Audio-Oszilloskop.
- **Buttons:** LED-Modus „VR1 dims" (POTDIM) als Handtest für Poti und LED-PWM.
- **Simulation:** Schalter „Patch cables AUDIO OUT → AUDIO IN" und „Noise on the
  audio inputs" für den Loopback ohne Hardware; der simulierte Eingang ist eine
  Annahme, kein Codec-Modell.
- **Sicherheit:** Der Ton braucht eine Sitzung, endet mit ihr, wird vor einem
  blockierenden SD-Test ausgeschaltet und verstummt im Audio-Callback von selbst,
  wenn die Hauptschleife ihn 1 s lang nicht mehr bestätigt. Pegel sind digitale
  Vollaussteuerung, keine Volt; Codec-, Buchsen- und Kopfhörerpegel sind
  **NICHT GEMESSEN**.
- Kein Spektrum (bewusst weggelassen).

Prüfung 0.4.0: Host-Tests C++ (MSVC und clang) und Python gegen den Simulator,
ARM-Build beider Varianten; keine Hardware. Das 0.4.0-Image muss der Mensch
flashen und abnehmen.

## 0.3.5: Buchsenringe für CV und Gate (01.11.2026)

Die CV-/Gate-Buchsen der Platinenansicht melden ihren Zustand wie LED-Ringe an
Eurorack-Modulen zurück: CV-IN-Ringe laufen farblich von Rot (−1) über Bernstein
(0) nach Grün (+1), CV-OUT-Ringe nutzen dieselbe Skala über den DAC-Code 0…4095
(kein Ring bei Off), Gate-Ringe leuchten grün bei High und blitzen bei jeder
gezählten steigenden Flanke kurz auf (150 ms), sodass auch kurze Trigger sichtbar
werden. Die Skala ist der normierte Wert, keine Spannung (Skalierung ungemessen).
Rendertest in `test_gui_design.py`; sonst nur Simulator, **NICHT GEMESSEN**.

## Oberfläche

Links bleibt die Platine als Live-Übersicht. Rechts ist das Bedienfeld in sechs
Kategorien geteilt; der JSON-Bericht ist aus jedem Reiter erreichbar:

| Reiter | Inhalt |
| --- | --- |
| **USB** | USB-Port, Firmware-/Protokollversion, Transportstatistik, Stream-Rate, Firmware-Updatemodus |
| **Buttons** | Tastendrücke S1–S16 (Zähler, gehaltene Taste hervorgehoben), LEDs D1–D16: Testmuster und Helligkeit |
| **Pots** | VR1–VR8 mit Rohwert und Min/Max, SW17/SW18, Klinkenerkennung, microSD-Erkennung |
| **CV / Gate** | CV IN 1/2 normalisiert, Rohwert, Min/Max; Gate-Zustand und Flanken; CV-OUT-Testsignal mit gemeldeten DAC-Codes |
| **SD card** | Lesetest, Kartenleser-Besitz (Share/Return) |
| **Log** | Ereignisprotokoll (bisher unter der Platine) |
| **Simulation** | nur im Simulationsmodus: simulierte Kabel, Karte, Klinken, Störungen |

Die Reiternamen sind bewusst kurz, damit alle sieben bei 1280 px Fensterbreite
ohne Blätterpfeile sichtbar bleiben; Tooltips nennen den vollen Inhalt.

Entfallen sind die „Planned features"-Liste, die LED-Kästchen, das Klicken auf
LEDs in der Platinenansicht und die „Demo animation" der Simulation.

## LEDs: Taster steuern LEDs

Der Firmware-Standardmodus **Buttons → LEDs** ist jetzt auch in der App der
Normalfall: Taste S*n* gedrückt → LED D*n* leuchtet, am Gerät wie in der
Simulation. Die früheren Einzel-LED-Kästchen (Modus MASK) und das Umschalten per
Klick auf eine LED entfallen, weil sie den Eindruck erweckten, die LED sei das
Bedienelement. Als Testmuster bleiben **All off**, **All on** und **Chase**
(mit Schrittzeit) sowie die Helligkeit; **Buttons → LEDs** stellt den Standard
wieder her. Die Anzeige zeigt weiterhin nur den kommandierten Zustand.

## Korrekturen aus dem Review

- **G1/H3 (P1):** Ein gespeicherter Bericht enthielt nach Kartenentnahme oder
  USB-Trennung weiterhin `sd=MOUNTED` sowie `device`/`port` der alten
  Verbindung. Die Session verwirft das Lesetest-Ergebnis jetzt bei Kartenentnahme
  und beim Verbindungsabbruch; `device` und `port` sind nur im verbundenen
  Zustand belegt.
- **G6b (P2):** CV-IN-Min/Max wurden nur im 100-ms-Anzeige-Timer aus dem jeweils
  letzten Rahmen gebildet (Aliasing, bei SAW 200 ms war Min = Max). Min/Max
  entstehen jetzt pro empfangenem Rahmen, wie bei den Potis.
- **H2 (P2):** `ERR c=SDTEST` (z. B. BUSY, wenn Windows die Karte besitzt) ließ
  die 20-s-Testsperre stehen. Die Ablehnung hebt die Sperre jetzt sofort auf.
- **G4/F3:** Ein Lesetest bei aktivem CV-OUT-Signal fragt nach und sendet zuerst
  `CVOUT m=OFF`, weil die Firmware während der Blockade den letzten DAC-Code
  hält. Das ist eine Host-Maßnahme; die Firmwaregrenze bleibt dokumentiert offen.
- **G2:** Tabellenhöhen folgen der Schriftmetrik statt fester 22 px.
- **G3:** Verschwindet der gewählte COM-Port, bleibt er als „not present" in der
  Auswahl sichtbar statt scheinbar auf „Automatic" zurückzufallen.
- **H6b:** Die Simulation liefert `cvr` wie die Hardware invertiert (raw ≈ 0 ↔ +1,0).
- Die Stream-Rate-Auswahl folgt der von der Firmware gemeldeten Rate.

Nicht Teil dieser Änderung: Firmware-Befunde (F1–F3b, Init-Blockade), H1
(Zombie-Session) und die Dokumentationsnachweise N1/N2/N4b/N5b.

## Prüfung

- `python -m pytest diagnostics/tests/python`: siehe PR-Beschreibung (Anzahl und
  Datum); neue Tests: LED-Standard und Testmuster, Min/Max über alle Rahmen,
  Bericht nach Kartenentnahme/Trennung, Lesetest mit aktivem CV OUT,
  Reiterstruktur, Session-Tests für Kartenentnahme, Trennung und `ERR c=SDTEST`.
- Gepackte EXE: `--sim --selftest 5`.
- Keine Hardware: Diese Änderung wurde nur gegen den Simulator geprüft. Die
  Bedienung mit echter Firmware 0.3.2 (Taster → LED, Lesetest, Kartenleser) ist
  **NICHT GEMESSEN** und bleibt eine Prüfung durch den Menschen.
