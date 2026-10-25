# Gegenvorschlag: Anschlussblenden, Stützboden, Klemm-Sandwich

**Nachprüfung:** [Vergleich und Korrekturvorschläge vom 25.10.2026](DESIGN_REVIEW_2026-10-25.md)
und [Prüfung dieses Konzepts](REVIEW_ALTERNATIVE_CONCEPT_2026-10-25.md); deren
Befunde sind eingearbeitet (Kennungen in Klammern, z. B. R06).
Die folgenden Konzeptmaße sind nicht als bestätigte CAD-Passung zu lesen.

**Konzeptstand 25.10.2026, nach Review überarbeitet. Keine CAD-Ausgabe,
Hardware NICHT GEMESSEN.** Dieses Dokument bewertet [V3](UNIFIED_V3.md)
gegen die Projektziele und beschreibt eine alternative Konstruktion.
Der behobene Spiegelfehler ([ORIENTATION_FIX.md](ORIENTATION_FIX.md)) ist
nicht Teil der Bewertung. Die Kopfhörerbuchse ([RIGHT_ACCESS.md](RIGHT_ACCESS.md))
wird nicht neu bewertet und muss in jeder kompakteren Variante erhalten
bleiben. Für das Lautstärkerad gilt ab Stand 25.10.2026 die Seitennische
aus Punkt 5.

## Ziele

- Bauteile der nackten Platine schützen, alle Bedienelemente und Buchsen
  zugänglich lassen, einschließlich BOOT/RESET auf dem Seed3.
- 6,35-mm-Audiobuchsen und beide USB-Anschlüsse besonders gut abstützen;
  sie werden voraussichtlich am stärksten belastet (Kräfte nicht gemessen).
- Die vorhandenen Kappen (E-Switch 1SBLK und 1RLGR) auf den 16 TL1105-Tastern
  sicher fixieren.
- Papierschablonen zwischen zwei Acrylplatten, Wechsel mit Befaco-Knurlies;
  zusätzliche Klemmung über Kippschalter- und Eurorack-Muttern.

## Bewertung V3

Grundlage: `build_unified_case.py`, `build_closed_case.py`,
`closed-mechanics.json`, das gepinnte Rev4-KiCad-Board und Herstellerdaten.

**Gut gelöst:** Die Mittelpunkte aller XY-Ausschnitte (H1–H4, S1–S16,
D1–D16, VR1–VR8, SW17/SW18, sechs Eurorack-Buchsen) stimmen digital mit
KiCad überein; Lochgrößen und Zugänglichkeit sind damit nicht geprüft.
Bei dieser Bedienhöhe ist die gestufte Deckfläche eine sinnvolle Lösung
(Buchsenschulter nominal 9 mm, Gewindeende 13,5 mm über PCB). Vorgesehener
Kraftpfad der Eurorack-Buchsen: Buchse → Mutter → untere Acrylplatte →
Druckträger → Wannenrand. Muttern nur auf der unteren Platte erlauben den
Papierwechsel über die Knurlies (mit den Einschränkungen aus Punkt 13).

### Stabilität

| # | Befund | Folge |
| --- | --- | --- |
| A1 | Die NMJ6HCD2 ist laut [Neutrik](https://www.neutrik.com/product/nmj6hcd2) für Rückmontage mit Mutter an einer Frontplatte gebaut (Panelbohrung Ø 11,4, Platte < 4,7 mm). V3 erzeugt nur Freilöcher Ø 13,6 („no assumed ferrule clamping“). | Steckkraft bis 30 N, Ziehkraft > 10 N (Katalogwerte) und Kabelhebel laufen über Buchsengehäuse, Lötstifte und PCB in Pfosten und Wanne; eine direkte Buchsenabstützung gibt es nicht. Die Verteilung ist nicht berechnet. |
| A2 | USB-C J7, Seed-USB und MIDI-DIN haben nur Freiausschnitte: keine Anlage an der Front, kein Anschlag dahinter, keine Stütze darunter. | Die am stärksten belasteten Anschlüsse hängen nur am PCB bzw. an den Seed-Buchsenleisten. |
| A3 | Das PCB liegt nur auf H1–H4 (Raster 187 × 101 mm). Unter Tastern und Potis gibt es keine Stütze. | Durchbiegung beim Drücken; ohne Rechenmodell nicht beziffert. |
| A4 | Im Knurlie-Klemmpaket liegen 0,8 mm Druckkunststoff; Gewindeeingriff nominal 3,05 mm. | Mögliches Kriechen und Lockern, Verschleiß bei häufigem Wechsel (nicht beobachtet). |
| A5 | Kippschaltermuttern klemmen auf unbekannte Schulterhöhe (`toggle_shoulder_above_pcb: null`). | Bei Spalt Zug auf die Lötstellen. |

### Zugänglichkeit

| # | Befund |
| --- | --- |
| B1 | microSD (Push-Push) sitzt etwa 9 mm hinter der Außenwand in einem 22 × 7-mm-Fenster; Fingerzugang schwierig, Werkzeugbedarf nicht erprobt. |
| B2 | Die Neutrik-Nase endet nominal bei PCB-Y −2,29 mm, also 6,71 mm hinter der Außenwand Y −9. Ein gerader Ø14-mm-Steckerkörper kollidiert im V3-STL; Winkelstecker sind nicht abgedeckt. |
| B3 | USB-C J7 liegt etwa 8,5 mm zurück. Ein 12 × 6-mm-Steckerkörper an der GCT-Referenzachse schneidet die V3-Wanne. |
| B4 | BOOT/RESET nur mit separatem Stift durch Ø-3-Bohrungen; Position aus dem FBX-Modell, vorläufig. |
| B5 | LEDs unter etwa 9 mm tiefen Ø-4,8-Öffnungen: sichtbar nur etwa ±15° zur Senkrechten (Punktmodell). |
| B6 | Potiknöpfe über Ø 14 mm, Patchkabel und Kippschalterstellungen können das Abheben der oberen Platte blockieren. |

### Maße und Ausrichtung

- Neutrik-Achse: V3 nimmt PCB + 8,9 mm an, die
  [Herstellerzeichnung](https://www.neutrik.com/media/8582/download/st-nmj6hcd2.PDF?v=3)
  nennt 8,14 mm (Abweichung 0,76 mm).
- Kappenhöhe (13 mm montiert), Seed-Höhe (9 mm), Kippschalter-Schulter und
  die Lage der DIN- und USB-Gehäuse sind Annahmen. J7: BOM nennt
  HYCW445-USBC16-730B, Footprint und STEP heißen USB4110-GF-A.
- Obere Fenster für Eurorack-Muttern Ø 9,2: Eine angenommene Sechskantmutter
  SW 8 hat 9,24 mm Eckmaß und schneidet das obere Acryl. Die mitgelieferten
  Muttern sind nicht identifiziert; messen.
- Ø14-Potilöcher bei 15,279 mm Raster lassen nur **1,279 mm Acrylsteg**.
- Das Dach der hinteren Haube liegt innen bei PCB + 23, außen bei PCB + 25,4
  (Gesamthöhe 39,8 mm). Die alte Ø13,6-Audioöffnung endet bei PCB + 15,7.
  Die gemeinsame Dachhöhe mit der DIN-Haube ist eine Generatorentscheidung,
  keine Zwangshöhe.

### Sinnhaftigkeit

- Das Gehäuse hält rundum 9 mm Abstand zum PCB. Daraus folgen A1, A2 und
  B1–B3.
- Die Hauben sind hohle Abdeckungen und berühren die Bauteile nicht, die sie
  überdecken.
- Die Kappen werden nur seitlich geführt, nicht gegen Auszug gesichert.
  Anforderung: Die Sicherung muss unabhängig von der abnehmbaren
  Klarplatte bestehen, also an einem fest montierten Teil hängen. Praktisch
  bietet sich der Raum unter der unteren Platte an, weil zwischen den
  Acrylplatten nur das Papier liegt (angenommen 0,15 mm).

## Gegenvorschlag

Die Punkte sind Konstruktionsprinzipien mit Startwerten, keine Passmaße.
Jeder Punkt braucht CAD, RH-Exportprüfung wie bei V3 und einen Funktionsdruck.

### 1. Kontur folgt den Anschluss-Bezugsebenen

Hintere und seitliche Kontur werden aus den Montage- und Steckebenen der
einzelnen Anschlüsse abgeleitet, nicht aus einem festen Rand um das PCB.
Diese Ebenen sind verschieden: PCB-Kante, Blendenanlage der Neutrik-Buchsen,
Vorderkante von USB- und SD-Gehäuse. Das Außenmaß wird erst festgelegt, wenn
Blenden, Muttern, Ecken und Montageweg konstruiert sind; 203 × 123 mm ist nur
ein Zielwert. Die Kopfhörer-Frontöffnung aus RIGHT_ACCESS und die Radnische
(Punkt 5) bleiben eigene Anforderungen und werden mit H4-Pfosten und
MIDI-Steckern zusammen geprüft.

### 2. Neutrik-Rückblende über dem PCB

Nach der Herstellerzeichnung liegt die Blendenanlage 16,7 mm vor Pin T, also
bei PCB-Y **3,50 mm** (J4) bzw. 3,535 mm (J1–J3), **innerhalb** der
PCB-Kontur. Die Nase ragt 5,79 mm darüber hinaus bis Y −2,29 (J4) bzw.
−2,255 (J1–J3). Achsen X = 39,915 / 62,400 / 140,400 / 162,800; die vier
Einbaulagen einzeln verwenden.

- Die Blende sitzt deshalb **über** dem PCB: Unterkante mit Abstand zur
  Bestückung, das PCB läuft darunter durch. Eine durchgehende Vollblende bis
  zum Boden geht nicht; eine Blende mit PCB-Durchlass oder außenliegenden
  Füßen in einer Bodennut wäre eine Alternative.
- Bevorzugt: Vorderwand der hinteren Haube im Druckträger. Startidee: von
  unten U-förmig geschlitzt (Breite Ø 11,4 plus Fertigungsspiel), damit der
  Träger senkrecht auf die verlöteten Buchsen gesetzt werden kann.
- Ein offener Schlitz ist nicht das Herstellerbohrbild (geschlossen Ø 11,4)
  und nicht freigegeben: Eine übliche Scheibe deckt den nach unten offenen
  Schlitz nicht vollständig ab, und gegen Abheben braucht es eine eigene
  Befestigung. Eindeutiger ist eine **verschraubte Schließleiste** unter
  den Buchsen oder ein austauschbares Blendenteil mit geschlossenen
  Ø-11,4-Bohrungen. U-Schlitz nur nach Last- und Montageprobe.
- Plattendicke: Neutrik nennt < 4,7 mm. Aus den 5,79 mm Nasenlänge allein
  lässt sich keine sichere maximale Dicke für vollen Mutterneingriff
  ableiten; 2 oder 3 mm sind Startwerte. Mit Musterblende, echter Scheibe
  und Mutter prüfen.
- Die Mutter hat **SW 11** (Eckmaß 12,70 mm). Rechenbeispiel Nuss Ø 16 mm:
  bei Achse PCB + 8,14 mm bleiben über der PCB-Fläche nominal 0,14 mm;
  außerhalb der PCB-Kante entfällt diese Grenze. Neben der Nachbarmutter
  (Achsabstand J4–J3 22,485 mm, J2–J1 22,400 mm) bleiben horizontal etwa
  8 mm. Gabelschlüssel SW 11 oder dünnwandige Nuss vorsehen; realen
  Werkzeugraum im CAD prüfen.
- Die Haube muss gegen Ziehen, Drücken, Verdrehen und Herausheben am Rahmen
  gesichert sein, etwa durch Verzahnung mit dem Wannenrand und eigene
  Schrauben von unten. Positionen, Werkzeugweg und Montagefolge festlegen;
  Scheibenreibung ersetzt keinen Abhebeanschlag.

### 3. USB-Halter mit Anschlägen in beide Richtungen

Gilt getrennt für J7 und den USB-Anschluss am steckbaren Seed3. V3-Zugang
bei PCB-Y 67,5–82,5, PCB + 5…14 mm; das Hersteller-FBX legt die
Seed-USB-Baugruppe bei Y 68,05–79,35 und etwa PCB + 8,4…12,3 (bei
angenommener Modulhöhe 9 mm). Lage am Modul messen.

- Vorderer Anschlag nur an vorgesehenen robusten Gehäusestufen oder Laschen
  der Buchse. Der USB-Steckbereich samt Steckerhülle bleibt frei; kein
  Kragen, der Einstecktiefe oder Verriegelung verkürzt. Ob die bestückte
  Buchse dafür geeignete Flächen hat, ist offen.
- Hinterer Anschlag: Rippe des Trägers hinter dem Schirmblech (Steckkraft).
- Oben Niederhalter, unten Stütze. Kontakt nur an robusten Schirm- bzw.
  Gehäuseflächen, nie an Kontakten oder SMD-Bauteilen.
- Spaltmaße aus einer Toleranzkette festlegen: Spiel lässt die Buchse erst
  wandern, Nullspalt ohne Toleranzkette erzeugt Vorspannung.
- Seed-USB: Die Steckkraft muss in den Rahmen gehen, ohne das Modul aus den
  Buchsenleisten zu hebeln. Niederhalter nur auf geeigneten Flächen, nicht
  auf Modul-SMDs oder ungestütztem Modul-PCB. Das Modul muss demontierbar
  bleiben.
- J7-Maße aus der Zeichnung der tatsächlich bestückten Buchse übernehmen,
  nicht aus der GCT-Referenz.

### 4. MIDI-DIN und microSD

DIN: Blende mit Ausschnitt nach Gehäusemaß, Sattel von oben, Stütze unten.
Maße aus dem [Switchcraft-Kundendokument 57PC5F](https://www.switchcraft.com/assets/1/24/57PC5F_CD.pdf?5023=),
nicht aus dem vereinfachten V3-Zylinder.

microSD: Liegt die Fassung zurückgesetzt wie in V3, ist der Fingerzugang
schwierig; eine Pinzette ist eine mögliche Werkzeugwahl (nicht erprobt). Das
ist als Kompromiss zulässig, wenn der Karteninhalt über J7 als
Windows-Laufwerk erreichbar ist. Ein physischer Kartenwechsel muss aber auch
mit alter oder defekter Firmware möglich bleiben.

- Der USB-Kartenleser ist implementiert und als Buildziel vorhanden
  (`DaisyDiagnostic-CardReader`, `diagnostics/firmware/src/msc_bot.cpp`,
  `diagnostics/docs/SD_CARD_READER.md` auf `feat/shared-sd-driver`): CDC plus
  MSC über J7, exklusive Freigabe an Windows. Normal: Rückgabe nach
  Auswerfen und bestätigtem Abschluss; bei Unterbrechung ein eigener
  Fehler-/Bestätigungsweg. App-Schließen ist kein Auswerfen. Ein
  Laufwerksbuchstabe hängt von Partition, Dateisystem und Windows ab.
- Stand: nur Software-Tests. Laufwerkserkennung am Seed3 ist NICHT GEMESSEN.
- `common/sd` ist laut `PLAN.md` ein Treiber nur zum Lesen. Der USB-Leser
  bleibt eine eigene Schicht; gemeinsam angeboten werden Kartenbesitz,
  Unmount und Remount. Schreibender USB-Blockzugriff gehört nicht in den
  Lesetreiber.
- Freigabe der Karte aus **jeder** App per Tastenfunktion oder Control-App
  (HTML, JUCE, VST3) ist ein neues Entwicklungsziel: USB-Klassen,
  Audio-Pause, Kartenbesitz und Wiederanlauf je App. PR #40 fordert nur den
  Bootloader an, keine SD-Freigabe.
- Solange das nicht gilt, bekommt die Fassung eine Öffnung mit Pinzettenzugang
  und Fase. Wenn die Seitenblende nahe genug an die Fassung kommt, reicht
  eine Fingermulde. Einstecken, Auswerfen und Greifen mit Karte prüfen;
  Werkzeug darf sich nicht an Fassung oder Bestückung abstützen.

### 5. Lautstärkerad: Seitennische, oben geschlossen

Das Rad (Alps RK10J12R0A0B, Ø 14 × 1 mm) wird von der Seite mit dem
Zeigefinger gedreht. Von oben muss es nicht sichtbar sein; der Bereich
darüber wird als Staubschutz geschlossen.

- Lage: Mitte (191,325, 91,6), Radrand bei Platinen-x 198,325, also
  1,325 mm über die PCB-Kante. Radscheibe PCB + 1,5…2,5 mm (Nennwerte).
- Bei der Wand x 206 läge der Radrand 7,675 mm tief. Ob ein Finger ihn unter
  einem geschlossenen Dach erreicht, hängt von Fingerform und Fenster ab.
- Nische, Startwerte: Außenfläche x ≈ 200 (1,675 mm vor dem Radrand),
  Fenster PCB − 3 bis PCB + 6,5. Unter die PCB-Ebene nur im Bereich x > 197;
  Rundungen, Dome und Fingerhülle dürfen nicht ungeprüft nach x < 197 reichen.
- In y **nicht mittig 26 mm**: Das ergäbe y 78,6–104,6 und reichte 0,2 mm in
  den Zugang des MIDI-Steckers J10 (bis y 78,8) und nahe an den H4-Dom
  (Tasche bis y 102,5). Startwert y 80–101, Kontur gegen J10-Stecker,
  MIDI-Haube (bis y 86), H4-Dom und J5-Frontöffnung (ab y 108) im CAD
  prüfen.
- Dach: Bei Fensteroberkante PCB + 8 blieben bis zur unteren Acrylplatte
  (PCB + 9) nur **1,0 mm** Dach. Deshalb Fensteroberkante auf etwa PCB + 6,5
  senken (Dach 2,5 mm) oder ein 1-mm-Dach bewusst auslegen. Acryl und Papier
  laufen darüber durch.
- Rechenbeispiel Fingerkuppe als Kugel R 7 mm, tangential am Radrand: am
  Fenster x 200 braucht sie PCB − 2,54 bis PCB + 6,54. Ein Fenster bis
  PCB + 6,5 ist damit nominal gerade zu knapp; Fenster leicht konisch
  aufweiten oder x der Nische näher an den Radrand legen. Zum Drehen braucht
  der Finger zusätzlich tangentialen Weg.
- Ob der 1,5-mm-Reststeg neben H4 entfällt, zeigt erst ein neuer Schnitt mit
  dem tatsächlichen Dom.
- Prüfkörper: Fingerkuppe in Bedienstellung am Radrand, nicht nur ein
  Zylinder von oben. Die 15 bestehenden ACCESS-Prüfungen gelten für die
  offene V3-Aussparung, nicht für diese Nische.

### 6. Wenige, einstellbare PCB-Stützen

- Isolierende Stützen aus der Bodenwanne, jede **einstellbar** (zum Beispiel
  Nylon-Gewindestift M3 in gedrucktem Dom) und auf Anlage ohne Vorspannung
  eingestellt. Alternativ fester Dom mit definiertem Spalt plus weicher
  Auflage, wobei die Auflagendicke in den Spalt eingerechnet wird.
  Einstellzugang und Sicherung gegen Losdrehen vorsehen; Nylonspitze und
  Ø6-Teller sind unterschiedliche Kontaktflächen.
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

Via-Zahlen nach [support-location-review.json](orientation-audit/support-location-review.json)
(volle Ø6-Scheiben). Vias und Leiterbahnen stehen nicht zwangsläufig vor,
sind aber kein bestätigter Auflagepunkt. **Lötüberstände** können dagegen
mechanisch kollidieren, auch mit isolierender Stütze. Punkte mit Vias
verschieben oder das Höhenprofil der PCB-Unterseite am realen Board prüfen.

### 7. Keine gedruckte Auflage im oberen Pfosten-Klemmpaket

Die oberen Metallpfosten reichen bis unter die untere Acrylplatte
(PCB + 9,0 mm). Der Druckträger hat dort nur eine Freibohrung. Im Kraftpfad
bleiben Acryl, Papier, die Knurlie-Kunststoffscheibe und unten der
1,2-mm-Druckboden unter dem Schraubenkopf. Die Pfostenoberseite ist nur eine
lokale Auflage: Die 1-mm-Acrylplatten brauchen zusätzlich flächige Auflage
auf dem Träger; keine Anzugskraft über kleine Acrylkanten.

Verschraubung je Ecke H1–H4 (4 Sätze), Nennmaße. Alle Abstandsbolzen werden
neu gekauft; Schrauben können aus dem preenFM2-Bestand kommen, falls
vorhanden:

| Pos. | Teil | Spezifikation | Herkunft |
| --- | --- | --- | --- |
| 1 | Bodenschraube | M3 × 6 Linsenkopf ISO 7380-1 (Kopf max. Ø 5,7 × 1,65, SW 2), von unten. Kopfsenkung Ø 6,4 × 1,8 mm als eigene Entwurfsvorgabe (wie bei der preenFM2-Wanne V4.1), Kopf etwa 0,15 mm unter der Bodenfläche. Durchgang Ø 3,4. Boden 3,0 mm, unter dem Kopf bleiben 1,2 mm. Eindringtiefe 4,8 mm im unteren Bolzen (geometrisch; Fasen und erste Gänge tragen weniger). **Nicht länger als 6 mm.** | Bestand oder neu |
| 2 | Unterer Abstandsbolzen | Sechskant M3, SW 5,5, **Innen/Innen, L = 10 mm, Gewinde durchgehend**, Messing vernickelt | neu kaufen |
| 3 | PCB | 1,6 mm, Bohrung Ø 3,5 | – |
| 4 | Oberer Abstandsbolzen | Sechskant M3, SW 5,5, **Außen/Innen, L = 8 mm, Zapfen 6 mm**, Innengewinde ≥ 5 mm tief, plus **2 × Scheibe M3 × 0,5 mm** (DIN 433, Ø 6, oder DIN 125-A, Ø 7; Außenmaß gegen Nachbarteile prüfen) zwischen PCB und Sechskant. Ergibt nominal 9,0 mm; bei 0,45–0,55 je Scheibe 8,9–9,1 vor weiteren Toleranzen. Wer einen 9-mm-Bolzen bekommt, lässt die Scheiben weg. | neu kaufen |
| 5 | Plattenpaket | Acryl 1,0 + Papier 0,15 + Acryl 1,0 = 2,15 mm | – |
| 6 | Knurlie | Befaco Knurlie M3 mit Kunststoffscheibe (Befaco nennt M3 × 7); Planungswert 6 mm nutzbar, Eingriff `6 − 2,15 = 3,85 mm`. Unter der Scheibe nachmessen. | neu kaufen |

Alle Teile sind Standardmaße: Sechskantbolzen M3 SW 5,5 in 8 und 10 mm,
Scheiben M3 × 0,5, Linsenkopf M3 × 6 und M3 × 12, Einschmelzmuttern M3.
Bei der Bestellung Zapfenlänge, tatsächliche Innengewindetiefe, Fasen,
SW 5,5 und Material anhand der Artikelnummer prüfen; sie folgen nicht aus
der Körperlänge.

Einkaufsliste (M3-Schrauben und Muttern sind vorhanden):

| Menge | Teil |
| --- | --- |
| 4 | Sechskant-Abstandsbolzen M3, SW 5,5, Innen/Innen, 10 mm, Gewinde durchgehend |
| 4 | Sechskant-Abstandsbolzen M3, SW 5,5, Außen/Innen, 8 mm, Zapfen 6 mm, Innengewinde ≥ 5 mm |
| 8 | Scheibe M3 × 0,5 mm (DIN 433 oder DIN 125-A) |
| 4 | Befaco Knurlie M3 |
| 4 | Einschmelzmutter M3 für die Trägerbefestigung (Länge, Außen- und Kernlochmaß nach Domkonstruktion) |
| 2 | Acrylplatte 1 mm klar, Laserzuschnitt nach SVG (je Kappenvariante ein Satz, falls getrennte Konturen) |
| je nach Ausführung | Nylon-Einstellstützen mit Sicherung, Kappenhülsen (gedruckt), Lichtleiter, Schließleiste/Blendenteil der Rückwand, Papier |

Nicht durch „M3-Muttern vorhanden“ abgedeckt: Neutrik-Muttern SW 11 mit
Scheiben, sechs Eurorack-Muttersätze, zwei Dailywell-Muttersätze 10-48 UNS
samt Distanzteilen. Diese liegen den Bauteilen bei; Inventur vor Montage.

Im unteren Bolzen sitzt der Zapfen 6 − 1,0 − 1,6 = 3,4 mm tief, die
Bodenschraube 4,8 mm; es bleiben `10 − 3,4 − 4,8 = 1,8 mm` Luft. Mit einem
9-mm-Bolzen ohne Scheiben sind es 4,4 mm Zapfen und 0,8 mm Luft (deutlich
weniger Toleranzreserve). Eine 8-mm-Bodenschraube würde die Lücke
überfahren. Der Knurlie ist nicht durch die Bodenschraube begrenzt, wohl
aber durch die Innengewindetiefe des oberen Bolzens: Bei 5 mm Tiefe und
3,85 mm Eingriff bleiben nominal 1,15 mm, ohne Fasen und Toleranzen.

Höhenänderung gegenüber V3: Boden 3,0 statt 2,8 mm, das PCB liegt 0,2 mm
höher. Alle vom PCB abhängigen absoluten Höhen steigen um 0,2 mm, nur die
Kopfsenkungen bleiben am Boden. Absolute Höhen ab Bodenunterseite (nominal):

| Ebene | V3 | Gegenentwurf |
| --- | ---: | ---: |
| PCB unten / oben | 12,8 / 14,4 | 13,0 / 14,6 |
| unteres Acryl unten, PCB + 9 | 23,4 | 23,6 |
| obere Klarplatte oben, PCB + 11,15 | 25,55 | 25,75 |
| hohe Haube außen, PCB + 25,4 | 39,8 | 40,0 |
| Neutrik-Achse, PCB + 8,14 | 22,54 (V3-Loch bei 23,3) | 22,74 |

Die 1,2-mm-Ringauflage unter dem Bodenschraubenkopf trägt die Klemmung der
Pfosten. Tragfähigkeit prüfen; bei Bedarf Boden lokal verstärken oder eine
größere Auflage vorsehen.

- Die Lücke liegt bewusst im unteren Bolzen: Dort wird selten geschraubt.
  Im oberen Bolzen wechseln die Knurlies, deren tatsächliche Länge noch
  offen ist.
- Die vorhandenen preenFM2-Bolzen (DI20 Innen/Innen 20 mm, DA11 Außen/Innen
  11 mm) passen in diese Stapelung nicht: Oberer Bolzen und Scheiben müssen
  zusammen 9 mm ergeben, weil sie die Plattenebene auf die nominal 9 mm hohe
  Schulter der Eurorack-Buchsen legen. Endgültig erst nach Schulterabgleich.
- Durchgehende Gewindestangen (DIN 976) statt Bolzen sind eine verworfene
  Bauentscheidung: Die Plattenebene müsste über Muttern eingestellt werden.
- Die PCB-Bohrung Ø 3,5 gibt bei M3 radial ±0,25 mm Spiel. Das ist Teil der
  XY-Toleranzkette; Blendenluft je Seite festlegen und Druck-, PCB- und
  Bauteiltoleranzen einbeziehen, oder die Blende richtet das PCB aus.
- Tatsächliche Knurlie-Länge und Gewindeauslauf messen.

Trägerbefestigung: 4 × **M3 × 12** Linsenkopf ISO 7380-1 (Bestand oder neu)
von oben durch die Trägerhaut in M3-Einschmelzmuttern in Domen der Wanne.
Die Haut wird dort lokal auf 3,6 mm verstärkt (Unterseite PCB + 5,4) und
bekommt die Kopfsenkung Ø 6,4 × 1,8 mm; unter dem Kopf bleiben 1,8 mm. Der
Kopf liegt unter der unteren Acrylplatte. Die Schraube ragt 10,2 mm unter
die Haut, ihre Spitze also bis **PCB − 4,8**: Die Dome müssen deshalb
**außerhalb der PCB-Kontur** liegen (Rand oder Ecken). Beim Zielaußenmaß um
203 mm ist dort wenig Platz; Dome früh im CAD platzieren. 10,2 mm sind
Schraubenfreiraum (plus Spiel), nicht Gewindeeingriff.

Muttern an den Bauteilen: Neutrik mitgeliefert (SW 11, mit Scheibe),
Eurorack-Buchsen M6 × 0,5, Kippschalter 10-48 UNS (Dailywell B1). Mutter- und
Scheibenmaße der Eurorack-Buchsen und Kippschalter messen.

### 8. Hauben als tragende Sättel

Innenrippen liegen über den Gehäusen von 6,35-mm-Buchsen, DIN und beiden
USB. Spaltmaß aus der Toleranzkette wie in Punkt 3; solange Spiel besteht,
gibt es noch keinen Kontakt. Kontaktzustand und Lastpfade getrennt
festlegen; ein Sattel von oben fängt nicht automatisch axiale, seitliche
und Drehlasten. Haubenhöhe = maximale Bauteilhöhe plus tolerierte Luft plus
Wand. Druckorientierung, entfernbare Stützen und Werkzeugraum für Muttern
und Schrauben mit konstruieren.

### 9. Haltehülsen für die vorhandenen Kappen

Die bestellten Kappen haben keinen Kragen: 1SBLK nominal 5 × 5 × 9 mm,
1RLGR Ø 5,08 mm am Fuß, Ø 4,82 mm oben, 9,05 mm hoch; Aufnahme für den
Betätiger 2,80 bzw. 2,84 mm.

- Eine gedruckte **Haltehülse** umfasst den unteren Teil der Originalkappe
  und hält sie mit kleinen Rastnasen. Unten hat sie einen Flansch. Die Kappe
  muss in der Hülse ebenso gefangen bleiben wie die Hülse im Träger;
  Rastkraft, Montage und Kippwinkel sind nachzuweisen.
- Der Flansch sitzt in einer Tasche im Druckträger unter der **fest
  montierten** unteren Acrylplatte. Deren Öffnung ist größer als der
  Hülsenkörper und kleiner als der Flansch.
- Durchbrüche passend zur Form: Für die 5 × 5-Kappe braucht ein rundes Loch
  mindestens die Diagonale 7,07 mm plus Spiel. Getrennte Formen sind eine
  Frage von Optik und Führung.
- Die Hülse endet unter der Papierebene. Papier und obere Platte brauchen
  dann nur Öffnungen für die Kappe selbst. Beim Hochziehen oder Kippen darf
  die Hülse die obere Platte nicht berühren.
- In Ruhe weder ziehen noch drücken: Luft zum Anschlag, Hub (0,25 mm
  **typisch** laut TL1105, kein garantierter Maximalweg) plus Reserve nach
  unten, eigener Überlastanschlag. Ruhelage, Hub, Rückkehr und Auszug in
  einem Schnitt bemaßen. Montierte Höhe messen; 13 mm bleiben eine Annahme.
- Flansch gegen LED-Lichtweg und Nachbarn prüfen: LEDs sitzen 7 mm hinter
  der Tastermitte. Rechenbeispiel: Flansch Ø 8 lässt zur Ø-4,8-LED-Öffnung
  nur 0,6 mm Steg (Ø 10 würde überdecken), zum Nachbarflansch 4,29 mm.
  Rastnasen, Taschenluft und Lichtleiter mitrechnen.
- Erst ein Funktionsstück mit einigen Tastern drucken. Alternative mit
  Kappen mit integriertem Flansch wäre eine bewusste Abweichung von der
  Bestellung.

Zwei Varianten, weil die Kappenwahl noch offen ist: **16 eckige** (1SBLK)
oder **16 runde** (1RLGR).

- Gemeinsam: Druckträger. Seine Tasche nimmt beide Flanschformen auf
  (gemeinsame Maximalhülle).
- Je Variante eigen: Hülsensatz, obere Acrylplatte und Papier-Schnittkontur.
- Eine **gemeinsame untere Platte** ist möglich: Ein runder Flansch wird von
  einer quadratischen Öffnung schon gehalten, wenn sein Durchmesser größer
  ist als die Seitenlänge (Beispiel: Quadrat 6,5, Flansch Ø 8, je Seite
  0,75 mm Übergriff). Tragende Flächen und Kippsicherung trotzdem
  dimensionieren. Getrennte untere Platten bleiben eine Gestaltungswahl.

### 10. BOOT/RESET: Softwareweg zuerst, Stößel voraussichtlich entbehrlich

**Entscheidung offen bis zum Test an Toy Piano.** Wenn der Flash-Modus per
Tastenkombination zuverlässig funktioniert, entfallen die Stößel. Dann
bleibt eine **kleine abnehmbare Serviceabdeckung** über BOOT/RESET als
Wiederherstellungsweg; Firmware ohne die neue Funktion, fehlender
Bootloader und ROM-DFU brauchen weiter Zugang zu den Tastern.

Softwareweg: [PR #40](https://github.com/sscheidl/Daisy-Seed/pull/40),
implementiert, offen, nicht gemergt, am Kit NICHT GEMESSEN. Auslöser:
S9 + S16 allein 5 s halten, SysEx aus Instrumenten/Effekten und den
Editoren, `BOOT`-Befehl der Diagnostics über CDC. Der Kartenleser blockiert
Bootanforderungen, solange Windows die Karte besitzt. Aufruf in libDaisy:
`daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT)`.
Nutzbar erst, wenn eine Firmware mit dieser Funktion installiert ist.

Bootfolgen ohne Softwareweg: Daisy-Bootloader auf J7 = RESET, dann BOOT
innerhalb des Fensters der installierten Bootloader-Variante (etwa 2 s).
ROM-DFU über den Seed-USB = BOOT halten, RESET drücken und lösen, BOOT
lösen. Beides gilt für Wiederherstellung und Variantenwechsel, nicht nur
für die Ersteinrichtung.

Falls doch Stößel nötig werden (Rückfallentwurf, Startwerte):

- Keine passende Katalogverlängerung gefunden; Eigenbau.
- Schaft Ø 3 mm in einer **größeren** Führung (etwa Ø 3,3, per Coupon
  prüfen); die V3-Bohrungen Ø 3 hätten kein Laufspiel. Bund Ø 5 sichert nur
  nach oben, nach unten braucht es einen Käfig.
- Spitze Ø 1,5 mm. Die Betätigerfläche ist laut FBX etwa 2,2 × 3,0 mm; bei
  perfekter Zentrierung bleiben 0,35 mm Reserve. Lage am Modul bestätigen.
- Länge `L = 4,0 + Dachdicke + Kopfüberstand` (aus Dach innen PCB + 16,
  Taster PCB + 11,7, 0,3 Luft). Bei Dach 2,4 mm ergibt das 6,4 mm plus
  Überstand; 8 mm passen nur bei 1,6 mm Überstand.
- Ein frei liegender Stößel fällt auf den Taster; Leerspiel entsteht nur mit
  eigener Rückstellung gegen einen Gehäuseanschlag. Entweder sehr leichter,
  dauernd aufliegender Stößel ohne Luftspalt oder Feder plus Anschlag.
  Reibung, Klemmen und Überdruckanschlag prüfen.
- Köpfe für RST und BOOT unterschiedlich geformt.

### 11. LED-Lichtweg bis zur Oberfläche

Die 16 LED-Durchbrüche sind im V3-Druckträger und im Papier vorhanden
(senkrecht durch die STLs geprüft, 16/16). Beide Acrylplatten sind dort
geschlossen; das ist gewollt, weil sie klar sind. In der Blender-Ansicht
„Solid“ ist das Acryl undurchsichtig und verdeckt die Löcher; mit X-Ray
(Alt+Z) oder ausgeblendeten Acryl-Objekten werden sie sichtbar.

Die LEDs liegen jeweils **7 mm hinter** ihrem Taster, vom Spieler aus
gesehen. Im Punktmodell (Blick aus Spielerrichtung in derselben Spalte,
LED-Punkt auf dem PCB, Kappe 5 mm breit, 13 mm hoch) verdeckt die Kappe die
LED unterhalb etwa 71° Blickhöhe über der Horizontalen, auch am nackten
Board. Eine Öffnung, die nur oben aufgeweitet wird, hilft nicht: Ein Ø3-Loch
bei PCB + 6 mm begrenzt den Kegel weiter auf etwa 14° zur Senkrechten.

- Bevorzugt: gefangener **Lichtleiter** (klares PETG oder Acryl) mit Luft
  über der LED, Oberkante unter der unteren Acrylplatte. Das Licht kommt so
  auf etwa 9 mm Höhe; die Kappe verdeckt es dann nur noch unterhalb etwa
  42° Blickhöhe. Form, Kopplung, Diffusion und Flanschfreiheit offen.
- Alternative: Öffnung über die ganze Höhe bis nahe an die LED aufweiten.
- Papierfenster und klare Platten mitprüfen. Alle Winkel sind
  Rechenbeispiele, keine optische Messung.

### 12. Potiöffnungen

Der 1,279-mm-Steg zwischen Ø14-Löchern ist schmal und empfindlich
(Festigkeit nicht geprüft).

- Startwert: Löcher auf etwa **Ø 11 mm** verkleinern (Steg 4,279 mm).
  Knöpfe brauchen Abhebespiel; Knöpfe ab etwa Ø 11 werden beim Papierwechsel
  abgezogen oder es werden kleinere Knöpfe verwendet.
- Alternative: alle acht Potilöcher zu einem Langloch verbinden. Das kostet
  voraussichtlich Steifigkeit und Beschriftungsfläche.
- Standard-Alpha RV09: Welle Ø 6, Bund etwa Ø 7; die Sonderausführung
  `-0085` ist nicht bestätigt. Hülle auf Höhe beider Acrylplatten,
  Wellenrundlauf und Knopfschürze am realen Poti prüfen.

### 13. Eurorack- und Kippschaltermuttern, Papierwechsel

Die Muttern klemmen weiter nur die untere Acrylplatte. Obere Fenster nach
der größten Mutter-/Scheibenhülle plus Fertigungs- und Abhebespiel;
benachbarte Fenster ggf. verbinden und Reststege prüfen. Eine gemeinsame
Bezugsebene für die sechs Buchsenschultern, beide Kippschalter und die
Metallpfosten herstellen; Distanzscheiben vor dem Anziehen einpassen. Beim
Lösen der Knurlies müssen Träger und untere Platte gegen Abheben gesichert
bleiben (Punkt 7, Trägerbefestigung).

Papierwechsel heißt: Knurlies lösen, Patchkabel ziehen, Knöpfe über der
Lochgröße abziehen, Kippschalter in eine Stellung bringen, die das Abheben
erlaubt. Auch das Papier selbst muss über alle Bedienelemente ausgefädelt
werden. Papierdicke, Wölbung und Registrierung mehrerer Vorlagen erproben. Für häufigeren Wechsel wären gemeinsame Fenster, Papierschlitze oder
ein seitlich einschiebbarer Einsatz zu untersuchen; das ist eine
Gestaltungsentscheidung.

### 14. Gravur (Wunsch: „Daisy Seed“ mit Blume; Rechte offen)

- Probeparameter: 0,6 mm tief, Strichbreite ≥ 0,8 mm, Schrifthöhe ≥ 5 mm.
  Schrift, Gegenformen und Slicer-Linienbreite mit einer Probe prüfen.
- Wannenboden auf der Druckplatte ist ein guter Kandidat, abhängig von
  Bettstruktur und erster Schicht. Gravur nicht an den Schraubenauflagen:
  Dort bleiben nur 1,2 mm Boden. Alternativ die Rückfläche der hinteren
  Haube über den Buchsen, nach Platz für Muttern und Werkzeug.
- Rechte: Laut [Hersteller-README](https://github.com/daisyaudio/Seed3-DevKit-Desktop/blob/987297fb7c9a8c58721ef40abf6d6eedd1fcda2a/README.md#trademarks)
  ist **Qu-Bit Electronix, Inc. (dba Daisy)** Rechteinhaber, DAISY ist eine
  Marke; die Hardwarelizenz gewährt keine Markenlizenz. Das Repository ist
  öffentlich. Weder Logo noch dekorativen Schriftzug „DAISY“ ohne Klärung
  veröffentlichen; für den eigenen Druck lokale Datei. Für das öffentliche
  Gehäuse neutrale Grafik und sachlichen Kompatibilitätshinweis (z. B.
  „für Seed3 Desktop Dev Kit“) verwenden. Keine Rechtsberatung.

## Vor CAD und Druck messen

- Montierte Kappenhöhe aller 16 Taster
- Schulterhöhe der Kippschalter und der Eurorack-Buchsen über PCB
- Seed-Modulhöhe, Lage von BOOT/RESET und Seed-USB
- Neutrik-Sitz am verlöteten Board: Achshöhe, Blendenanlage, Mutter und Scheibe
- Gehäusemaße von J7 (tatsächlich bestückte Buchse), DIN und microSD
- Mutterngrößen (Eurorack, Kippschalter)
- Poti-Gehäuse und Knopfdurchmesser
- Knurlie-Länge und Sacklochtiefe der Pfosten
- Lage und Höhe des Lautstärkerads zur PCB-Kante; Fingerhaltung und Drehweg
  an einer Dachprobe; gesteckte MIDI- und Kopfhörerstecker
- Lage der microSD-Kartenfront im eingerasteten Zustand, Auswerfhub, Greifraum
- Neutrik: vollständige Verschraubung an einer Musterplatte, eigenes
  SW-11-Werkzeug
- PTS815-Lage und Bedienfolge der Recovery-Taster, nur falls Stößel nötig
- Höhenprofil der PCB-Unterseite an den gewählten Stützpunkten (Lötspitzen)
- Tatsächliche Schraubenlängen unter Kopf, Scheiben-, Acryl- und Papierdicken

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

Ergänzt nach Durchsicht in Blender (25.10.2026):

- Lautstärkerad: Seitennische, oben geschlossen (Punkt 5).
- microSD: Pinzette als Kompromiss mit USB-Kartenleser über J7 (Punkt 4).
- Zwei Hülsenvarianten, eckig und rund (Punkt 9).
- BOOT/RESET-Stößel konkretisiert, Softwareweg zum Bootloader (Punkt 10).
- LED-Durchbrüche bestätigt, Blender-Darstellung erklärt (Punkt 11).
- Schraubenliste je Ecke und Trägerbefestigung (Punkt 7): Bolzen neu
  in Standardlängen (unten Innen/Innen 10 mm, oben Außen/Innen 8 mm plus
  2 × Scheibe 0,5 mm), M3 × 6 ISO 7380 in
  Kopfsenkung Ø 6,4 × 1,8 wie bei der preenFM2-Wanne, M3 × 12 für den Träger.
- Gravur mit Hinweis auf Logo-Rechte (Punkt 14).

Eingearbeitet aus [REVIEW_ALTERNATIVE_CONCEPT_2026-10-25.md](REVIEW_ALTERNATIVE_CONCEPT_2026-10-25.md):

- Radnische: Fenster nur bis PCB + 6,5 statt + 8 (sonst 1 mm Dach, R06);
  nicht mittig 26 mm wegen J10-Zugang und H4 (R04, R07, R09); Bereich unter
  PCB-Ebene nur bei x > 197 (R05); Fingerkuppen-Rechnung (R08).
- Trägerschrauben: Dome außerhalb der PCB-Kontur, weil die Spitze bis
  PCB − 4,8 reicht (G15).
- Kappen: gemeinsame untere Platte möglich, Ø-10-Begründung gestrichen (K06);
  Flanschbeispiel Ø 8 gegen LED (K07); Rundloch-Aussage präzisiert (K03).
- Seed-Taster: Stößel voraussichtlich entbehrlich nach Test der
  Tastenkombination an Toy Piano; Serviceabdeckung bleibt. Rückfallentwurf
  mit Laufspiel, Längenformel und Rückstellung korrigiert (B02–B05);
  PR-#40-Stand und voll qualifizierter libDaisy-Aufruf (B07–B09).
- microSD und Kartenleser: Buildziel statt fertiges Image (D09), Fehlerpfad
  beim Auswerfen (D04), Leser als eigene Schicht neben dem Lesetreiber
  (D05), universelle Freigabe ist neue Entwicklung (D06).
- Neutrik: Schlitz nicht freigegeben, Schließleiste als eindeutigere Lösung
  (N04, N05); Plattendicke nicht aus 5,79 ableitbar (N06); Achsabstände und
  Werkzeug-Rechenbeispiel präzisiert (N02, N08).
- Verschraubung: Knurlie durch Gewindetiefe begrenzt (G09); Toleranz der
  Scheiben (G07); Ringauflage im Boden (G05); Z-Tabelle mit 40,0 mm
  Gesamthöhe; Einkaufsliste um Bauteilmuttern und offene Teile ergänzt
  (G16).
- Logo-Rechte nach Hersteller-README korrigiert (V03, V04).
- Formulierungen präzisiert: Mittelpunkt- statt Kontur-Übereinstimmung
  (A03), gestufte Fläche als sinnvolle Wahl (A04), Lasten laufen über PCB in
  die Wanne (A07), Haubendach innen/außen (A17), Kappensicherung als
  Anforderung (A19), Blende in Nut mit PCB-Durchlass denkbar (N03),
  Lötspitzen können kollidieren (S03), Titel von Punkt 7 (G01),
  Gewindestangen als Bauentscheidung (G13).

Quellen: [Neutrik NMJ6HCD2](https://www.neutrik.com/product/nmj6hcd2),
[NMJ6HCD2-Zeichnung](https://www.neutrik.com/media/8582/download/st-nmj6hcd2.PDF?v=3),
[Canford NMJ-Serie](https://www.canford.co.uk/NEUTRIK-JACK-SOCKETS-NMJ-series-Panel-type),
[E-Switch 1SBLK](https://configured-product-images.s3.amazonaws.com/2D/specs/1SBLK.pdf),
[E-Switch 1RLGR](https://configured-product-images.s3.amazonaws.com/2D/specs/1RLGR.pdf),
[GCT USB4110 (Referenz)](https://gct.co/files/drawings/usb4110.pdf),
[Rev4-BOM](https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed-3-desktop/Seed3-DevKit-Desktop_Rev4-bom.csv).
