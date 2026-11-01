# Diagnostic App 0.3.4: Control Panel in Kategorien, Review-Korrekturen

Die Firmware bleibt bei 0.3.2; die DDP-Version ist unverändert. App 0.3.4 löst den
Reviewkandidaten 0.3.3 ab und setzt die Host-/GUI-Befunde des unabhängigen
Meilenstein-Reviews ([Auftrag](CLAUDE_MILESTONE_REVIEW.md)) um.

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
