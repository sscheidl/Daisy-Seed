# Vergleich und Fehlerprüfung: V3 und Claudes Gegenentwurf

**25.10.2026. Hardware NICHT GEMESSEN.** Geprüft: V3 einschließlich
RH-/ACCESS-Korrektur (`6b75f7b`) und der Konzepttext `84a6dd8`.
Für den Gegenentwurf existiert noch kein zweites CAD-/STL-Modell.
Seine konstruktiven Ansätze können bewertet, seine Passung, Druckbarkeit
und Kollisionsfreiheit aber noch nicht bestätigt werden.

Das Ziel ist ein geschlossenes Schutzgehäuse mit wirksam abgestützten
Anschlüssen, allen erreichbaren Bedienelementen, gefangenen bestellten
Tasterkappen und schnell wechselbaren Papiervorlagen zwischen zwei
Acrylplatten. Keiner der beiden Stände erfüllt dieses Ziel bereits vollständig.

## Ergebnis des Vergleichs

| Anforderung | Aktuelle V3 | Gegenentwurf `84a6dd8` | Bewertung / Korrektur |
| --- | --- | --- | --- |
| Schutz der Bestückung | Zusammenhängender Träger mit Seed-/Buchsenhauben und abnehmbarer Wanne | Abstützende Hauben und näher anliegende Blenden vorgeschlagen | Durchgehenden Schutzträger beibehalten, lokale Anschlussblenden ergänzen |
| 6,35-mm-Audio abstützen | Freilöcher, keine Verschraubung der Buchsen am Gehäuse | Neutrik-Muttern an Rückblende | Gegenentwurf hier deutlich zweckmäßiger; richtige Montageebene und verriegelte Blende fehlen noch |
| Beide USB-Anschlüsse abstützen | Freiraum, keine direkte Gehäuseabstützung | Oben/unten gehalten; Seed-USB nicht separat ausgearbeitet | Formschlüssige, isolierte Halter für J7 und Seed-USB; Kräfte auch axial aufnehmen |
| Alle Anschlüsse erreichbar | Kopfhörer/Rad nachgebessert; weitere Steckerhüllen nicht abgesichert | Bündige Blenden und SD-Mulde | Einzelne Mating-Ebenen statt pauschalem PCB-Rand; echte Stecker und Greifwege prüfen |
| 16 Kappen sicher halten | Seitliche Führung, keine Auszugssicherung | Beweglicher Kragen unter unterer Acrylplatte | Prinzip richtig, bestellte Kappen besitzen diesen Kragen nicht |
| Zwei Seed-Mikrotaster | Provisorische Stiftbohrungen | PETG-Biegezungen vorgeschlagen | Gefangene, getrennt bedienbare Betätiger mit Rückstellung und Gehäuseanschlag ausarbeiten |
| Papierwechsel | Vier Knurlies; große Potiknöpfe und bestimmte Kabel können blockieren | Grundsätzlich gleiche Einschränkung | Fester Halte-/Trägerteil und abnehmbare Klarplatte; Wechselweg am vollständigen Aufbau prüfen |
| Metallpfosten / Knurlies | 0,8 mm Druckkunststoff im Klemmpaket; nominal 3,05 mm Eingriff | Metall bis an Acryl, nominal 3,85 mm | Metallauflage übernehmen; tatsächliche Schraubenlänge, Sacklochtiefe und Trägerfixierung ergänzen |
| Ausrichtung | RH-Korrektur erneut an tatsächlichen STLs bestanden | Nur 2D-Koordinatenbeschreibung, noch kein Export | Derselbe physische RH-Export und unabhängige Landmarkenprüfung erforderlich |
| Nachweisstand | CAD und begrenzte Prüfkörper vorhanden | Konzept und Nennmaße | Beide ohne Hardwarefreigabe |

## Fehler und offene Punkte mit Korrekturvorschlägen

### F1 - Hoch: Audioanschlüsse werden in V3 nicht direkt abgestützt

`port_cuts()` erzeugt Freilöcher; eine Montage der NMJ6HCD2-Muttern ist
nicht konstruiert. Die Kabelbinderschlitze erlauben zusätzliche Kabelentlastung,
ersetzen aber keine definierte Buchsenbefestigung. Kräfte laufen zunächst
über Buchsengehäuse, Anschluss-/Befestigungspunkte und PCB; ihre genaue
Verteilung ist nicht berechnet. Claudes Formulierung, alles gehe vollständig
in die Lötstellen, ist daher zu absolut.

**Korrektur:** Rückblende an der vorgesehenen Montageebene der Neutrik-Buchsen,
mit deren Muttern verschraubt. Die Herstellerzeichnung nennt Ø11,4 mm
Panelbohrung; die Produktseite nennt eine Plattendicke unter 4,7 mm.
3 mm Druck oder 2 mm Metall sind mögliche Ausgangswerte, keine
Festigkeitsfreigabe. Die Blende muss gegen Ziehen, Drücken, Verdrehen und
Herausheben in der Wanne gesichert werden. Eine 3,2-mm-Nut um eine
3-mm-Blende definiert allein noch keine sichere Halterung.

### F2 - Hoch: Neutrik-Achshöhe und Anschluss-Bezugsebenen sind falsch bzw. unvollständig

V3 verwendet für Audio `PCB_TOP + 8.9`. Die Neutrik-Zeichnung und die
Achse des zugehörigen Hersteller-STEP nennen nominal **8,14 mm über der
Montagefläche**. Abweichung: **0,76 mm**. Das großzügige Loch verdeckt
den Fehler teilweise: Die Ø11,4-mm-Hüllkontur bleibt frei, ein gerader
Ø14-mm-Steckerkörper kollidiert im tatsächlichen STL mit Wanne und Träger.
Die getestete Ø12-mm-Hülle bleibt frei, hat in dieser idealisierten
Kreisbetrachtung an der unteren Kante nur etwa 0,04 mm Reserve.

Auch Claudes Annahme, Buchsenfronten lägen pauschal bei der PCB-Kante,
ist für eine verschraubte Rückblende nicht ausreichend. Aus Pin T und
dem 16,7-mm-Montagemaß folgt für J4 nominal die Blenden-Bezugsebene
bei internem PCB-Y **3,50 mm**, für J1-J3 **3,535 mm**. Das vordere
Buchsenende, die Blendenanlage und die PCB-Kante sind verschiedene Ebenen.
Die 5,79-mm-Längsangabe ergibt für J4 ein nominelles vorderes Ende bei
Y -2,29 mm, also etwa 6,71 mm hinter der bisherigen Außenwand Y -9 mm.
Die im Konzept genannten pauschalen 9 mm sind somit keine exakte
Steckerkörper-Rezesstiefe.

**Korrektur:** Achsen und Anlageebenen aus der konkreten Herstellerzeichnung
übertragen, Sitz am verlöteten Board prüfen und Steckerhüllen an der
vollständig gesteckten Position testen. Die vorgeschlagenen 203 × 123 mm
bleiben ein Zielmaß, bis Blenden, Muttern, Ecken und Montageweg konstruiert sind.

### F3 - Hoch: USB-Abstützung des Gegenentwurfs nimmt axiale Kräfte noch nicht definiert auf

Obere Rippe und untere PCB-Stütze können Durchbiegung begrenzen. Sie
verhindern nicht automatisch die Verschiebung in Steck-/Ziehrichtung.
Mit 0,1-0,2 mm Luft entsteht zunächst **kein fester Kontakt**; das Bauteil
muss sich erst bewegen. Ein Nullspalt ohne Toleranzkette kann umgekehrt
unerwünschte Vorspannung erzeugen.

J7 hat zusätzlich eine Quellenabweichung: Die BOM nennt die Bestückungs-MPN
**HYCW445-USBC16-730B**, Footprint und Render-STEP heißen **USB4110-GF-A**.
Die GCT-Zeichnung ist deshalb Referenz, kein automatisch exakter Nachweis
der bestückten Buchse. Ein definierter 12 × 6-mm-Steckerkörper an der
GCT-Referenzachse (PCB +1,68 mm) schneidet die V3-Wanne um etwa 23,616 mm³.
Dies ist ein nachgewiesener Entwurfsziel-Konflikt, keine Messung des eigenen Kabels.

**Korrektur:** J7 und die USB-Buchse auf dem steckbaren Seed getrennt
behandeln. Halter an robusten Gehäuse-/Schirmflächen mit vorderem und
hinterem Anschlag auslegen; keine Last auf Kontakte, bewegliche Teile oder
zufällige SMD-Bauteile übertragen. Bei Seed-USB muss die Halterung die
Steckkraft in den Gehäuserahmen führen, ohne das Modul aus den Buchsenleisten
zu hebeln. Zugänge auf Steckerhülle und Greifraum auslegen.

### F4 - Hoch: Die bestellten Kappen sind in beiden Ständen nicht fertig gegen Auszug gesichert

V3: 5,8-mm-Quadratführung und 6,4-mm-Acrylöffnungen lassen die bestellten
1SBLK und 1RLGR nach oben durch. Das Raster ist eine Führung, keine
formschlüssige Rückhaltung.

Der Gegenentwurf verlangt einen Kragen Ø7,5 × 1 mm; die vorhandenen
E-Switch-Kappen besitzen ihn nicht. Laut Detailzeichnung hat 1RLGR
**Ø5,08 mm am Fuß, Ø4,82 mm oben und 9,05 mm Höhe**, nicht überall
exakt Ø5 × 9 mm. 1S ist laut Produktseite nominal 5 × 5 × 9 mm.
Die Toleranzen und der montierte Sitz sind zu berücksichtigen.
Eine runde Ø5,4-mm-Öffnung würde die quadratische 5-mm-Kappe außerdem
nicht durchlassen: deren Diagonale ist 7,07 mm. Runde und quadratische
Durchbrüche müssen ausdrücklich unterschieden werden.

**Korrektur:** Originalkappen als Vorgabe erhalten. Eine mögliche Lösung
sind bewegliche Haltehülsen mit unterem Flansch und kleinen Rastnasen,
die die Originalkappe in der Hülse halten. Der Flansch wird von einem
**fest montierten** Führungsteil gefangen. Das braucht neue, angepasste
Durchbrüche und einen Funktionsprototyp; ein nur aufgeklemmter Ring ist
noch kein nachgewiesener Formschluss. Alternativ wären andere Kappen mit
integriertem Flansch eine ausdrückliche Abweichung von der Bestellung.

Die Rückhaltung muss auch bei abgenommener oberer Klarplatte bestehen.
Deshalb kann ausschließlich die abnehmbare Beschriftungsabdeckung diese
Aufgabe nicht gleichzeitig erfüllen. Der feste Deckträger bzw. die untere
Deckplatte muss den Halter tragen. Der Halteanschlag darf den Taster in
Ruhe nicht ziehen oder drücken; voller Hub, Rückkehr und ein eigener
Überlastanschlag sind auszulegen. **13 mm montierte Höhe bleibt eine
Annahme.** Die Einzelteilhöhe darf nicht einfach zur Tasterhöhe addiert werden.

### F5 - Hoch: BOOT/RESET sind in keinem Stand als fertige Bedienmechanik nachgewiesen

Die V3-Stiftbohrungen liegen am vorläufig übertragenen Seed3-Modell.
Sie sind eine Service-Passstudie. Claudes PETG-Zungen 14 × 5 × 1,2 mm
mit 0,5 mm Leerspiel sind ebenfalls nur Nennwerte. Daraus folgen weder
zulässige Betätigungskraft noch Rückstellung, Lebensdauer oder ein
sicherer maximaler Druckweg.

**Korrektur:** Zwei gefangene, geführte Stößel mit eigener Rückstellung
oder geprüfte austauschbare Federzungen. Jeweils ein Gehäuseanschlag,
Leerspiel ohne Vorspannung und ein Fuß, der nur den Betätiger trifft.
Seed-Einsteckhöhe und Lage am konkreten Modul bestimmen. Auch die
Bedienfolge BOOT halten und RESET betätigen muss bei geschlossenem
Gehäuse möglich sein; die beiden Modellpositionen liegen nur 6 mm auseinander.
Eine abnehmbare kleine Serviceabdeckung ist eine praktikable Rückfalllösung.

### F6 - Hoch: Auflagenprüfung des Gegenentwurfs ist noch keine mechanische Freigabe

Die unabhängige KiCad-Prüfung bestätigt: Alle zwölf vorgeschlagenen
**vollständigen Ø6-mm-Scheiben** meiden die 235 auf B.Cu vorhandenen
Pad-Geometrien. Das Board hat keine rückseitigen Footprints.
235 rückseitige Pads sind jedoch nicht gleich 235 elektrische PTH-Pads;
die Menge enthält auch nichtplattierte Bohrungen.

Zusätzlich existieren 195 rückseitig relevante Vias. Fünf Auflagen
überdecken deren Kupferkonturen:

| Stützmitte im internen PCB-Raum | Via-Überdeckungen |
| --- | --- |
| USB (10; 41) | 4 |
| MIDI (178; 37) | 1 |
| CV (102; 23) | 2 |
| Tasterfeld (87; 93) | 2 |
| Tasterfeld (137; 93) | 2 |

Dies beweist **keine** physische Kollision mit einer hervorstehenden Lötstelle:
Vias können maskiert und bündig sein. Es verhindert aber die Aussage,
diese Flächen seien bereits vollständig als Auflage geeignet.
Leiterbahnen, Lötstoppmaske, Lötüberstände und Toleranzen fehlen weiter.

**Korrektur:** Auflagen als lokale, isolierende und justierbare Stützen
mit definiertem weichem Kontakt oder abgestimmtem Spiel. Ihre Höhe an
der realen PCB-Unterseite prüfen. 0,5-mm-EVA darf nicht stillschweigend
um 0,5 mm zusätzlich auf eine bereits auf Nullspalt konstruierte Säule
gelegt werden. Zu viele harte, unterschiedlich hohe Säulen verspannen
das Board. Die im Konzept genannte Durchbiegung 0,2 mm/N ist ohne
vollständiges Rechenmodell kein belastbarer Ergebniswert.

### F7 - Hoch: Kopfhörer und Rad dürfen beim Verkleinern nicht wieder verloren gehen

Der Gegenentwurf klammert diesen Bereich ausdrücklich aus. Damit ist
seine Zugänglichkeit dort nicht bewertet. Die ACCESS-V3 besteht erneut
alle 15 Überschneidungsprüfungen (Ø10-mm-Steckerkörper, Ø14-mm-Fingerhülle,
Radscheibe gegen fünf montierte Teile). Das ist ein begrenzter Geometrienachweis.

Am Rad verbleiben nur 0,2 mm nominelle Reserve zum Finger-Prüfkörper;
neben H4 bleiben lokal 1,5 mm Material. Die Radscheibe ist Ø14 × 1 mm,
nominale Gesamthöhe 2,5 mm; der blaue Ball im Detailbild ist nur eine
Fingerraum-Illustration. Der Kopfhöreranschluss zeigt **vorne rechts nach
vorn**, nicht nach rechts. Er besitzt keine Panelmutter.

Die neue Same-Sky-Zeichnung zeigt einen **5-mm-Gehäusekörper**; das
Render-/Konfigurationsmodell verwendet noch 6 mm. Achshöhe 2,5 mm und
die geprüfte Steckerrichtung werden dadurch nicht widerlegt, die
Bestückungsdarstellung ist aber an dieser Stelle nicht exakt.

**Korrektur:** Seitenmulde für Rad und Frontöffnung für Kopfhörer als
eigene Anforderungen in jede neue CAD-Variante übernehmen. Nicht aus
dem kleineren Außenmaß ableiten. Mit tatsächlichem Stecker, Fingern,
H4-Pfosten und MIDI-Steckern zugleich prüfen; lokale H4-Verstärkung nach
innen vorsehen, ohne Rad oder Leiterplatte zu treffen. J5 gegebenenfalls
über einen passenden Gehäusesattel abstützen. Größere oder gewinkelte
Kopfhörerstecker sind durch den bisherigen Ø10-mm-Test nicht abgedeckt.

### F8 - Mittel: Vier gelöste Knurlies garantieren noch keinen schnellen Papierwechsel

Geschlossene Potiöffnungen Ø14 mm können nicht über größere Knöpfe
gehoben werden. Geschlossene Buchsenöffnungen und Papierschablonen
können durch Patchkabel bzw. deren Steckergehäuse blockiert werden.
Bei Kippschaltern muss der Abhebeweg in allen Schaltstellungen frei sein.
Diese Einschränkungen gelten auch für den Gegenentwurf.

**Korrektur:** Nutzungsfall festlegen: Wechsel nach Abziehen von Knöpfen/
Patchkabeln ist möglich, aber nicht einfach nur ein Vier-Schrauben-Wechsel.
Für häufigen Wechsel sind breitere gemeinsame Öffnungen, von außen
zugängliche Schlitze im Papier oder ein seitlich einschiebbarer
Beschriftungseinsatz zu untersuchen. Schlitze kosten zusammenhängende
Beschriftungsfläche; dies ist eine Gestaltungsentscheidung.
Die untere Acrylplatte und Kappenhalter bleiben immer am Gehäuse.

### F9 - Mittel: Mutternfenster, Schultern und dünne Acrylstege benötigen Korrektur

V3 hat oben Ø9,2-mm-Fenster für einige Eurorack-Muttern. Eine angenommene
SW8-Sechskantmutter hat 9,2376 mm Eckmaß und schneidet das tatsächliche
obere Acryl-STL. Das beweist den Fehler für **diesen Muttertyp**, nicht,
dass die mitgelieferten Muttern zwingend SW8 sind. Scheiben und Werkzeug
können noch größer sein. Die unteren Kippschalterbohrungen dürfen nicht
mit M5-Muttern verwechselt werden; vorgesehen ist Dailywell 10-48 UNS-2A.

**Korrektur:** Fenster nach maximaler Mutter-/Scheibenhülle plus
Fertigungs- und Abhebespiel. Benachbarte Fenster ggf. verbinden und
deren Reststege prüfen. Gleiche Bezugsebene der sechs Buchsenschultern,
beiden Kippschalter und Metallpfosten herstellen; erforderliche Distanzteile
vor dem Anziehen einpassen. Gegenmuttern dürfen nicht das PCB in eine
unpassende Plattenebene ziehen. Eine zusätzliche Gegenmutter kostet
Gewindelänge und Bauhöhe; ihr Platz ist bei den kurzen Buchsenhülsen
gesondert nachzuweisen. Ø14-mm-Potilöcher lassen bei 15,279 mm
Raster nur 1,279 mm Acrylsteg; die 1-mm-Platten brauchen eine sichere
Auflage und sorgfältiges Handling. Aus dem Material allein folgt keine Steifigkeit.

### F10 - Mittel: Die Metallauflage des Gegenentwurfs ist sinnvoll, ihre Befestigung noch offen

Der nominale Eingriff verbessert sich rechnerisch von 3,05 auf 3,85 mm,
wenn 0,8 mm Kunststoff aus dem Klemmpaket entfallen. Befaco nennt M3×7 mm
mit bereits vorhandener Kunststoffscheibe. Die hier angesetzten 6 mm
nutzbare Länge sind ein konservativer Planungswert, keine Messung der
vorhandenen Schrauben. Auch 3,85 mm sind daher keine bestätigte Passung.

**Korrektur:** Metallauflage bis unter die untere Acrylplatte übernehmen.
Dabei eine separate Sicherung des Druckträgers und eine durchgängige
Abstützung des flexiblen Acryls vorsehen; eine Freibohrung allein fixiert
den Träger nicht. Tatsächliche Knurlie-Länge, Gewindeauslauf, Sackloch,
Scheibenpaket sowie die von unten einschraubende Gegenschraube gemeinsam
prüfen. Anzugskräfte nicht über kleine Acrylkanten oder Papierdicke festlegen.

### F11 - Mittel: Ein LED-Konus löst die Sichtbarkeit nicht automatisch

Claudes Vorschlag unten Ø3, oben Ø8 kann sinnvoll sein. Bleibt die
untere Öffnung aber auf der bisherigen Rasterunterseite PCB +6 mm,
begrenzt sie den Blickwinkel weiter. Ein vereinfachtes Punktmodell mit
LED in der PCB-Ebene ergibt etwa 14,9° für das jetzige obere Ø4,8-Loch
bei +9 mm; ein Ø3-Loch bei +6 mm begrenzt bereits auf 14,0°, auch wenn
es oben zu Ø8 aufweitet. Dies sind **Rechenbeispiele**, keine optische Messung;
LED-Höhe, Leuchtfläche und Acrylbrechung verändern die tatsächlichen Werte.

**Korrektur:** Den ganzen Lichtweg auslegen: vergrößerte Öffnung bis
nahe an die LED oder gefangener Lichtleiter ohne Druck auf die LED.
Papierfenster und klare Platten mitprüfen. Nicht nur die Oberkante aufweiten.

### F12 - Mittel: Die kompakte Bauweise braucht eine Montage- und Fertigungsprüfung

Die Stecker-Mating-Ebenen liegen nicht in einer gemeinsamen Rechteckkontur.
Muttern brauchen Zugang zum Anziehen; PCB, fest verlötete Buchsen und
Blenden müssen ohne Biegen zusammengebaut werden können. Stützsäulen
und Gehäuserippen dürfen keine Stecker aus ihren Buchsen hebeln.
Die einteilige V3 hat ihrerseits große überhängende Haubenkammern und
lokal dünne Auflagen; ihre Druckbarkeit ist noch nicht am Ausdruck geprüft.

**Korrektur:** Durchgehender oberer Schutzträger, separate austauschbare
Anschlussblenden und abnehmbare Wanne sind eine sinnvolle Kombination.
Blenden zunächst lose ausrichten, am Gehäuserahmen befestigen und erst
dann Buchsenmuttern anlegen. Zugängliche Schrauben, Druckorientierung,
entfernbare Stützen und Platz für Montagewerkzeug im CAD berücksichtigen.
Lüftungsöffnungen mit Griffschutz vorsehen; Temperatur im Betrieb ist offen.

## Was am bisherigen Entwurf bestätigt bleibt

- Alle **52** deklarierten Mittelpunkte (16 Taster, 16 LEDs, acht Potis,
  zwei Kippschalter, sechs Eurorack-Buchsen, vier PCB-Bohrungen) stimmen
  innerhalb 0,000001 mm mit den Rev4-Footprintpositionen überein.
- Die aktuelle STL-Ausrichtung besteht die unabhängig festgelegten
  Material-/Öffnungslandmarken: Audio hinten, MIDI rechts, Seed links.
  Ein neuer Gegenentwurf muss nach Export dieselben Prüfungen bestehen.
- Die Korrektur von Kopfhörer/Rad besteht ihre begrenzten 15 Paarprüfungen.
- Die plane Acryl-/Papierfläche um erhöhte Schutzhauben ist als Aufbau
  sinnvoll. Eine vollständig rechteckige flache Fläche über sämtlichen
  hohen Buchsen würde die kleinen Bedienelemente zu tief versenken.

## Empfohlene gemeinsame Konstruktion

1. Durchgehenden Schutzträger mit integriertem festen Kappenhalter aus V3
   weiterverwenden; zusätzliche Haltehülsen für die **vorhandenen** Kappen
   konstruktiv ausarbeiten.
2. Neutrik-Rückblende mit Muttern und definiertem Kraftweg in den Rahmen;
   separate passende Halter für beide USB-Anschlüsse. Keine bloßen Freilöcher
   als mechanische Entlastung ausgeben.
3. Wenige gezielt platzierte, isolierte und höhenangepasste PCB-Stützen;
   keine unkontrollierte harte Mehrpunktverspannung.
4. Metallpfosten bis an die feste untere Acrylplatte. Obere Klarplatte und
   Papier bleiben separat lösbar; Buchsen-/Schaltermuttern werden beim
   normalen Beschriftungswechsel nicht gelöst.
5. Zwei gefangene Seed-Betätiger mit Gehäuseanschlägen; Radmulde,
   Kopfhörer-Frontöffnung und microSD-Fingermulde als eigene Zugänge.
6. Außenmaß erst nach den Anschluss-/Muttern-/Montageebenen festlegen.
   Vor einem vollständigen Druck einen lokalen Funktionsabschnitt für
   Kappenhalter und den rechten Anschlussbereich erstellen und erproben.

## Nachweise, Reproduktion und Quellen

Neue Ergebnisse stehen in
[support-location-review.json](orientation-audit/support-location-review.json)
und [functional-review-probes.json](orientation-audit/functional-review-probes.json).
Die Überschneidungen darin sind **digitale Prüfergebnisse**, keine
Hardwaremessungen. Die Gehäuse-STLs wurden für diese Prüfung nicht geändert.

```powershell
& 'C:/Program Files/KiCad/10.0/bin/python.exe' hardware/desktop-case/audit_support_locations.py
.tools/case/Scripts/python.exe hardware/desktop-case/audit_review_geometry.py
.tools/case/Scripts/python.exe hardware/desktop-case/check_stl_orientation.py --output hardware/desktop-case/output/review-orientation-check.json
.tools/case/Scripts/python.exe hardware/desktop-case/check_right_access.py --output hardware/desktop-case/output/review-access-check.json
```

Alle vier Prüfungen lokal ausgeführt. Keine Firmware-/CI-Ausführung erforderlich.
Kein Belastungs-, Ergonomie-, Temperatur- oder Druckversuch durchgeführt.

Primärgrundlagen, am 25.10.2026 eingesehen:

- [Daisy Rev4 PCB, gepinnter Herstellerstand](https://github.com/daisyaudio/Seed3-DevKit-Desktop/tree/987297fb7c9a8c58721ef40abf6d6eedd1fcda2a/pcb),
  lokal SHA-256 `bb9c37d795268f830b6ceeaa0dc6b952a4b547a3f253c8ff4d3251e62035aa68`.
- [Offizielle BOM](https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed-3-desktop/Seed3-DevKit-Desktop_Rev4-bom.csv).
- [Neutrik NMJ6HCD2](https://www.neutrik.com/en/product/nmj6hcd2),
  [Montagezeichnung](https://www.neutrik.com/media/8582/download/st-nmj6hcd2.PDF?v=3)
  und [Hersteller-STEP](https://www.neutrik.com/media/15276/download/3-D%20NMJ6HCD2.stp?v=1).
- [E-Switch 1S](https://www.e-switch.com/product/1s/),
  [1SBLK-Zeichnung](https://configured-product-images.s3.amazonaws.com/2D/specs/1SBLK.pdf),
  [1RLGR-Zeichnung](https://configured-product-images.s3.amazonaws.com/2D/specs/1RLGR.pdf)
  und [TL1105-Zeichnung](https://www.e-switch.com/wp-content/uploads/2022/06/TL1105.pdf).
- [Same Sky SJ-352X-SMT, mechanische Zeichnung S. 2](https://www.sameskydevices.com/product/resource/sj-352x-smt.pdf).
- [GCT USB4110-Referenzzeichnung](https://gct.co/files/drawings/usb4110.pdf),
  mit ausdrücklich ungeklärter Gleichheit zur Bestückungs-MPN.
- [Alps RK10J12R0A0B](https://tech.alpsalpine.com/e/products/detail/RK10J12R0A0B/).
- [Befaco Knurlies](https://www.befaco.org/knurlies/).

Die Hersteller-PDFs wurden lokal gerendert und die relevanten Zeichnungen
visuell gelesen. Die Rechnungsdatei wird nicht ins Repository übernommen.
