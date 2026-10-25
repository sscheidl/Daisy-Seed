# Gegenvorschlag: Anschlussblenden, Stützboden, Klemm-Sandwich

**Nachprüfung:** [Vergleich und Korrekturvorschläge vom 25.10.2026](DESIGN_REVIEW_2026-10-25.md).
Die folgenden Konzeptmaße sind damit nicht als bestätigte CAD-Passung zu lesen.

**Konzeptstand 25.10.2026. Keine CAD-Ausgabe, Hardware NICHT GEMESSEN.**
Dieses Dokument bewertet [V3](UNIFIED_V3.md) gegen die Projektziele und
beschreibt eine alternative Konstruktion. Ausgeklammert sind der bereits
behobene Spiegelfehler ([ORIENTATION_FIX.md](ORIENTATION_FIX.md)) sowie
Kopfhörerbuchse und Lautstärkerad ([RIGHT_ACCESS.md](RIGHT_ACCESS.md)).

## Ziele

- Bauteile der nackten Platine schützen, alle Bedienelemente und Buchsen
  zugänglich lassen, einschließlich BOOT/RESET auf dem Seed3.
- 6,35-mm-Audiobuchsen und USB besonders gut abstützen; sie tragen die
  höchsten mechanischen Lasten.
- Kappen auf den 16 TL1105-Tastern durch die Deckplatten sicher fixieren.
- Papierschablonen zwischen zwei Acrylplatten, schneller Wechsel mit
  Befaco-Knurlies; zusätzliche Klemmung über Kippschalter- und
  Eurorack-Muttern.

## Bewertung V3

Grundlage: `build_unified_case.py`, `build_closed_case.py`,
`closed-mechanics.json`, das gepinnte Rev4-KiCad-Board und Herstellerdaten.

**Gut gelöst:** Alle XY-Ausschnitte (H1–H4, S1–S16, D1–D16, VR1–VR8,
SW17/SW18, sechs Eurorack-Buchsen) treffen die KiCad-Positionen ohne
Abweichung. Die gestufte Deckfläche ist durch die Eurorack-Buchsen erzwungen
(Schulter 9 mm, Gewindeende 13,5 mm über PCB). Eurorack-Buchsen haben einen
guten Kraftpfad: Buchse → Mutter → untere Acrylplatte → Druckträger →
Wannenrand. Muttern nur auf der unteren Platte erlauben den Papierwechsel
mit vier Knurlies.

### Stabilität

| # | Befund | Folge |
| --- | --- | --- |
| A1 | Die NMJ6HCD2 sind laut [Neutrik](https://www.neutrik.com/product/nmj6hcd2) für Rückmontage mit Mutter an einer Frontplatte (< 4,7 mm) gebaut. V3 setzt die Wand 6,6 mm vor die Buchsenfront und bohrt nur Ø 13,6 frei („no assumed ferrule clamping“). | Steckkraft bis 30 N, Ziehkraft > 10 N und Kabelhebel gehen vollständig in die Lötstellen. |
| A2 | USB-C J7 und MIDI-DIN haben nur Freiausschnitte: keine Wand an der Front, kein Niederhalter, keine Stütze darunter. | Die am stärksten belasteten Anschlüsse hängen nur am PCB. |
| A3 | Das PCB liegt nur auf H1–H4 (Raster 187 × 101 mm). Unter Tastern und Potis gibt es keine Stütze. | Grobe Balkenabschätzung ≈ 0,2 mm/N in Feldmitte, nicht gemessen. |
| A4 | Im Knurlie-Klemmpaket liegen 0,8 mm Druckkunststoff; Gewindeeingriff 3,05 mm. | Kriechen und Lockern; Gewindeverschleiß bei häufigem Wechsel. |
| A5 | Kippschaltermuttern klemmen auf unbekannte Schulterhöhe (`toggle_shoulder_above_pcb: null`). | Bei Spalt Zug auf die Lötstellen. |

### Zugänglichkeit

| # | Befund |
| --- | --- |
| B1 | microSD (Push-Push) sitzt 9 mm hinter der Außenwand in einem 22 × 7-mm-Fenster; ohne Werkzeug nicht bedienbar. |
| B2 | 6,35-mm-Steckerkörper müssen 9 mm tief durch Ø 13,6; dickere Stecker und Winkelstecker stecken nicht voll. |
| B3 | USB-C liegt ≈ 8,5 mm zurück hinter 15,2 × 8 mm; große Steckergehäuse stoßen an. |
| B4 | BOOT/RESET nur mit separatem Stift durch Ø-3-Bohrungen; Position aus dem FBX-Modell, vorläufig. |
| B5 | LEDs unter ≈ 9 mm tiefen Ø-4,8-Röhren: sichtbar nur etwa ±16° um die Senkrechte. |
| B6 | Potiknöpfe über Ø 14 mm müssen bei jedem Papierwechsel ab. |

### Maße und Ausrichtung

- Kappenhöhe (13 mm), Seed-Höhe (9 mm), Kippschalter-Schulter und die
  Achshöhen der horizontalen Buchsen sind Annahmen.
- Obere Fenster für Eurorack-Muttern Ø 9,2: Eine Sechskantmutter SW 8 hat
  9,24 mm Eckmaß. Mutterndurchmesser vor dem Zuschnitt messen.
- Die hintere Haube endet 23 mm über PCB, obwohl die 6,35-mm-Öffnung nur bis
  ≈ 15,7 mm reicht; die Höhe folgt aus der DIN-Haube. Gesamthöhe 39,8 mm.

### Sinnhaftigkeit

- Das Gehäuse hält rundum 9 mm Abstand zum PCB. Daraus folgen A1, A2 und
  B1–B3.
- Die Hauben sind hohle Abdeckungen und berühren die Bauteile nicht, die sie
  überdecken.
- Die Kappen werden nicht gehalten; sie sitzen nur auf dem Betätiger. Die
  obere Platte **kann** sie nicht fixieren: Zwischen den Acrylplatten liegen
  nur 0,15 mm Papier, dort ist kein Platz für einen Kragen. Fixieren kann nur
  etwas unterhalb der unteren Platte.

## Gegenvorschlag

### 1. Kontur folgt den Anschlüssen

Hinten, links und rechts sitzt je eine **3-mm-Anschlussblende** (Druck oder
2-mm-Aluminium), die innen bündig an den Buchsenfronten liegt. Laut KiCad
liegen die Fronten von J1–J4, J7 und der microSD bei etwa 0 mm zur PCB-Kante.
Vorn bleibt Rand als Handauflage. Außenmaß etwa 203 × 123 mm statt
215 × 129 mm.

### 2. 6,35-mm-Buchsen verschraubt

Die mitgelieferten Neutrik-Muttern klemmen die Buchsen an die hintere Blende.
PCB und Blenden werden so zu einer Einheit, die in 3,2 mm breite Nuten der
Bodenwanne gesetzt wird. Steck-, Zieh- und Hebelkräfte gehen in die Wanne.

### 3. USB-C und DIN oben und unten gehalten

Blendenausschnitt = Buchsengehäuse + 0,2 mm je Seite. Ein Steg vom Deck liegt
mit 0,1–0,2 mm Luft auf dem Buchsenblech, darunter steht eine Stützsäule.
Die microSD bekommt eine bündige Blende mit Fingermulde.

### 4. Stützsäulen in der Bodenwanne

Ø 6 mm, mit 0,5-mm-EVA-Auflage oder auf Nullspalt. Die Punkte sind gegen
alle 235 Durchsteck-Pads des Rev4-Boards geprüft; auf der Unterseite gibt es
keine SMD-Bauteile. Platinenkoordinaten in mm (Ursprung hinten links,
y zum Spieler):

| Bereich | Freier Punkt |
| --- | --- |
| USB J7 | (10, 41) |
| 6,35 mm J3/J4 bzw. J1/J2 | (43,5, 11) / (144, 11) |
| MIDI | (178, 37) |
| Eurorack-Feld | (102, 23), (77,5, 23,5) |
| Poti-Reihe | (82, 51,5), (112,5, 51,5), (143, 51,5) |
| Tasterfeld | (87, 93), (137, 93) |
| Kippschalter | (39, 90) |

Vor Freigabe gegen Leiterbahnen, Testpunkte und die tatsächliche Bestückung
prüfen.

### 5. Kein Kunststoff im Klemmpaket

Obere Metallpfosten 9 mm (oder 8 mm plus 1 mm Metallscheibe) reichen bis
unter die untere Acrylplatte; der Druckträger hat dort nur eine Freibohrung.
Gewindeeingriff: `6 − 1 − 0,15 − 1 = 3,85 mm`.

### 6. Hauben als tragende Sättel

Innenrippen liegen 0,2 mm über den Gehäusen von 6,35-mm-Buchsen, DIN und USB.
Zusammen mit den Säulen ergibt das einen Kraftschluss von oben und unten.
Jede Haube nur so hoch wie das gemessene Bauteil plus 2 mm Wand.

### 7. Kappen mit Kragen unter der unteren Acrylplatte

- Kappen mit Kragen ≥ Ø 7,5 × 1 mm, gekauft oder selbst gedruckt.
- Der Kragen liegt in einer Tasche im Druckträger unter der unteren Platte;
  deren Loch 5,4 mm.
- In Ruhe 0,3 mm Luft zum Acryl, nach unten ≥ 0,5 mm frei (Schaltweg 0,25 mm).
- Papier und obere Platte 5,8 mm.
- Die Kappen bleiben auch beim Papierwechsel gefangen.

### 8. BOOT/RESET mit Biegezungen

Zwei angeformte Federzungen in der Seed-Haube, etwa 14 × 5 × 1,2 mm (PETG),
Stößel 0,5 mm über dem PTS815. Keine Vorspannung, kein loses Werkzeug.
Position vorher am realen Board messen.

### 9. LEDs mit konischen Öffnungen

Unten Ø 3, oben etwa Ø 8 mm, statt zylindrischer Röhre. Alternativ kurze
Lichtleiter aus klarem PETG oder Acryl.

### 10. Eurorack- und Kippschaltermuttern

Klemmen weiter die untere Acrylplatte. Obere Fenster = Mutter-Eckmaß
+ 1,5 mm. Distanzscheiben erst nach Messung der Schulterhöhe.

## Vor dem Druck messen

- Montierte Kappenhöhe aller 16 Taster
- Schulterhöhe der Kippschalter über PCB
- Seed-Modulhöhe und Lage von BOOT/RESET
- Neutrik-Frontdurchmesser und Achshöhe
- DIN- und USB-Gehäusemaße
- Mutterngrößen (Eurorack, Kippschalter)

Quellen: [Neutrik NMJ6HCD2](https://www.neutrik.com/product/nmj6hcd2),
[Canford NMJ-Serie](https://www.canford.co.uk/NEUTRIK-JACK-SOCKETS-NMJ-series-Panel-type).
