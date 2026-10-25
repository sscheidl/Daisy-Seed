# Gegenvorschlag: Anschlussblenden, Stützboden, Klemm-Sandwich

**Nachprüfung:** [Vergleich und Korrekturvorschläge vom 25.10.2026](DESIGN_REVIEW_2026-10-25.md).
Die folgenden Konzeptmaße sind damit nicht als bestätigte CAD-Passung zu lesen.

**Konzeptstand 25.10.2026, nach Review überarbeitet. Keine CAD-Ausgabe,
Hardware NICHT GEMESSEN.** Dieses Dokument bewertet [V3](UNIFIED_V3.md)
gegen die Projektziele und beschreibt eine alternative Konstruktion.
Der behobene Spiegelfehler ([ORIENTATION_FIX.md](ORIENTATION_FIX.md)) ist
nicht Teil der Bewertung. Kopfhörerbuchse und Lautstärkerad
([RIGHT_ACCESS.md](RIGHT_ACCESS.md)) werden nicht neu bewertet, müssen aber
in jeder kompakteren Variante erhalten bleiben.

## Ziele

- Bauteile der nackten Platine schützen, alle Bedienelemente und Buchsen
  zugänglich lassen, einschließlich BOOT/RESET auf dem Seed3.
- 6,35-mm-Audiobuchsen und beide USB-Anschlüsse besonders gut abstützen;
  sie tragen die höchsten mechanischen Lasten.
- Die vorhandenen Kappen (E-Switch 1SBLK und 1RLGR) auf den 16 TL1105-Tastern
  sicher fixieren.
- Papierschablonen zwischen zwei Acrylplatten, Wechsel mit Befaco-Knurlies;
  zusätzliche Klemmung über Kippschalter- und Eurorack-Muttern.

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
| A1 | Die NMJ6HCD2 ist laut [Neutrik](https://www.neutrik.com/product/nmj6hcd2) für Rückmontage mit Mutter an einer Frontplatte gebaut (Panelbohrung Ø 11,4, Platte < 4,7 mm). V3 erzeugt nur Freilöcher Ø 13,6 („no assumed ferrule clamping“). | Steckkraft bis 30 N, Ziehkraft > 10 N und Kabelhebel laufen über Buchsengehäuse, Lötstifte und PCB; das Gehäuse trägt nichts davon. Die genaue Verteilung ist nicht berechnet. |
| A2 | USB-C J7, Seed-USB und MIDI-DIN haben nur Freiausschnitte: keine Anlage an der Front, kein Anschlag dahinter, keine Stütze darunter. | Die am stärksten belasteten Anschlüsse hängen nur am PCB bzw. an den Seed-Buchsenleisten. |
| A3 | Das PCB liegt nur auf H1–H4 (Raster 187 × 101 mm). Unter Tastern und Potis gibt es keine Stütze. | Durchbiegung beim Drücken; ohne Rechenmodell nicht beziffert. |
| A4 | Im Knurlie-Klemmpaket liegen 0,8 mm Druckkunststoff; Gewindeeingriff nominal 3,05 mm. | Kriechen und Lockern; Gewindeverschleiß bei häufigem Wechsel. |
| A5 | Kippschaltermuttern klemmen auf unbekannte Schulterhöhe (`toggle_shoulder_above_pcb: null`). | Bei Spalt Zug auf die Lötstellen. |

### Zugänglichkeit

| # | Befund |
| --- | --- |
| B1 | microSD (Push-Push) sitzt etwa 9 mm hinter der Außenwand in einem 22 × 7-mm-Fenster; ohne Werkzeug nicht bedienbar. |
| B2 | Die Neutrik-Nase endet nominal bei PCB-Y −2,29 mm, also 6,71 mm hinter der Außenwand Y −9. Ein gerader Ø14-mm-Steckerkörper kollidiert im V3-STL; Winkelstecker sind nicht abgedeckt. |
| B3 | USB-C J7 liegt etwa 8,5 mm zurück. Ein 12 × 6-mm-Steckerkörper an der GCT-Referenzachse schneidet die V3-Wanne. |
| B4 | BOOT/RESET nur mit separatem Stift durch Ø-3-Bohrungen; Position aus dem FBX-Modell, vorläufig. |
| B5 | LEDs unter etwa 9 mm tiefen Ø-4,8-Öffnungen: sichtbar nur etwa ±15° um die Senkrechte (Punktmodell). |
| B6 | Potiknöpfe über Ø 14 mm, Patchkabel und Kippschalterstellungen können das Abheben der oberen Platte blockieren. |

### Maße und Ausrichtung

- Neutrik-Achse: V3 nimmt PCB + 8,9 mm an, die
  [Herstellerzeichnung](https://www.neutrik.com/media/8582/download/st-nmj6hcd2.PDF?v=3)
  nennt 8,14 mm (Abweichung 0,76 mm).
- Kappenhöhe (13 mm montiert), Seed-Höhe (9 mm), Kippschalter-Schulter und
  die Lage der DIN- und USB-Gehäuse sind Annahmen. J7: BOM nennt
  HYCW445-USBC16-730B, Footprint und STEP heißen USB4110-GF-A.
- Obere Fenster für Eurorack-Muttern Ø 9,2: Eine Sechskantmutter SW 8 hat
  9,24 mm Eckmaß und schneidet das obere Acryl. Mitgelieferte Muttern messen.
- Ø14-Potilöcher bei 15,279 mm Raster lassen nur **1,279 mm Acrylsteg**.
- Die hintere Haube endet 23 mm über PCB, obwohl die 6,35-mm-Öffnung nur bis
  etwa 15,7 mm reicht; die Höhe folgt aus der DIN-Haube. Gesamthöhe 39,8 mm.

### Sinnhaftigkeit

- Das Gehäuse hält rundum 9 mm Abstand zum PCB. Daraus folgen A1, A2 und
  B1–B3.
- Die Hauben sind hohle Abdeckungen und berühren die Bauteile nicht, die sie
  überdecken.
- Die Kappen werden nur seitlich geführt, nicht gegen Auszug gesichert. Die
  abnehmbare obere Platte **kann** sie nicht fixieren: Zwischen den
  Acrylplatten liegen nur 0,15 mm Papier, und die Sicherung muss auch bei
  abgenommener Klarplatte bestehen. Fixieren kann nur ein fest montiertes
  Teil unterhalb der unteren Platte.

## Gegenvorschlag

Die Punkte sind Konstruktionsprinzipien mit Startwerten, keine Passmaße.
Jeder Punkt braucht CAD, RH-Exportprüfung wie bei V3 und einen Funktionsdruck.

### 1. Kontur folgt den Anschluss-Bezugsebenen

Hintere und seitliche Kontur werden aus den Montage- und Steckebenen der
einzelnen Anschlüsse abgeleitet, nicht aus einem festen Rand um das PCB.
Diese Ebenen sind verschieden: PCB-Kante, Blendenanlage der Neutrik-Buchsen,
Vorderkante von USB- und SD-Gehäuse. Das Außenmaß wird erst festgelegt, wenn
Blenden, Muttern, Ecken und Montageweg konstruiert sind; 203 × 123 mm ist nur
ein Zielwert. Radmulde und Kopfhörer-Frontöffnung aus RIGHT_ACCESS bleiben
eigene Anforderungen und werden mit H4-Pfosten und MIDI-Steckern zusammen
geprüft.

### 2. Neutrik-Rückblende über dem PCB

Nach der Herstellerzeichnung liegt die Blendenanlage 16,7 mm vor Pin T, also
bei PCB-Y **3,50 mm** (J4) bzw. 3,535 mm (J1–J3), **innerhalb** der
PCB-Kontur. Die Nase ragt 5,79 mm darüber hinaus bis Y −2,29.

- Die Blende sitzt deshalb **über** dem PCB: Unterkante mit Abstand zur
  Bestückung, das PCB läuft darunter durch. Sie kann nicht in einer
  Bodennut stehen.
- Sie wird zur Vorderwand der hinteren Haube im Druckträger und ist von
  unten U-förmig geschlitzt (Breite Ø 11,4). So lässt sich der Träger
  senkrecht auf die verlöteten Buchsen setzen. Scheibe und Mutter decken den
  Schlitz ab.
- Dicke so wählen, dass Scheibe und Mutter auf der 5,79-mm-Nase voll greifen;
  Neutrik nennt < 4,7 mm. Am Bauteil prüfen.
- Die Mutter hat **SW 11** (Eckmaß etwa 12,7 mm). Eine Steckschlüsselnuss
  hat etwa Ø 16 mm; bei Achse PCB + 8,14 mm bleibt zur PCB-Ebene kaum Luft.
  Gabelschlüssel SW 11 oder dünnwandige Nuss vorsehen und den Werkzeugraum
  zwischen J3/J4 bzw. J1/J2 (22,5 mm Achsabstand) im CAD prüfen.
- Die Haube muss gegen Ziehen, Drücken, Verdrehen und Herausheben am Rahmen
  gesichert sein, etwa durch Verzahnung mit dem Wannenrand und eigene
  Schrauben von unten.

### 3. USB-Halter mit Anschlägen in beide Richtungen

Gilt getrennt für J7 und den USB-Anschluss am steckbaren Seed3 (V3-Zugang
bei PCB-Y 67,5–82,5, PCB + 5…14 mm; Lage am Modul messen).

- Vorderer Anschlag: Blendenausschnitt größer als das Steckergesicht, aber
  kleiner als das Schirmblech, sodass die Blende die Buchsenfront fängt
  (Ziehkraft).
- Hinterer Anschlag: Rippe des Trägers hinter dem Schirmblech (Steckkraft).
- Oben Niederhalter, unten Stütze. Kontakt nur an robusten Schirm- bzw.
  Gehäuseflächen, nie an Kontakten oder SMD-Bauteilen.
- Spaltmaße aus einer Toleranzkette festlegen: Spiel lässt die Buchse erst
  wandern, Nullspalt ohne Toleranzkette erzeugt Vorspannung.
- Seed-USB: Die Steckkraft muss in den Rahmen gehen, ohne das Modul aus den
  Buchsenleisten zu hebeln. Niederhalter auf dem Modul neben dem
  USB-Gehäuse.
- J7-Maße aus der Zeichnung der tatsächlich bestückten Buchse übernehmen,
  nicht aus der GCT-Referenz.

### 4. MIDI-DIN und microSD

DIN: Blende mit Ausschnitt nach Gehäusemaß, Sattel von oben, Stütze unten;
Zeichnung des 57PC5F noch beschaffen. microSD: bündige Blende mit
Fingermulde, sodass die Karte mit dem Finger gedrückt werden kann.

### 5. Wenige, einstellbare PCB-Stützen

- Isolierende Stützen aus der Bodenwanne, jede **einstellbar** (zum Beispiel
  Nylon-Gewindestift M3 in gedrucktem Dom) und auf Anlage ohne Vorspannung
  eingestellt. Alternativ fester Dom mit definiertem Spalt plus weicher
  Auflage, wobei die Auflagendicke in den Spalt eingerechnet wird.
- Nur so viele Stützen, wie die Lastpunkte verlangen; viele harte,
  unterschiedlich hohe Stützen verspannen das Board.
- Startpunkte, frei von allen 235 Pad-Geometrien auf B.Cu (Board ohne
  rückseitige Footprints), Platinenkoordinaten in mm (Ursprung hinten links,
  y zum Spieler):

| Bereich | Startpunkt | Vias unter Ø6-Fläche |
| --- | --- | --- |
| USB J7 | (10, 41) | 4 |
| 6,35 mm J3/J4 bzw. J1/J2 | (43,5, 11) / (144, 11) | 0 |
| MIDI | (178, 37) | 1 |
| Eurorack-Feld | (102, 23), (77,5, 23,5) | 2 / 0 |
| Poti-Reihe | (82, 51,5), (112,5, 51,5), (143, 51,5) | 0 |
| Tasterfeld | (87, 93), (137, 93) | 2 / 2 |
| Kippschalter | (39, 90) | 0 |

Via-Zahlen nach [support-location-review.json](orientation-audit/support-location-review.json).
Vias, Leiterbahnen, Lötstoppmaske und Lötüberstände sind keine Kollision im
Sinn eines Bauteils, aber auch kein bestätigter Auflagepunkt: Punkte mit Vias
verschieben oder Auflage am realen Board prüfen.

### 6. Kein Kunststoff im Klemmpaket

Obere Metallpfosten 9 mm (oder 8 mm plus 1 mm Metallscheibe) reichen bis
unter die untere Acrylplatte. Nominal steigt der Gewindeeingriff auf
`6 − 1 − 0,15 − 1 = 3,85 mm`; die 6 mm nutzbare Knurlie-Länge sind ein
Planungswert. Knurlie-Länge, Gewindeauslauf, Sacklochtiefe und die von unten
einschraubende Schraube gemeinsam prüfen.

Weil der Träger dort nur eine Freibohrung hat, braucht er eine eigene
Befestigung (Schrauben in die Wanne, zusätzlich zu Buchsen- und
Kippschaltermuttern). Die 1-mm-Acrylplatten müssen flächig aufliegen; keine
Anzugskraft über kleine Acrylkanten.

### 7. Hauben als tragende Sättel

Innenrippen liegen auf den Gehäusen von 6,35-mm-Buchsen, DIN und beiden USB.
Spaltmaß aus der Toleranzkette wie in Punkt 3. Zusammen mit den Stützen
ergibt das einen Kraftschluss von oben und unten. Jede Haube nur so hoch wie
das gemessene Bauteil plus Wand. Druckorientierung, entfernbare Stützen und
Werkzeugraum für Muttern und Schrauben mit konstruieren.

### 8. Haltehülsen für die vorhandenen Kappen

Die bestellten Kappen haben keinen Kragen: 1SBLK nominal 5 × 5 × 9 mm,
1RLGR Ø 5,08 mm am Fuß, Ø 4,82 mm oben, 9,05 mm hoch.

- Eine gedruckte **Haltehülse** umfasst den unteren Teil der Originalkappe
  und hält sie mit kleinen Rastnasen. Unten hat sie einen Flansch.
- Der Flansch sitzt in einer Tasche im Druckträger unter der **fest
  montierten** unteren Acrylplatte. Deren Öffnung ist größer als der
  Hülsenkörper und kleiner als der Flansch.
- Rund und quadratisch getrennt auslegen: Ein rundes Loch lässt eine
  quadratische 5-mm-Kappe (Diagonale 7,07 mm) nicht durch.
- Die Hülse endet unter der Papierebene. Papier und obere Platte brauchen
  dann nur Öffnungen für die Kappe selbst.
- In Ruhe weder ziehen noch drücken: Luft zum Anschlag, voller Hub
  (0,25 mm) plus Reserve nach unten, eigener Überlastanschlag. Montierte
  Höhe messen; 13 mm bleiben eine Annahme.
- Flansch gegen den LED-Lichtweg prüfen: LEDs sitzen 7 mm hinter der
  Tastermitte.
- Erst ein Funktionsstück mit einigen Tastern drucken. Alternative mit
  Kappen mit integriertem Flansch wäre eine bewusste Abweichung von der
  Bestellung.

### 9. BOOT/RESET mit gefangenen Betätigern

Zwei gefangene, geführte Stößel mit eigener Rückstellung oder austauschbare
Federzungen; je ein Gehäuseanschlag, Leerspiel ohne Vorspannung, ein Fuß,
der nur den Betätiger trifft. Die beiden Positionen liegen nur 6 mm
auseinander: BOOT halten und RESET drücken muss bei geschlossenem Gehäuse
mit einer Hand gehen. Seed-Einsteckhöhe und Tasterlage am Modul messen.
Rückfalllösung: kleine abnehmbare Serviceabdeckung.

### 10. LED-Lichtweg bis zur Oberfläche

Die LEDs liegen jeweils **7 mm hinter** ihrem Taster, vom Spieler aus
gesehen. Im Punktmodell verdeckt eine 13 mm hoch sitzende Kappe die LED auf
PCB-Höhe unterhalb etwa 70° Blickhöhe, und zwar auch am nackten Board. Eine
Öffnung, die nur oben aufgeweitet wird, hilft nicht: Ein Ø3-Loch bei
PCB + 6 mm begrenzt weiter auf etwa 14°.

- Bevorzugt: gefangener **Lichtleiter** (klares PETG oder Acryl) mit Luft
  über der LED, Oberkante unter der unteren Acrylplatte. Das Licht kommt so
  auf etwa 9 mm Höhe; die Kappe verdeckt es dann nur noch unterhalb etwa
  40° Blickhöhe statt 70°.
- Alternative: Öffnung über die ganze Höhe bis nahe an die LED aufweiten.
- Papierfenster und klare Platten mitprüfen. Alle Winkel sind
  Rechenbeispiele, keine optische Messung.

### 11. Potiöffnungen

Der 1,279-mm-Steg zwischen Ø14-Löchern ist für 1-mm-Acryl zu schwach.

- Empfohlen: Löcher auf etwa **Ø 11 mm** verkleinern (Steg etwa 4,3 mm).
  Knöpfe über Ø 11 werden beim Papierwechsel abgezogen oder es werden
  kleinere Knöpfe verwendet.
- Alternative: alle acht Potilöcher zu einem Langloch verbinden. Das
  schwächt die Platte stärker und kostet Beschriftungsfläche.
- Platz für Poti-Gehäuse und Wellenbund unter der unteren Platte am realen
  Poti prüfen.

### 12. Eurorack- und Kippschaltermuttern, Papierwechsel

Die Muttern klemmen weiter nur die untere Acrylplatte. Obere Fenster nach
der größten Mutter-/Scheibenhülle plus Fertigungs- und Abhebespiel;
benachbarte Fenster ggf. verbinden und Reststege prüfen. Eine gemeinsame
Bezugsebene für die sechs Buchsenschultern, beide Kippschalter und die
Metallpfosten herstellen; Distanzscheiben vor dem Anziehen einpassen.

Papierwechsel heißt: Knurlies lösen, Patchkabel ziehen, Knöpfe über der
Lochgröße abziehen, Kippschalter in eine Stellung bringen, die das Abheben
erlaubt. Für häufigeren Wechsel wären gemeinsame Fenster, Papierschlitze oder
ein seitlich einschiebbarer Einsatz zu untersuchen; das ist eine
Gestaltungsentscheidung.

## Vor CAD und Druck messen

- Montierte Kappenhöhe aller 16 Taster
- Schulterhöhe der Kippschalter und der Eurorack-Buchsen über PCB
- Seed-Modulhöhe, Lage von BOOT/RESET und Seed-USB
- Neutrik-Sitz am verlöteten Board: Achshöhe, Blendenanlage, Mutter und Scheibe
- Gehäusemaße von J7 (tatsächlich bestückte Buchse), DIN und microSD
- Mutterngrößen (Eurorack, Kippschalter)
- Poti-Gehäuse und Knopfdurchmesser
- Knurlie-Länge und Sacklochtiefe der Pfosten

## Änderungen nach dem Review

- Blendenlage: Neutrik-Anlage bei PCB-Y 3,5 über dem PCB statt bündig an der
  PCB-Kante; Achse 8,14 mm; Mutter SW 11 mit Werkzeugraum.
- Seed-USB ergänzt; USB-Halter mit vorderem und hinterem Anschlag.
- Kappen: Haltehülsen für die vorhandenen Kappen statt Kappen mit Kragen;
  runde und quadratische Durchbrüche getrennt.
- LED: ganzer Lichtweg bzw. Lichtleiter statt nur oben aufgeweitetem Konus;
  Verdeckung durch die eigene Kappe ergänzt.
- Potisteg 1,279 mm ergänzt, Ø 11 mm vorgeschlagen.
- Stützen: einstellbar, Vias ausgewiesen, Widerspruch EVA plus Nullspalt
  entfernt, Durchbiegungszahl gestrichen.
- Träger braucht eigene Befestigung, wenn Metall bis ans Acryl reicht.
- Rezesstiefen der Steckerkörper auf Nennwerte aus der Zeichnung korrigiert.
- Radmulde und Kopfhörer-Frontöffnung als Pflicht für kompakte Varianten.

Quellen: [Neutrik NMJ6HCD2](https://www.neutrik.com/product/nmj6hcd2),
[NMJ6HCD2-Zeichnung](https://www.neutrik.com/media/8582/download/st-nmj6hcd2.PDF?v=3),
[Canford NMJ-Serie](https://www.canford.co.uk/NEUTRIK-JACK-SOCKETS-NMJ-series-Panel-type),
[E-Switch 1SBLK](https://configured-product-images.s3.amazonaws.com/2D/specs/1SBLK.pdf),
[E-Switch 1RLGR](https://configured-product-images.s3.amazonaws.com/2D/specs/1RLGR.pdf),
[GCT USB4110 (Referenz)](https://gct.co/files/drawings/usb4110.pdf),
[Rev4-BOM](https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed-3-desktop/Seed3-DevKit-Desktop_Rev4-bom.csv).
