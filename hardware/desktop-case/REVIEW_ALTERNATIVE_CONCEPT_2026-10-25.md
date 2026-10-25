# Prüfung des aktuellen Gegenentwurfs Desktop-Gehäuse

**Prüfdatum: 25.10.2026. Hardware NICHT GEMESSEN.**

Geprüft wurde ausschließlich der aktuelle Textstand von
`ALTERNATIVE_CONCEPT.md`: HEAD `7b741684b9c2995080eeac163ab8d3066defafa0`
**plus die uncommitteten Änderungen**. Zeilenangaben beziehen sich auf diesen
452-zeiligen Arbeitsstand, nicht allein auf den Commit.

- Eingabe-SHA-256: `934a302ace74caf530a0110f901b90d70f52ba6debcc71de352c767f20dc377f`.
- SHA-256 von `git diff HEAD` zu Prüfungsbeginn:
  `a1d6468b7e205b5caedc47864baf5609b31c4e639fa06b2b777c18dced649ec0`.
- Arbeitsverzeichnis: `C:/Users/ssche/.codex/worktrees/desktop-case/Daisy-Seed`;
  Branch `codex/desktop-case`.
- Es gibt noch kein CAD des revidierten Gegenentwurfs. Geometrieprüfungen an
  bestehenden STLs betreffen ausdrücklich V3, nicht die vorgeschlagene Nische,
  neue Stützen, Hülsen oder Rückblende.
- Keine Änderung an Konzept, STL, SVG oder bisherigen Prüfberichten; kein
  Commit, Push, Rebase, Reset, Firmware-Build oder Hardwarezugriff.

## Bewertungsmaßstab und Ergebnis

**richtig** bedeutet: als Nennmaß, Rechnung oder im gelesenen Quellstand
belegt. Es bedeutet keine Passungs-, Ergonomie- oder Festigkeitsfreigabe.
**falsch** bedeutet: widersprüchlich, rechnerisch falsch oder als allgemeine
Aussage widerlegt. **unbelegt** bedeutet: konstruktiver Vorschlag, fehlende
Randbedingung oder eine bisher nicht nachgewiesene Funktion. Reine
Zielsetzungen werden als Ziele bewertet und nicht als Messergebnisse.

Alle nachstehenden Maßzahlen sind **NOMINAL**, aus Dateien bestimmt oder
rechnerische Beispiele. Kein Maß wurde am realen Board GEMESSEN.

Der Gegenentwurf ist als Richtung sinnvoll, aber noch keine verbindliche
CAD-Spezifikation. Besonders zu bereinigen sind das Verhältnis von
Radfenster und Dachstärke, die federlosen Seed-Stößel, die Begründung für
getrennte Kappenplatten, die Schrauben-/Dome-Toleranzkette und die
Softwarevoraussetzungen des SD-Kartenlesers. Die Zahlen 4,8 / 3,4 / 1,8 /
3,85 / 10,2 mm stimmen als geometrische Nennrechnungen.

Quellenkürzel sind am Ende vollständig aufgelöst. Bei einer Tabellenzeile
ist der Abschnitt durch die Überschrift, die konkrete Aussage durch die
Zeilennummer und den Text in Spalte 2 festgelegt.

## A. Eingangstext, Ziele und Bewertung der bestehenden V3

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| A01 | Z. 6-13: Konzept, kein CAD; Hardware nicht gemessen; Kopfhörer bleibt Anforderung | Arbeitsstand, vorhandene V3-Dateien; kein revidierter Generator oder Export | richtig | Diese Unterscheidung auch bei Bildern, Einkaufslisten und Prüfergebnissen erhalten. |
| A02 | Z. 17-24: Schutz, Zugang, beide USB, 16 Kappen und Papierwechsel | Nutzeranforderungen; BOM nennt 16 TL1105SPF250Q, acht Potis, zwei Kippschalter | richtig | Als Abnahmekriterien formulieren; Schutz und Bedienbarkeit sind noch nicht bewiesen. Die Rangfolge der tatsächlich größten mechanischen Belastungen ist ohne Kräfte und Kabelhebel unbelegt. |
| A03 | Z. 31-33: Alle XY-Ausschnitte treffen KiCad ohne Abweichung | [K], [S]: 52 deklarierte Mittelpunkte stimmen innerhalb 0,000001 mm; dieser Test prüft nicht sämtliche Öffnungsränder oder Werkzeughüllen | unbelegt | Präzisieren: Mittelpunkte stimmen digital überein; Lochgrößen und vollständige Zugänglichkeit werden gesondert geprüft. Keine Fertigungsgenauigkeit aus den Nachkommastellen ableiten. |
| A04 | Z. 33-34: Gestufte Fläche sei durch die Buchsen erzwungen; Schulter 9, Gewindeende 13,5 mm | [C] setzt diese Maße nominal; 9 + 4,5 = 13,5. Die Wahl einer tieferen Bedienfläche folgt zusätzlich den kurzen Tastern und unterschiedlich hohen Anschlüssen | unbelegt | 9 / 13,5 als geplante Buchsen-Nennmaße kennzeichnen. Eine Stufe ist eine sinnvolle Lösung bei dieser Bedienhöhe, kein allgemeines geometrisches Muss. |
| A05 | Z. 34-37: Kraftpfad über unteres Acryl/Träger/Wanne, Papierwechsel durch vier Knurlies | V3 hat Auflageflächen und Muttern; kein Belastungsversuch. Z. 385-388 nennt weitere Demontageschritte | unbelegt | Als vorgesehener Kraftpfad, nicht als nachgewiesen gut bezeichnen. Für Papierwechsel ausdrücklich Kabel/Knöpfe/Schalterstellungen ergänzen. |
| A06 | A1: Ø11,4, Platte <4,7, Steckkraft bis 30 N, Ziehkraft >10 N | [N1], [N2]; Hersteller nennt Einsteckkraft **<30 N**, nicht eine gemessene Kraft dieses Boards | richtig | Grenz-/Katalogwerte nennen; daraus allein keine zulässige Gehäuse- oder PCB-Belastung ableiten. |
| A07 | A1: Das Gehäuse trage nichts von den Anschlusskräften | V3 hat keine direkte Buchsenklemmung, aber das PCB ist mit Pfosten an der Wanne befestigt; Kabelbinderschlitze existieren ebenfalls | falsch | Keine direkte Buchsenabstützung; Lasten werden über Anschlussbefestigung und PCB in die Pfosten/Wanne geleitet. Lastanteile sind nicht berechnet. |
| A08 | A2/A3: Keine direkten USB-/DIN-Sättel; PCB nur H1-H4, Raster 187 × 101 | Generatoren; 192-5 = 187, 106-5 = 101 mm | richtig | Direkte Gehäuseabstützung ergänzen. Durchbiegung als Risiko, nicht als bereits gemessenen Effekt dokumentieren. |
| A09 | A4: 0,8 mm Druckauflage und 3,05 mm Knurlie-Eingriff | [C], [V]: 6 - 0,8 - 1 - 0,15 - 1 = 3,05 | richtig | Geometrische Nenn-Einschraubtiefe, tatsächliches Vollgewinde und Toleranzen trennen. Kriechen/Lockern/Verschleiß sind mögliche Folgen, keine beobachteten Ergebnisse. |
| A10 | A5: Schalter-Schulterhöhe unbekannt; Spalt kann Lötstellen belasten | [C]: `toggle_shoulder_above_pcb=null`; direkte PCB-Montage bestimmt nicht automatisch die Schulter | richtig | Distanzierung nach tatsächlicher Schulterhöhe, nicht durch Anziehen herstellen. |
| A11 | B1/Z. 149: Karte nur mit Werkzeug bzw. Pinzette wechselbar | 22 × 7-mm-Freiausschnitt und etwa 9-mm-Rand im V3-Code; kein Hand-/Fingernagel-/Auswerfversuch | unbelegt | Schwieriger Fingerzugang; Werkzeugbedarf und sichere Entnahme erst prüfen. Pinzette ist eine mögliche Werkzeugwahl, kein geometrisch bewiesenes Muss. |
| A12 | B2/B3: Ø14-Audiostecker bzw. 12×6-USB-Hülle kollidieren | [F] read-only erneut berechnet: Audio Wanne 32,850955, Träger 0,577074 mm³; USB-Referenz Wanne 23,615990 mm³ | richtig | Dies sind definierte Prüfkörper. Nicht behaupten, jedes reale Kabel kollidiere. J7-Gleichheit mit GCT ist offen. |
| A13 | B4/B6: Seed-Stiftzugänge vorläufig; Knöpfe/Kabel/Schalter können Abheben blockieren | [C], [U], geschlossene Plattenkonturen | richtig | Vollständigen Abhebeweg mit allen Teilen in Einbaulage prüfen. |
| A14 | B5: etwa ±15° Sichtkegel bei Ø4,8 / 9 mm | atan(2,4 / 9) = 14,931° im Punktmodell, Winkel **zur Senkrechten** | richtig | Von den späteren 70°/40° Blickhöhen über der Horizontalen unterscheiden; LED-Höhe und Acryl verändern die Optik. |
| A15 | Z. 62-70: 8,9 statt 8,14; 0,76 Differenz; SW8-Eckmaß 9,24; Potisteg 1,279 | [N1], [C], [F]; 8/cos30° = 9,2376; 15,279-14 = 1,279 | richtig | SW8 als angenommene Mutter kennzeichnen. Die mitgelieferte Mutter ist nicht identifiziert. |
| A16 | Z. 65-67: Kappenhöhe 13, Seed-Höhe 9 und weitere Gehäuselagen angenommen; J7-MPN-Abweichung | [C], [B], [R] | richtig | Diese Annahmen nicht später als nachgewiesene Passmaße verwenden. |
| A17 | Z. 71-72: hintere Haube ende bei PCB+23; Höhe 39,8 | [V]: **Unterseite** des Dachs +23, Außenseite +25,4; 14,4+25,4 = 39,8 | falsch | Dachunter- und Dachoberseite benennen. Auch die Ø13,6-Öffnung endet im alten Code bei +15,7; das ist kein Bauteilhöhenmaß. Die gemeinsame DIN-Dachhöhe ist eine Generatorentscheidung, keine Zwangshöhe. |
| A18 | Z. 76-82: 9-mm-Rand; hohle Hauben; Kappen nur geführt, Papier0,15 | [V], [U]; keine Auszugssicherung an den 5,8-/6,4-mm-Öffnungen | richtig | Der feste Aufbau muss die Kappen beim Papierwechsel halten;0,15 ist eine angenommene Papierdicke. |
| A19 | Z. 83-84: Nur ein Teil unterhalb der unteren Platte könne fixieren | Die Rückhaltung muss von der abnehmbaren Klarplatte unabhängig sein, aber ihre Lage folgt daraus nicht zwingend | falsch | Auch ein fest befestigter Retainer an/über der unteren Platte wäre möglich. „Feste Rückhaltung unabhängig von der oberen Klarplatte“ ist die tatsächliche Anforderung. |

## B. Punkt 1 und 2: Kontur und Neutrik-Rückblende

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| N01 | Z. 91-106: separate Bezugsebenen; 203 × 123 nur Ziel; Anlage Y 3,50 / 3,535 | [K], [N1]: Pin T bei J4 Y20,200, J1-J3 Y20,235; jeweils minus16,7. Buchsenachsen X = 39,915 / 62,400 / 140,400 / 162,800 | richtig | Diese vier nominalen Einbaulagen einzeln verwenden; 0,035 mm Differenz nicht durch eine als exakt gemeinsam angenommene Anlage verstecken. |
| N02 | Z. 106/B2: Nase bis Y -2,29 | [N1], [N3], Übertragung 3,50-5,79 = -2,29; für J1-J3 dagegen 3,535-5,79 = **-2,255** | richtig | -2,29 ausdrücklich auf J4 beziehen. Die 5,79-mm-Geometrie ist keine bestätigte vollständig nutzbare Gewindelänge. |
| N03 | Z. 108-110: Anlage liegt über dem PCB; Bodennut unmöglich | Ebene Y3,5 liegt innerhalb des PCBs. Eine unveränderte Vollblende bis zum Boden würde das PCB schneiden | falsch | Nur die einfache durchgehende Vollblende ist dort ausgeschlossen. Eine Blende mit PCB-Durchlass oder außenliegenden Füßen könnte weiterhin in einer Bodennut gehalten werden. Der integrierte Haubenansatz bleibt eine zulässige Alternative. |
| N04 | Z. 111-114: U-Schlitz von unten, Breite 11,4; Träger senkrecht aufsetzbar | [N1] spezifiziert ein **geschlossenes** Ø11,4-Panelbohrbild; das neue offene Schlitzbild ist nicht freigegeben | unbelegt | Montageprinzip geometrisch plausibel. Schlitzbreite mit Fertigungsspiel, verbleibende Wangen, untere Kante, Verrundung und Einsetzweg auslegen. Ein offener Schlitz bleibt gegen vertikales Abheben auf separate Befestigung angewiesen. |
| N05 | Z. 113-114: Scheibe/Mutter decken Schlitz ab und machen ihn tragfähig | Keine Last-/Momentannahmen, Material- oder Scheibenabmessungen, kein Anzugsmoment bzw. Nachweis der Restflächen | unbelegt | Auflage muss beidseitig tragfähige Flächen erreichen. Eine übliche Scheibe schließt nicht automatisch den gesamten nach unten verlängerten Schlitz. Separate verschraubte Schließleiste oder austauschbares Blendenteil mit geschlossenem Loch ist konstruktiv eindeutiger. U-Schlitz nur nach Last-/Montageprobe freigeben. |
| N06 | Z. 115-116: Plattendicke so wählen, dass Scheibe und Mutter auf 5,79 mm voll greifen; <4,7 | [N1] enthält axiale Maße 3,74 / 5,79 sowie Darstellungen für 1,2 / 3 mm; [N2] nennt <4,7. Vollständiger Gewindeanfang, Gewindeauslauf und erforderliche Überdeckung sind damit nicht festgelegt | unbelegt | Aus **5,79 allein lässt sich keine sichere maximale Dicke für vollen Gewindeeingriff bestimmen**. 2 oder 3 mm sind mögliche Startwerte unter der Herstellergrenze; reale Scheibe, Mutter, Anlagestufen und nutzbare Gewindestrecke abgleichen. Nicht pauschal die gesamte Mutternbauhöhe von 5,79 abziehen. |
| N07 | Z. 117: SW11, Eckmaß etwa12,7 | [N1]: A/F11; 11/cos30° = 12,7017 | richtig | Für Werkzeug- und Scheibenhüllen zusätzlich deren reale Außenmaße verwenden. |
| N08 | Z. 118-120: Ø16-Nuss, Achse +8,14, etwa22,5 Achsabstand | Ø16 ist kein universelles Nussmaß. Über der PCB-Fläche: 8,14-16/2 = **0,14 mm** nominal. KiCad-Abstände: J4-J3 **22,485**, J2-J1 **22,400** mm | richtig | Als Rechenbeispiel kennzeichnen. Über PCB ist theoretisch D≤16,28 ohne Spiel; außerhalb des PCB-Randes entfällt diese spezielle Grenze. Reale Nuss/Gabel, Schwenkweg, Blende und Nachbarmutter prüfen. Bei D16 neben einer SW11-Mutter bleiben horizontal rund 8,05 mm; vertikal ist der Fall wesentlich knapper. |
| N09 | Z. 121-123: Verzahnung und eigene Schrauben von unten sichern Haube | Keine Positionen, Restwand- oder Lastdaten; unterseitiger Werkzeugweg fehlt | unbelegt | Formschluss gegen alle relevanten Bewegungen, Schraubenanzahl und Einbau-/Wartungsfolge definieren. Scheibenreibung allein ersetzt den Abhebeanschlag nicht. |

**Zur gefragten Blendenstärke:** Belegt ist die Herstellergrenze <4,7 mm,
nicht eine bestimmte Dicke mit garantiert vollem Eingriff. Bei der
idealisierenden Annahme einer außenliegenden vollständig nutzbaren Strecke
L gilt `t_Platte + t_Scheibe + e_erforderlich + Reserve ≤ L`. Für diesen
Beschlag sind L und die erforderliche wirksame Gewindeüberdeckung nicht
vollständig spezifiziert; L darf nicht ohne Weiteres mit 5,79 gleichgesetzt
werden. Eine Musterblende und die tatsächlich vorhandenen Beschläge sind
hier zwingend. Das beantwortet die Frage als **noch nicht freigabefähig**,
nicht als vermeintlich berechnete sichere Maximalstärke.

## C. Punkt 3: beide USB-Halter

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| U01 | Z. 127-128: Seed-Zugang Y67,5-82,5, PCB+5 bis+14 | [V]: `box(-11,67.5,PCB_TOP+5,15,15,9)` | richtig | Das ist der V3-Ausschnitt, kein Bauteilmaß und noch keine Öffnung des Gegenentwurfs. |
| U02 | Z. 130-133: Frontanschlag größer als Steckergesicht, kleiner als Schirmblech; rückwärtiger Anschlag | Rückwärtiger Anschlag ist als Steckkraftpfad plausibel; frontseitige belastbare Anlage ist nicht aus der bloßen Schirm-Außenkontur bewiesen | unbelegt | USB-Mating-Keepout einschließlich Steckerhülle einhalten. Nur vorgesehene robuste Gehäusestufen/Laschen abfangen. Kein vorderer Kragen, der Einstecktiefe oder Verriegelung verkürzt. |
| U03 | Z. 134-137: oben/unten halten, nur robuste Flächen; Toleranzkette | Mechanisch sinnvoller Konstruktionsgrundsatz, aber Kontaktflächen und Kräfte sind nicht bemessen | richtig | Justierbare/definierte Kontakte, keine unkontrollierte harte Vorspannung; konkreten Halter später am realen Gehäuse prüfen. |
| U04 | Z. 138-142: Seed-Modul darf nicht ausgehebelt werden; J7 nach realem MPN | [B]: J7 HYCW445-USBC16-730B; Footprint/Referenzmodell USB4110-GF-A. Seed ist separat gesteckt | richtig | Beide Halter getrennt konstruieren. Ein Niederhalter irgendwo neben USB darf nicht auf beliebigen Modul-SMDs oder auf ungestütztem PCB liegen. |
| U05 | Lage des Seed-USB passt zum V3-Zugang | [R], aktuelles Hersteller-FBX erneut nur lesend importiert, gleiche RH-Transformation: USB-Baugruppenbounds X0,120-7,820; internes Y68,050-79,350; Z22,775-26,660 mm bei PCB14,4 | richtig | Y passt in 67,5-82,5; Z relativ PCB etwa8,375-12,260 passt in5-14. Bounds enthalten das ganze Modell, sind kein präzises Mating- oder Sattelmaß. Sitzhöhe +9 bleibt angenommen. |
| U06 | Funktionelle USB-Abstützung sei damit gelöst | Kein toleriertes Haltermodell; keine Zug-/Steckprobe. Der Modellabgleich beweist keinen axialen Kraftpfad | unbelegt | Erforderliche Anschlagflächen, Abstand zu Nachbarbauteilen, Demontage des Moduls und eingesteckte Steckerhülle gemeinsam abnehmen. |

## D. Punkt 4: MIDI, microSD und Software

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| D01 | Z. 146-147: 57PC5F-Zeichnung noch beschaffen | [DIN] ist als offizielles Switchcraft-Kundendokument verfügbar | richtig | Als eigene offene Aufgabe verständlich, aber keine unbekannte Herstellerzeichnung: konkrete Quelle unten nutzen; Halter nicht aus dem vereinfachten V3-Zylinder ableiten. |
| D02 | Z. 149-151: Pinzette als akzeptabler Kompromiss bei Windows-Zugriff | Weder Entnahmeergonomie noch J7-Laufwerksfunktion auf Seed3 gemessen | unbelegt | Physischer Kartenwechsel muss bei defekter/alter Firmware möglich bleiben. Ein Softwareplan ersetzt keinen nachgewiesenen Wartungszugang. |
| D03 | Z. 153-157: CardReader-Image, CDC+MSC über J7, nur Softwaretests | [SD] auf `feat/shared-sd-driver` / `ffcfafa5096b37f331448d729aa337499f0655fc`; Makefile/Implementierung vorhanden | richtig | Existenz eines Build-Ziels/Implementierung von der Existenz eines hier geprüften Binärartefakts trennen. Windows-Enumeration und R1-R12 sind offen. |
| D09 | Z. 153: Konkrete Datei `DaisyDiagnostic-CardReader.bin` existiere bereits | Hier wurden Quelltext und Buildziel geprüft, kein passendes Binärartefakt mit Version/Hash abgenommen | unbelegt | „CardReader-Variante ist implementiert und als Buildziel vorhanden.“ Ein auslieferbares konkretes Image separat mit Commit und Hash belegen. |
| D04 | Z. 155-156: exklusiv Windows, Rückgabe erst nach Auswerfen | [SD] beschreibt Share → EJECTED → explizites Return. `CardReader::Return()` lässt zusätzlich INTERRUPTED/ERROR nach Bestätigung zu | falsch | „Im normalen Erfolgsablauf nach Auswerfen und bestätigtem Abschluss; bei Unterbrechung separater Fehler-/Acknowledge-Pfad.“ App-Schließen ist kein Auswerfen. Laufwerksbuchstabe hängt zudem von Partition/Dateisystem/Mount-Policy ab. |
| D05 | Z. 158-160: Leser müsse in `common/sd` integriert werden | `common/sd/PLAN.md` S5/S6: gemeinsamer **read-only** Treiber; USB-Leser bleibt eigene Komponente. Späterer Leserstartmodus für Nebulae optional und erst nach Hardwareabnahme | falsch | Ownership und sauberes Unmount/Remount gemeinsam anbieten; USB-MSC als separate Schicht integrieren. Schreibende USB-Blockzugriffe nicht als normalen lokalen FatFs-Schreibbetrieb in den RO-Treiber einbauen. |
| D06 | Z. 158-160: Jede SD-App per Tastenfunktion oder HTML/JUCE/VST3 auslösbar | [SD] und [BL] implementieren diese universelle SD-Freigabe nicht. PR40-Control-Apps fordern **Bootloader**, keine SD-Freigabe an | unbelegt | Als neues, separat zu spezifizierendes Entwicklungsziel führen. USB-Klassen, Audiosuspendierung, Kartenbesitz und Wiederanlauf je App sind zusätzliche Arbeit. |
| D07 | Z. 161-163: Öffnung mit Fase bzw. Fingermulde | Plausible Konstruktionsoption; keine verriegelte Kartenfront-/Eject-Hub-/Greifmaße | unbelegt | Einstecken, Push-Push-Auswerfen, Greifen und Herausziehen mit Karte und Deckel prüfen. Werkzeug darf Fassung oder Bestückung nicht als Hebellager benutzen. |

## E. Punkt 5: geschlossene Radnische, H4, MIDI und Kopfhörer

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| R01 | Z. 167/171-172: Ø14×1, Mitte191,325/91,6, Rand198,3, Überstand1,3; Scheibe+1,5 bis+2,5 | [W], [A], [K]: 191,325+7 = **198,325**, minus197 = **1,325**. Y84,6-98,6 | richtig | Als gerundete Nennwerte und Zeichnungs-/Modelllage kennzeichnen. Exakte Einbaulage am Board offen. |
| R02 | Z. 173: V3-Wand206, Tiefe7,7 | 206-198,325 = **7,675** | richtig | Gilt für die frühere Wand-/Rim-Projektion; aktuelle ACCESS-V3 hat bereits einen offenen Seitenausschnitt. |
| R03 | Z. 174: Geschlossenes Dach mache Fingerzugang kaum möglich | Keine Finger-/Dachmaße und noch kein neues CAD | unbelegt | Mit konkreter Fingerform bewerten. Eine gerundete Kuppe kann näher eintauchen als ein über die ganze Länge Ø14 dicker Zylinder. Ein Dach allein beweist keine Unbedienbarkeit. |
| R04 | Z. 175-178: X≈200, 1,5-2 Abstand, Y-Länge26, Z-3 bis+8 | Bei X200 Abstand1,675. Mittig angenommene26-mm-Nische ergibt Y**78,6-104,6**, nicht nur den Radbereich84,6-98,6 | unbelegt | Zentrum, innere Tiefe, seitliche Wanddicke, Verrundungen und vollständige XY-Kontur festlegen. Es sind Startwerte, keine geprüfte Passung. |
| R05 | Z. 177: Unter PCB-Ebene erlaubt, weil PCB bei197 endet | Nur der Teil des Ausschnitts mit **X>197** ist damit gegen die Platinenkante frei | falsch | Innere Rundungen/Fingerhülle, Dom und Schnittvolumen dürfen nicht ungeprüft nach X<197 reichen. Die Aussage ist ohne X-Ausdehnung zu allgemein. |
| R06 | Z. 179: Fenster bis+8, Dach aus Träger, Acryl/Papier laufen durch | Unteres Acryl liegt bei+9. Bei Dachunterseite+8 verbleibt bis dahin **nur1,0 mm Dach**, gegenüber V3-Haut2,4 mm | unbelegt | Entweder lokale Dachhaut1 mm bewusst auslegen oder Fensteroberkante absenken bzw. lokale Bauhöhe erhöhen. „Wie V3“ mit2,4-mm-Haut und Fenster bis+8 geht an derselben Stelle nicht gleichzeitig. |
| R07 | Z. 180: Damit entfalle der1,5-mm-Reststeg neben H4 | Alte1,5 mm beziehen sich auf die alte Aussparung Y86-101. Neue26-mm-Nische kann im Y-Bereich bis104,6 an H4 (192;106) heranreichen | unbelegt | Neuer boolescher Schnitt mit tatsächlichem Dom nötig. Dach schließen beseitigt die alte Schwächung nicht automatisch; X-Tiefe und Dome-Außenradius entscheiden. |
| R08 | Z. 178/181: Finger erreicht Rim bei geschlossenem Dach | Rechenbeispiel unten: Ø14-Sphärenkuppe erreicht tangential den Rand bei X198,325; am Fenster X200 benötigt sie Z-2,544 bis+6,544 | richtig | **Nur idealisierte Geometrie**, ohne Reibschluss, Bewegungsweg oder Toleranznachweis. Unter V3-Dachunterseite+6,6 bleiben nur0,056 mm obere Reserve; das ist nicht toleranzfest. |
| R09 | Nische verträgt sich mit J10/J5/H4 | [A], [V]: J10 beiY68,3; bisheriger Ø21-Zugang reicht bisY78,8. Nischenstart78,6 liegt0,2 mm darin. MIDI-Haube reicht bisY86. H4-TascheR3,5 reicht bisY102,5; J5-Frontschnitt beginntY108 | unbelegt | Überschneidung der Y-Bereiche ist noch keine räumliche Kollision. Bei X197,6 als angenommener innerer Wandfläche läge H4-Tasche noch etwa2,27 mm entfernt; bei tieferer Nische kann das anders sein. Haube, Stecker und Dom komplett modellieren. |
| R10 | Kopfhöreröffnung unverändert erhalten | J5-Ausschnitt X166,6-180,6 / Y108-123 liegt im einfachen genannten Nischenbeispiel seitlich getrennt. Allgemeine3D-Unabhängigkeit ist nicht bewiesen | unbelegt | Gerade/gewinkelte Stecker, Fingermulde, H4 und Dach zusammen prüfen. Die15 erfolgreichen ACCESS-Paarungen gelten nicht automatisch für ein neues geschlossenes Dach. |

### Rechenbeispiel zur Fingerkuppe

Kein Handmodell und keine Hardwaremessung: Kugelradius7 mm, Kontaktpunkt
am Radrand `(198,325;91,6;PCB+2)`, Kugelmitte folglich
`(205,325;91,6;PCB+2)`. Am Fenster X200 ist der halbe Querschnitt

`sqrt(7² - (205,325-200)²) = 4,543608 mm`.

Benötigte Eintrittshöhe9,087216 mm; vertikale Grenzen -2,543608 / +6,543608.
Im idealen Fenster -3 bis+8 bleiben unten0,456392 und oben1,456392 mm.
Bleibt die normale V3-Haut hingegen bis+6,6 heruntergezogen, bleiben oben
nur0,056392 mm. Eine **gerundete** Kuppe kann also nominal eintreten;
eine durchgehend Ø14-mm-Zylinderhülle passt nicht in ein11-mm-Fenster.
Zum Drehen muss zusätzlich ein tangentialer Fingerweg frei bleiben.

## F. Punkt 6: Stützpunkte

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| S01 | Z. 186-191: einstellbare isolierende Stützen ohne Vorspannung; weiche Auflage eingerechnet | Mechanisches Prinzip plausibel; keine konkrete Baugruppe | richtig | Einstellzugang und Sicherung gegen Verdrehen/Losdrehen ergänzen. M3-Nylontip und Ø6-Auflageteller sind unterschiedliche Kontaktflächen. |
| S02 | Z. 192-204: zwölf Startpunkte,235 rückseitige Pads, keine rückseitigen Footprints, Viazahlen4/0/0/1/2/0/0/0/0/2/2/0 | [S] und `audit_support_locations.py` im Speicher erneut ausgeführt, ohne Datei zurückzuschreiben: identisches Ergebnis;195 Via-Konturen | richtig | Zahlen gelten für volle Ø6-Scheiben. Alle zwölf Punktkoordinaten sind unten nochmals ausgewiesen. |
| S03 | Z. 207-209: Vias, Leiterbahnen, Maske und Lötüberstände seien keine Bauteilkollision | Via-/Kupferkontur ist nicht automatisch ein mechanisch vorstehendes Teil; **Lötüberstände** können sehr wohl physisch kollidieren | falsch | Kupfer/Maskenzustand von realem Höhenprofil trennen. Eine Auflage über einer Lötspitze ist eine mechanische Kollision; Isolierung beseitigt deren Geometrie nicht. |

| Bereich | Zentrum/zentren, internes PCB-XY | Pads unterØ6 | Vias unterØ6 |
| --- | --- | --- | --- |
| J7 | (10;41) |0|4|
| Audio | (43,5;11), (144;11) |0 /0|0 /0|
| MIDI | (178;37) |0|1|
| Eurorack | (102;23), (77,5;23,5) |0 /0|2 /0|
| Potis | (82;51,5), (112,5;51,5), (143;51,5) |0 /0 /0|0 /0 /0|
| Taster | (87;93), (137;93) |0 /0|2 /2|
| Kippschalter | (39;90) |0|0|

## G. Punkt 7: Schrauben, Pfosten, Z-Kette und Einkauf

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| G01 | Z. 211: Kein Kunststoff im Klemmpaket | Selbst im Gegenentwurf bleiben Acryl, Papier, Knurlie-Kunststoffscheibe und die1,2-mm-Druckbodenauflage in Kraftpfaden | falsch | Titel auf „Keine gedruckte Trägerauflage im oberen Pfosten-Klemmpaket“ begrenzen. Bodenkriechen bleibt separat zu beurteilen. |
| G02 | Z. 213-216: Metall bis unter Acryl bei PCB+9; flächige Auflage | 8 +2×0,5 =9 nominal; eine Pfostenoberseite ist nur lokale Auflage, nicht automatisch flächige Unterstützung der gesamten Acrylplatte | unbelegt | Globale Auflageflächen, Pfostenhöhe und unabhängige Trägerfixierung gemeinsam auslegen. |
| G03 | Z. 224: Boden3, Senkung6,4×1,8, Durchgang3,4; Rest1,2; M3×6 →4,8 | 3-1,8=1,2; 6-1,2=4,8. [ISO] nennt M3-KopfØmax5,7, Höhemax1,65 | richtig | Das ist geometrische Eindringtiefe. Gewindefase, unvollständige erste Gänge und Toleranzen vermindern tragendes Vollgewinde. Kopf liegt bei maximal1,65 etwa0,15 mm unter der äußeren Oberfläche, nicht exakt bündig. |
| G04 | „Wie preenFM2-Wanne V4.1“; vorhandene Schrauben/Muttern | Im Auftrag keine eindeutige Quelldatei/Stückliste dieses Bestands. Ein fremdes Gehäuse ist kein Nachweis für Daisy-Lasten | unbelegt | Maße als eigene Entwurfsvorgaben führen; Bestand nachmessen. Nicht aus früherem Druck eine Freigabe ableiten. |
| G05 | 1,2-mm-Restboden trägt die Pfostenklemmung | Keine Druckrichtung, Toleranzen, zulässige Vorspannung oder Belastung vorgegeben | unbelegt | Tragfähigkeit der Ringauflage prüfen; ggf. Boden lokal verstärken, größere Auflage oder Metallbuchse vorsehen. Bei1,8-mm-Tasche in nur3-mm-Boden ist dies ein eigener kritischer Schnitt. |
| G06 | Z. 225-229: F/F10 durchgehend; PCB1,6/Ø3,5; M/F8, Zapfen6, Innengewinde≥5;2×0,5-Scheiben;2,15-Plattenpaket | [K], [WASH], [C]. 1+0,15+1=2,15; 8+1=9 | richtig | Nenn-Spezifikation ist schlüssig. Konkrete Artikelnummern mit Zapfenlänge, wirklicher Gewindetiefe, Fasen, SW5,5 und Material festlegen; diese folgen nicht allein aus der Körperlänge. |
| G07 | DIN433Ø6 oder DIN125-AØ7 jeweils0,5 fürM3 | [WASH]: DIN433/BN1414 M3 3,2/6/0,5; DIN125-A M3 3,2/7/0,5 | richtig | Außenmaße sind nicht austauschbar bezüglich Freibohrungen und Nachbarteilen. Zwei0,5-mm-Scheiben sind keine präzise1,000-mm-Messdistanz; z.B.0,45-0,55 je Scheibe ergibt0,9-1,1 vor weiteren Toleranzen. |
| G08 | Z. 245-247: Zapfen3,4; Boden4,8; Rest1,8; Variante9 ohne Scheiben: Zapfen4,4, Rest0,8 | 6-1-1,6=3,4;10-3,4-4,8=1,8;6-1,6=4,4;10-4,4-4,8=0,8 | richtig | Alle Werte nominal, für tatsächlich durchgehendes Innengewinde und aufliegende Teile. Die0,8-mm-Variante hat wesentlich weniger Toleranzreserve. |
| G09 | Z. 247-249: Knurlie sei im eigenen oberen Innengewinde dadurch nicht begrenzt | Nur die untere Gegenschraube ist entkoppelt. Sacklochtiefe, Gewindeauslauf und Zapfenbasis begrenzen weiterhin | falsch | „Nicht durch die Bodenschraube begrenzt; wirksame obere Gewindetiefe weiterhin begrenzt.“ Bei5 mm und3,85 mm nominal bleiben1,15 mm bis zum angesetzten Lochende, ohne Fasen-/Toleranzabzug. |
| G10 | Z. 229: 6-2,15=3,85 Knurlie-Eingriff | Rechnung korrekt. [BEF] nennt M3×7 mit Kunststoffscheibe;6 mm nutzbar ist bewusster Planungswert, kein Befaco-Garantiemaß | richtig | Unter der vorhandenen Scheibe messen und volle Gewindegänge bewerten; 6 nicht als tatsächliche Schraubenlänge bestellen. |
| G11 | Z. 251-252: PCB liegt0,2 höher | Alter Boden2,8 +Pfosten10 +PCB1,6 =14,4; neu3+10+1,6 =14,6 | richtig | Alle von PCB abhängigen absoluten Z-Maße um0,2 mitnehmen; Tabelle unten. Nur die Boden-/Kopftaschen bleiben an der Bodenreferenz. |
| G12 | Z. 257-260: DI20/DA11 ungeeignet | 20 statt10 und11 statt effektiv9 passen ohne Änderung nicht in diese Stapelung; physischer Bestand ungeprüft. Die9-mm-Buchsenschulter ist ebenfalls noch nominal | richtig | „Für die gewählte Stapelung ungeeignet“ statt universell ungeeignet. Endgültige obere Höhe erst nach Schulterabgleich festlegen. |
| G13 | Z. 261-264: DIN976 ungenau; Ø3,5/M3 ergibt±0,25; mindestens0,3 Luft | (3,5-3)/2=0,25 radiales Nennspiel korrekt. Gewindestange ist nicht grundsätzlich ungenau; Mutterneinstellung kann definiert werden. Aus0,25 folgt ohne weitere Toleranzen keine garantierte0,3-Reserve | falsch | Ø3,5/M3-Spiel als Teil der XY-Kette nennen;0,3 **je Seite oder diametral** definieren. Fasen, Druck, PCB-/Bauteiltoleranzen und Ausrichtung durch Blende einbeziehen. Ablehnung von Gewindestangen als Bauentscheidung formulieren. |
| G14 | Z. 267-273: M3×12, Haut3,6, Tasche1,8,10,2 unter Haut | Restunterkopf3,6-1,8=1,8; Projektion12-1,8=10,2 | richtig | Mindestens10,2 plus End-/Montagespiel ist **Schraubenfreiraum**, nicht zwangsläufig10,2 Gewindeeingriff oder gesamte Domhöhe. Einsatz-Spezifikation und freie Bohrung darunter unterscheiden. |
| G15 | Domtiefe≥10,2 genüge für Trägerbefestigung | Keine XY-Positionen, Einsatz-Außenmaße oder Domwände. Bei bündiger HautoberseitePCB+9 liegt lokale Unterseite beiPCB+5,4 und die Spitze beiPCB-4,8 | unbelegt | Schraube/Dom kreuzen somit die PCB-Höhenlage. XY muss außerhalb der Platine oder in einer dafür geeigneten vorhandenen Öffnung liegen. Gerade beim203-mm-Zielaußenmaß ist Platz am Rand knapp. Nicht in die bestückte PCB-Fläche hineinplanen. |
| G16 | Z. 231-243/275-277: Standardmaße und vollständige Einkaufsliste | Normmaße existieren; bestandsabhängige bzw. noch nicht spezifizierte Komponenten fehlen | unbelegt | Liste um unten genannte Teile/Spezifikationen ergänzen. SW11 und10-48 UNS-2A sind keine M3-Muttern. M6×0,5 für Eurorack vor Bestellung am tatsächlichen Teil bestätigen. |
| G17 | Boden-Schraube keinesfalls länger6 | Bei exakt angegebenem8-mm+Scheiben-Stapel würde eine8-mm-Schraube die1,8-mm-Lücke um2 mm überfahren: -0,2; bei9-mm-Variante -1,2 | richtig | Das Verbot ist für diese Stapelung sinnvoll; keine allgemeine Normregel. Auch6-mm-Schrauben nur mit tolerierter Restlücke verwenden. |

### Absolute Z-Maße: V3 gegenüber vorgeschlagenem 3-mm-Boden

Alle Werte beziehen sich auf die äußere Bodenunterseite Z0. Die rechte
Spalte übernimmt die bisherigen **relativen Annahmen**, keine Messwerte.

| Ebene | V3 mm | Gegenentwurf mm |
| --- | ---: | ---: |
| Innenboden / Unterkante unterer10-mm-Pfosten |2,8|3,0|
| PCB-Unterseite |12,8|13,0|
| PCB-Oberseite |14,4|14,6|
| Rasterunterseite, PCB+6 |20,4|20,6|
| normale Hautunterseite, PCB+6,6 |21,0|21,2|
| unteres Acryl unten / oben, PCB+9/+10 |23,4 /24,4|23,6 /24,6|
| Papier oben / obere Klarplatte unten |24,55|24,75|
| obere Klarplatte oben, PCB+11,15 |25,55|25,75|
| Seed-Dach innen / außen, PCB+16/+18,4 |30,4 /32,8|30,6 /33,0|
| hohe Haube innen / außen, PCB+23/+25,4 |37,4 /39,8|37,6 /40,0|
| nominale Radscheibe, PCB+1,5 bis+2,5 |15,9-16,9|16,1-17,1|
| Kopfhörerachse, PCB+2,5 |16,9|17,1|
| neue Radfenstergrenzen, PCB-3 bis+8 |nicht V3|11,6-22,6|
| korrekt nominale Audioachse, PCB+8,14 |22,54; V3-Loch liegt stattdessen23,3|22,74|

Beim übernommenen2,4-mm-Hochdach wächst die Gesamthöhe damit auf40,0 mm.
Wird das Dach gesondert abgesenkt oder die Radhaut auf1 mm verändert,
gilt diese Gesamthöhenfortschreibung nur noch für die anderen unveränderten
Bereiche. Der gedrückte Kappenüberstand bleibt bei identischen relativen
Annahmen `13 -0,25 -11,15 =1,60 mm`.

### Fehlende oder zu präzisierende Einkaufsposten

- Vier M3×6 und vier M3×12 **ISO7380-1** ausdrücklich zählen; Bestand,
  Länge unter Kopf, Kopfmaß und Antrieb SW2 prüfen. ISO7380-2-Flanschkopf
  wäre ein anderer Beschlag.
- Artikelnummern/Toleranzen der vier F/F10 und vier M/F8 mit6-mm-Zapfen,
  ≥5-mm-Innengewinde sowie alternativ M/F9 angeben.
- Einschmelzmuttern: vier Stück allein genügt nicht; Länge, Außen- und
  Kernlochdurchmesser, Einbauverfahren, Lage und Domwand fehlen.
- Anzahl/Teile für Nylon-Einstellstützen und deren Sicherung; Auflageteller
  bzw. weiche isolierende Kontaktstücke.
- Zwei Seed-Stößel samt eventuell nötiger Rückstellung/unterer Sicherung;
  16 passende Kappenhülsen. Originalkappen sind vorhanden bestellt, nicht
  mit den noch zu entwickelnden Hülsen gleichzusetzen.
- Rückblende/Schließleisten, deren Befestigung, benötigtes Druckmaterial
  und die nach realer Inventur fehlenden Neutrik-Scheiben/Muttern.
- Sechs Eurorack-/zwei passende Dailywell-Muttersätze samt nötigen
  Distanzteilen; nicht durch „M3-Muttern vorhanden“ abgedeckt.
- 16 Lichtleiter mit Halterung, falls diese Variante gewählt wird.
- Bei beiden Kappenvarianten im Bestand: zwei Sätze unteres/oberes Acryl
  statt pauschal zwei Platten, sofern separate Konturen gewollt bleiben;
  Papier und Ersatzschablonen. Für nur eine gewählte Variante genügen zwei.

## H. Punkt 8: tragende Sättel

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| H01 | Z. 281-283: Rippen und Stützen ergeben Kraftschluss von oben/unten | Tatsächliche Anlage, Steifigkeit und Lage fehlen; das an anderer Stelle zugelassene Spiel kann zunächst gerade keinen Kontakt geben | unbelegt | Kontaktzustand und Lastpfade getrennt spezifizieren. Ein Sattel auf dem Gehäuse schützt nicht automatisch gegen alle axialen, seitlichen und Torsionslasten. |
| H02 | Z. 283-285: Haube Bauteilhöhe plus Wand, Druck-/Werkzeugraum beachten | Sinnvolle Dimensionierungsregel, aber Mindestluft, Verformung und Montageweg fehlen | richtig | „Maximale Bauteilhöhe plus tolerierte Luft plus Wand“. Nicht direkt auf ein gemessenes Einzelmaß aufsitzen lassen. |

## I. Punkt 9: Kappenhülsen und Flansche

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| K01 | Z. 289-290: 1SBLK5×5×9; 1RLGR Fuß5,08, Kopf4,82, Höhe9,05; kein Außenkragen | [E1], [E2], [E3], visuell erneut gelesen; Rundkappe2,84±0,05-Innenaufnahme;1S-Aufnahme2,80 | richtig | Beide Originalteile einschließlich Rast-/Sockelgeometrie und Toleranzen verwenden. Rundkappe-Katalogbezeichnung5×9 ist nur gerundet. |
| K02 | Z. 292-296: Hülse mit Rastnasen, Flansch in Tasche, feste untere Platte | Funktional plausibel, aber Außenmaße, Rastlast, Montage und Führung fehlen | unbelegt | Haltekraft zwischen Kappe und Hülse, zugängliche Montage und begrenzten Kippwinkel nachweisen. Flansch hält die Hülse; die Kappe selbst muss ebenfalls darin gefangen bleiben. |
| K03 | Z. 297-298: Ein rundes Loch lasse eine quadratische5-mm-Kappe nicht durch | Diagonale5√2 =7,0711. Ein ausreichend großes Rundloch lässt sie selbstverständlich durch | falsch | „Ein rundes Loch mit nur etwa5-6 mm Durchmesser reicht für die5×5-Kappe nicht; mindestens Diagonale plus Spiel.“ Formtrennung kann aus Optik/Führung sinnvoll sein, ist kein allgemeines Durchtrittsverbot. |
| K04 | Z. 299-303: Hülse unter Papier, volle0,25 Hub, eigener Anschlag,13 nur Annahme | [T] nennt0,25 **typisch**. Ein Hülsenende bündig unter Papier in Ruhe kann bei freiem Hochziehen/Kippen die obere Platte dennoch berühren | unbelegt | Ruhelage, voller Druck-/Rückkehrweg, Auszugsanschlag und Toleranzen in einem Schnitt bemaßen. Typischen Hub nicht als garantierten Maximalweg behandeln. |
| K05 | Z. 304-315: LED7 mm weg; getrennte Sätze mit gemeinsamem Träger | [K] bestätigt7; Tasterabstand horizontal12,285714..., vertikal16. Gemeinsame Tasche ist möglich, aber nicht konstruiert | richtig | Maximalhülle beider Flansche und Stege als gemeinsame Anforderung festlegen; zwei optische Deckvarianten sind eine zulässige Gestaltungswahl. |
| K06 | Z. 316-318: Rundflansch brauche in Quadratloch erst etwaØ10, deshalb gemeinsame untere Platte unsinnig | Für einen plan geführten Kreisflansch genügt geometrisch D>Quadrat-Seitenmaß, nicht D>Diagonale. Beispiel Quadrat6,5, FlanschØ8:0,75 mm Übergriff an jeder Seitenmitte | falsch | Nicht vollständige Abdeckung aller Loch-Ecken mit Rückhaltung verwechseln. Eine gemeinsame Platte ist möglich; tragende Flächen und Kippsicherung separat dimensionieren. Ø10 folgt ohne Hülsen-/Lochmaß nicht. |
| K07 | Flanschfreiheit neben LED und Tastern | Bei RundflanschØ8 und bestehender LED-ÖffnungØ4,8:7-4-2,4 =0,6 mm nomineller Steg. BeiØ10:7-5-2,4 =-0,4, Überdeckung. NachbarflanscheØ8 lassen12,285714-8 =4,285714 mm | richtig | Rechenbeispiele, keine fertigen Passmaße. Rastnasen, Taschenluft und Lichtleiter-Außenhülle mitrechnen. Ein großer Flansch kann LED-Licht tatsächlich verdecken; die bisherigeØ10-Begründung bleibt trotzdem falsch. |

## J. Punkt 10: Seed-Stößel und Bootloader

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| B01 | Z. 322-326: keine bekannte Katalogverlängerung; Schaft3, Bund5×1, Spitze1,5 | Keine konkrete Kataloglösung belegt; Maße sind neue Entwurfsvorgaben | unbelegt | Nicht als Nicht-Existenzbehauptung formulieren. Für Eigenbau Führung, Einbau von unten, radialen Freiraum und Sicherung in beide Richtungen ergänzen. |
| B02 | SchaftØ3 in übernommenen V3-Ø3-Bohrungen | [V] nutzt Radius1,5 für die Führung; gleicher Nenndurchmesser ergibt **kein** Laufspiel | falsch | Führungsloch toleriert größer als Schaft wählen und als Coupon prüfen; keine genaue3/3-Passung als gleitfähig ausgeben. BundØ5 sichert nur nach oben, solange kein unterer Käfig vorhanden ist. |
| B03 | Z. 327-328: Länge etwa8 aus Dach+16, Taster+11,7, Luft0,3 | Ohne Dachstärke t und Überstand e ist L unbestimmt: `L =16+t+e-(11,7+0,3)=4,0+t+e` | unbelegt | Bei V3-Dach2,4 ist L=6,4+e; **8 mm passt nur mit e=1,6 mm**, sofern Länge Spitze bis Kopfoberseite meint. Bundposition und Spitzenlänge fehlen. +11,7 bleibt V3-Annahme, kein gemessener PTS815-Sitz. |
| B04 | Z. 329-330: keine Feder nötig, Eigengewicht, Taster stellt zurück; zugleich0,3 Leerspiel | Ein frei gleitender Stößel fällt bis zum Tasterkontakt: Spitzenspiel dann0, nicht0,3. Der Taster kann ihn nicht über seine eigene Ruhelage hinaus zurückstellen | falsch | Entweder dauerhaft kontaktierenden, sehr leichten Stößel ohne behaupteten Luftspalt prüfen oder eigene Rückstellung gegen einen Gehäuseanschlag vorsehen. Neigung, Reibung, Klemmen und Überdruckanschlag einbeziehen. |
| B05 | Ø1,5 trifft nur Betätiger | [R]-FBX-Betätigerfläche etwa2,2×3,0 mm; bei perfekt zentrierter Spitze bleiben an der schmalen Seite0,35 mm Zentrierreserve | unbelegt | Transformations-, Führungs- und Einstecklagentoleranz darf diese Reserve nicht aufbrauchen. Physische Modulposition bestätigen. |
| B06 | Z. 331-336: ertastbare Köpfe, RESET dann BOOT etwa2s; ROM-DFU BOOT halten | [BL], [Daisy]: normaler externer Bootloader auf J7; ROM über Seed-USB mit BOOT halten, RESET drücken/lösen, BOOT lösen | richtig | Sequenz statt bloß „gleichzeitig drücken“ nennen. Das2-s-Fenster gehört zur installierten Bootloader-Variante; BOOT/RESET bleiben auch für Wiederherstellung bzw. Variantenwechsel erforderlich, nicht nur beim allerersten Einrichten. |
| B07 | Z. 338-339: `System::ResetToBootloader(DAISY_INFINITE_TIMEOUT)` | libDaisy f715e48 deklariert `BootloaderMode` innerhalb `daisy::System`; [BL] verwendet voll qualifizierten Enumerator | falsch | Als kopierbaren Ausdruck `daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT)` angeben; ohne passenden using-Kontext ist die verkürzte Form nicht eigenständig gültig. |
| B08 | Z. 340-341: Umsetzung läuft getrennt; Stößel nur bei hängender Firmware nötig | PR40 ist bereits implementiert, aber offen/nicht gemergt und Hardware NICHT GEMESSEN. Alte Firmware, fehlender Bootloader und ROM-Recovery brauchen ebenfalls Hardwarezugang | falsch | „Softwareweg in PR40 implementiert; erst nach geeigneter Installation nutzbar. Physische Stößel als unabhängigen Recovery-/Serviceweg behalten.“ |
| B09 | Konsistenz mit PR40: S9+S16, SysEx, Diagnostics-BOOT | [BL] HEADac279c1: S9+S16 allein5s; SysEx für Instrumente/Effekte/Mod-bench; DDP-BOOT für CDC-Diagnostics. CardReader blockiert Bootanforderungen bei Windows-Kartenbesitz | richtig | Im Konzept ergänzen. Editor-Timeout2s ist nicht gleich Tastenhaltezeit5s oder Bootloader-Recoveryfenster2s. Unterstützung und INF_TIMEOUT-Wirkung am Kit bleiben ungemessen. |

## K. Punkt 11: LED-Öffnungen und Blickwinkel

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| L01 | Z. 345-347:16/16 Durchgänge im Träger und Papier, beide Acrylplatten dort geschlossen | Tatsächliche STLs erneut geladen: je16 senkrechte Ø4,6-Prüfsäulen, Z14-28 in Montagekoordinaten; Träger/Papier jeweils0 mm³; jedes Acryl je16,59234151 mm³ | richtig | Aussage ist digital bestätigt. Geschlossenes transparentes Acryl ist kein optisch undurchsichtiger Verschluss; Render-Solid ist kein optischer Nachweis. |
| L02 | Z. 348-349: Solid verdeckt Acryl, Alt+Z oder ausblenden hilft | Blender-Solid zeigt Materialien nicht als physikalische Transmission; X-Ray/Ausblenden ist eine Ansichtsoption | richtig | Nur Darstellungshinweis; keine Aussage über Sichtbarkeit realer LEDs. |
| L03 | Z. 351-355: LEDs7 mm hinter Tastern; 13-mm-Kappe verdeckt bis etwa70° | [K]:7 mm. Im seitlichen Punktmodell mit5-mm-Kappe: `atan(13/(7-2,5))=70,9065°` über Horizontalen | richtig | Gilt beim Blick aus Spieler-Richtung in derselben Tasterspalte, angenommener13-mm-Sitz und LED-Punkt aufPCB. Andere Blickazimute/Kappenprofile/LED-Höhen ändern den Wert. |
| L04 | Ø3 unten beiPCB+6 begrenzt etwa14° | `atan(1,5/6)=14,0362°` **zur Senkrechten**, gleich75,9638° Blickhöhe | richtig | Winkelbezug immer ausschreiben; diesen14°-Wert nicht direkt mit70° Blickhöhe vergleichen. |
| L05 | Z. 357-363: Lichtleiter bis+9, Verdeckung dann etwa40° | `atan((13-9)/(7-2,5))=41,6335°`; Quellehöhe9 und Kappradius2,5 angenommen | richtig | Näherung40° ist vertretbar; für Präzision42° schreiben. Lichtleiterform, Kopplung, Diffusion, Luftspalt, Acrylbrechung und Flanschfreiheit sind weiter unbelegt. |

## L. Punkt 12: Potis

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| P01 | Z. 367: Steg1,279 beiØ14 und15,279-Raster | [K], 15,279-14 | richtig | Als Nenn-Reststeg angeben. |
| P02 | Derselbe Steg sei für1-mm-Acryl zu schwach | Keine Materialqualität, Schnittkerbe, Auflage, Last oder Versuch | unbelegt | Schmal und empfindlich, aber keine bewiesene Festigkeitsaussage. Auflage und Kerbradien verbessern; passende Coupon-/Handhabungsprobe. |
| P03 | Z. 369:Ø11 ergibt etwa4,3 mm Steg | 15,279-11=4,279 | richtig | Als Ausgangswert, nicht als freigegebene Kontur. |
| P04 | Ø11 passt für Poti-Bund und Knöpfe; größere Knöpfe müssen ab | [AL] Standard-RV09AF-40: WelleØ6, Bund ungefährØ7; Sonderausführung `-0085` und reale Knopfschürze nicht bestätigt | unbelegt | Hüllkontur genau auf Höhe beider Acrylplatten, Wellenrundlauf, Schulter und Knopfschürze messen. Ein nominal11-mm-Knopf braucht ebenfalls Abhebespiel; Gleichmaß11/11 ist keine Freigängigkeit. |
| P05 | Langloch schwäche die Platte stärker | Zusammenhängender offener Schlitz entfernt Zwischenstege, aber Wirkung hängt von Randbrücken und Auflage ab | unbelegt | Als wahrscheinlichen Steifigkeits-/Beschriftungsnachteil formulieren, nicht als quantitatives Ergebnis. |

## M. Punkt 13: Muttern und Papierwechsel

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| M01 | Muttern nur unten, große obere Fenster, gemeinsame Bezugsebene | Entspricht gewünschter Wartungsaufteilung; tatsächliche Schulter-/Mutternhöhen offen | richtig | Feste untere Platte und Träger müssen beim Knurlie-Lösen gegen Heben gesichert bleiben. Nicht alle Höhen durch Anziehen erzwingen. |
| M02 | Z. 385-388: Kabel ziehen, Knöpfe über Lochmaß ab, Schalterstellung; Schlitze/Einschub als Alternative | Geometrisch plausibler Wartungsablauf | richtig | Neuer Text beseitigt die frühere Zusage eines reinen Vier-Knurlie-Wechsels. Auch exakt gleich große Knöpfe und Steckerhüllen benötigen Spiel. Eine erreichbare Schalterstellung garantiert den gesamten Schwenk-/Abhebeweg noch nicht. |
| M03 | Papier0,15 und zwei1-mm-Platten geeignet für flexible Labels | Plausible Auslegung, keine Dicken-/Wölbungs-/Klemmprobe. Papier mit geschlossenen Löchern muss über alle betreffenden Bedienelemente | unbelegt | Reale Papierstärke, mehrere Vorlagen, Schnitt-/Druckregistrierung und Nut-/Knopföffnungen erproben. Nicht nur die obere Platte, auch das Blatt selbst muss ausfädelbar sein. |

## N. Punkt 14: Gravur und Rechte

| ID | Aussage / Zeilen | Quelle oder Rechnung | Bewertung | Korrekturvorschlag |
| --- | --- | --- | --- | --- |
| V01 | 0,6 Tiefe,0,8 Strich =zwei0,4-Düsenbahnen, Schrift≥5 | Startwerte für FDM; Extrusionsbreite ist nicht zwingend Düsendurchmesser. Eine Gravur ist ein Negativkanal, keine positive Zwei-Bahn-Wand | unbelegt | Als Probeparameter führen; Font, Gegenformen, Erstschicht und Slicer-Linienbreite prüfen.0,8 mm garantiert weder Schriftlesbarkeit noch zwei extrudierte Bahnen. |
| V02 | Boden auf Druckplatte werde am saubersten | Abhängig von Bettstruktur, erster Schicht, Elefantenfuß und den überbrückten Gravurkanälen | unbelegt | Lokale Beschriftungsprobe. Boden3-0,6=2,4 mm bleibt nominal; an einer1,2-mm-Schraubenauflage wären nur0,6 mm übrig, daher Gravur dort aussparen. Rückhaubenplatz nach Muttern/Werkzeug bemessen. |
| V03 | Logo sei von Electrosmith; öffentliches Repo erfordert Rechteklärung | [TM] nennt **Qu-Bit Electronix, Inc. (dba Daisy)** als Rechteinhaber und DAISY als US-Marke; die Hardwarelizenz gewährt ausdrücklich keine Markenlizenz | falsch | Aktuellen Rechtehinweis korrekt wiedergeben. Electrosmith als historische/umgangssprachliche Zuordnung ist kein belastbarer Lizenznachweis. |
| V04 | Für eigenen Druck unproblematisch; Logo lokal genügt, Schriftzug dürfe ins Repo | [TM] unterscheidet nicht pauschal zwischen grafischem Logo und DAISY-Namen auf Gehäusen. Deutsches Recht behandelt privaten Gebrauch, öffentliche Zugänglichmachung und kennzeichenmäßige Nutzung unterschiedlich [LAW] | falsch | Keine pauschale Freigabe. Herkunft/Lizenz des konkreten Logos klären; bis dahin für öffentliches Gehäuse neutrale Grafik und sachlichen Kompatibilitätshinweis verwenden. Eine private Kopie ist nicht automatisch eine Erlaubnis zur Veröffentlichung. |
| V05 | Logo nicht ohne Klärung in öffentlicheSTL/SVG | Vorsichtiger Umgang mit der konkreten ungeklärten Rechtekette ist sachlich plausibel | richtig | Gleiches für einen dekorativ als Produktmarke verwendeten Schriftzug prüfen. Zulässiger sachlicher Verweis ist etwas anderes als Branding; keine Behauptung einer Herstellerfreigabe. |

## O. Widersprüche und unterschiedliche Revisionsstände der vier Dokumente

| ID | Dokumente / Aussagen | Bewertung | Erforderliche Klarstellung |
| --- | --- | --- | --- |
| X01 | KonzeptPunkt5: geschlossenes Rad-Dach; RIGHT_ACCESS/UNIFIED: nach oben und seitlich offener Ausschnitt, bestehende15 Paarprüfungen | unbelegt | Neue Variante, kein bestätigter Ersatz. AktuelleSTLs bilden weiter ACCESS ab. Ergebnisse dürfen nicht auf das neue Dach übertragen werden. |
| X02 | Konzept: Boden3 / obere Pfosten8+1 / keine gedruckte obere Auflage; UNIFIED:2,8 /8+0,2 /0,8-Auflage | richtig | Absichtliche Variantenänderung, kein Rechenfehler. Im neuenCAD absoluteZ-Werte ändern; bestehendeSTLs/Schablonen entsprechen noch nicht diesem Stapel. |
| X03 | Konzept:16eckig oder16rund plusHülsen; UNIFIED/Render: gemischte Darstellung und bisherige5,8-/6,4-Öffnungen, keine Auszugssicherung | richtig | Konzeptanforderung versus vorhandenerCAD-Stand eindeutig kennzeichnen. Die altenÖffnungen sind kein Hülsen-Passnachweis. |
| X04 | KonzeptPunkt10: keineFeder /Eigengewicht /0,3Leerspiel; DESIGN_REVIEW F5 undUNIFIED: eigeneRückstellung für gefangeneBetätiger noch nötig | falsch | Nicht nur zeitlicher Unterschied: der neue Entwurf hat den Rückstell-/Luftspaltwiderspruch nicht gelöst. Konstruktionsentscheidung nach B04 treffen. |
| X05 | Konzept/B1: ohneWerkzeug bzw.nurPinzette; DESIGN_REVIEW F8/F12: Bedienbarkeit nochoffen | unbelegt | Die alte unbelegte Absolutaussage wurde weitergetragen. Erreichbarkeit nicht als gemessen ausgeben. |
| X06 | Konzept: UmsetzungBootsoftware „läuft“ undStößel nurbeimHänger; PR40: implementiert, offen, ältereFirmware weiterphysisch | falsch | Funktionsumfang, PR-Status und installierten Hardwarestand unterscheiden; keine fertige Fähigkeit des Kits behaupten. |
| X07 | RIGHT_ACCESS beschreibtJ5 nachFootprint, `right-controls.json` hatKörperhöhe6; DESIGN_REVIEW F7 nenntHerstellerhöhe5 | falsch | Bereits bekannte Render-/Konfigurationsabweichung bleibt bestehen. Der alteØ10-Stecker-Hülltest bleibt gültig für seine Achse, aber ist keine vollständige exakteBauteildarstellung. |
| X08 | KonzeptZ71 undhistorischeV3-Beschreibungen verwendenDach+23 teilswieAußenhöhe | falsch | Dachunterseite+23 /Außenhöhe+25,4 getrenntbenennen; neueGesamthöhe40,0 bei gleichemHochdach. |
| X09 | DESIGN_REVIEW bewertet84a6dd8; aktuellesKonzept7b74168+Diff enthältbereitsvieleKorrekturen | richtig | HistorischeFeststellungen zumaltenKonzept nichtalsaktuelleFehlerzählung behandeln. Beispielsweise wurdenFedern, Kappenkragen,USB-Anschläge undVia-Zahlen inzwischen neuformuliert; ihre neueFunktion braucht trotzdemNachweis. |

## P. Zwingende reale Maße vor verbindlichem CAD

Die Hersteller-/PCB-Daten erlauben Vorentwürfe. Für die engen Funktionsstellen
werden vor dem endgültigen CAD diese **NICHT GEMESSENEN** Größen benötigt:

1. **Neutrik:** tatsächliche Anlage-/Achshöhen am verlötetenBoard; Mutter,
   Scheibe, nutzbare Gewinde-/Anlagestufen und eine vollständige Verschraubung
   mit Musterplatte; Außenmaße und Einführweg des eigenen SW11-Werkzeugs.
2. **USB und SD:** reales J7-Gehäuse/MPN samt belastbaren Anschlagflächen,
   Seed-USB-Position bei ganz eingestecktem Modul; verwendeteSteckerhüllen;
   eingerastete SD-Kartenfront, Eindrück-/Auswerfhub und Greifraum.
3. **Rechte Ecke:** Scheibenrand/-höhe desLautstärkerads, H4-Pfosten-/Domhülle,
   tatsächlich gesteckterMIDI-Stecker und Kopfhörerstecker; Fingerhaltung
   samt tangentialem Drehweg unter einer Dachprobe.
4. **Kappen und Seed-Taster:** vollständig montierte1S-/1R-Höhe, Sockel-/Rastform,
   Streuung, Hub/Rückkehr; Seed-Einsteckhöhe, genauePTS815-Betätigerlage,
   Höhe und zulässigerDruckweg; Bedienfolge beiderRecovery-Taster.
5. **Klemmpaket:** wirklicheKnurlie-Nutzlänge unterScheibe, Pfostenkörper,
   Zapfen, Sackloch/Vollgewinde/Fasen; realeSchraubenlängen unterKopf,
   Scheiben-/PCB-/Acryl-/Papierdicken.
6. **Auflagen und obere Platte:** Unterseitenhöhen/Lötüberstände aller gewählten
   Stützen; Eurorack- undKippschalter-Schultern/Gewinde/Muttern;
   Poti-Bund aufPlattenhöhe und Außen-/Unterkontur dergewähltenKnöpfe.

Keine dieserMessungen wird durch einen nominal freienPrüfkörper oder einen
Softwaretest ersetzt. Das Gehäuse bleibt **nicht fertigungsfreigegeben**.

## Q. Verwendete Quellen und lokal ausgeführte Prüfungen

### Lokale Primärdateien und revisionsgebundene Referenzen

- **[K]** `build/case-reference/ES-Seed3-Devkit-Desktop-Rev4.kicad_pcb`,
  Herstellercommit `987297fb7c9a8c58721ef40abf6d6eedd1fcda2a`, SHA-256
  `bb9c37d795268f830b6ceeaa0dc6b952a4b547a3f253c8ff4d3251e62035aa68`;
  mit KiCad10.0.6 nur lesend ausgewertet; `board.json` zum Positionsvergleich.
- **[B]** `build/case-reference/official-bom.csv`,
  [offizielle BOM](https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed-3-desktop/Seed3-DevKit-Desktop_Rev4-bom.csv).
- **[C]** `closed-mechanics.json`, `right-controls.json`,
  `unified-v3/validation.json`.
- **[V]** `build_case.py`, `build_closed_case.py`, `build_unified_case.py` und
  tatsächlich geladene Dateien unter `unified-v3/`; **[U]** `UNIFIED_V3.md`.
- **[S]** `orientation-audit/support-location-review.json` sowie
  `audit_support_locations.py`.
- **[F]** `orientation-audit/functional-review-probes.json` sowie
  `audit_review_geometry.py`.
- **[R]** `realistic-v3/render-manifest.json`, `render_realistic_case.py`,
  offizielles `build/case-reference/seed3-models/ES_Daisy_Seed3_Rev5.fbx`.
  USB-Teil über die Herstellerhierarchie `GCT_USB4110GFA` ausgewählt und mit
  dem vorhandenen rechtshändigen Transform ausgewertet; keine Szene gespeichert.
- **[A]** `RIGHT_ACCESS.md` und `check_right_access.py`.
- **[SD]** `git show feat/shared-sd-driver:diagnostics/docs/SD_CARD_READER.md`,
  `diagnostics/firmware/src/{card_reader,msc_bot}.cpp`,
  `common/sd/{README,PLAN}.md`, lokaler Branchstand
  `ffcfafa5096b37f331448d729aa337499f0655fc`.
- **[BL]** [PR40](https://github.com/sscheidl/Daisy-Seed/pull/40), offen,
  nichtgemergt, Head `ac279c19d90ffc8428c342fedd98cbabcd4da5ce`;
  `common/bootloader/{README.md,bootloader_entry.h}`,
  `diagnostics/firmware/src/diag_app.cpp`, `docs/FLASHING.md` an diesemCommit.
  libDaisy-API direkt an gepinntem `f715e4875b2d28445d0c11a0a17f064a248f69d0`
  gelesen, ohne Submodule zu initialisieren.

### Hersteller- und Rechtsquellen, am 25.10.2026 gelesen

- **[N1]** [Neutrik-Montagezeichnung ST-NMJ6HCD2](https://www.neutrik.com/media/8582/download/st-nmj6hcd2.PDF?v=3),
  lokal `build/case-reference/neutrik-nmj6hcd2.pdf`, gesamte Seite visuell gelesen.
- **[N2]** [Neutrik NMJ6HCD2 Produktdaten](https://www.neutrik.com/en/product/nmj6hcd2).
- **[N3]** [Neutrik-Hersteller-STEP](https://www.neutrik.com/media/15276/download/3-D%20NMJ6HCD2.stp?v=1),
  lokal `build/case-reference/neutrik-nmj6hcd2.stp`.
- **[E1]** [E-Switch1SBLK-Zeichnung](https://configured-product-images.s3.amazonaws.com/2D/specs/1SBLK.pdf),
  lokal `e-switch-1s.pdf`; **[E2]**
  [1RLGR-Zeichnung](https://configured-product-images.s3.amazonaws.com/2D/specs/1RLGR.pdf),
  lokal `e-switch-1r.pdf`; beide komplette Zeichnungen visuell gelesen.
- **[E3]** [E-Switch1S-Produktseite](https://www.e-switch.com/product/1s/).
- **[T]** [E-SwitchTL1105](https://www.e-switch.com/wp-content/uploads/2022/06/TL1105.pdf),
  lokale Zeichnungs-/Bestellseiten;0,25 mm ist ein typischerSchaltweg.
- **[W]** [Alps RK10J12R0A0B](https://tech.alpsalpine.com/e/products/detail/RK10J12R0A0B/)
  und vorhandene lokaleKatalogzeichnung `alps-rk10j.pdf`.
- **[AL]** vorhandener Alpha-Katalog `alpha-rv09.pdf`, Outline-/K-Typ-Seiten
  `alpha-outline.png`, `alpha-k-type.png`; Sonderausführung-0085 nicht separat bestätigt.
- **[DIN]** [Switchcraft57PC5F-Kundendokument](https://www.switchcraft.com/assets/1/24/57PC5F_CD.pdf?5023=).
- **[GCT]** [GCTUSB4110-Referenzzeichnung](https://gct.co/files/drawings/usb4110.pdf);
  sie beweist nicht die Gleichheit mit HYCW445-USBC16-730B.
- **[ISO]** [BöllhoffISO7380-1-Maßtabelle](https://eshop-ro.boellhoff.com/out/media/pdf/ISO_7380-1_Stahl_10.9_Innensechskant___en.pdf).
- **[WASH]** [BossardBN1414/DIN433](https://www.bossard.com/de-de/eshop/scheiben-flach-ohne-fase/scheiben-ohne-fase-fuer-zylinderschrauben/p/1414/),
  [Bossard-Maßtabelle](https://www.radiomag.com.de/datasheets/BN1414-EN-datasheet.pdf)
  (Originaldatenblatt auf Händlerhost),
  [WASIDIN125-A-Maßtabelle](https://www.wasishop.de/pdf_datenblatt/din125a_en.pdf).
- **[BEF]** [BefacoKnurlies](https://www.befaco.org/knurlies/).
- **[Daisy] / [TM]** [Hersteller-README einschließlich Bootfolgen und ausdrücklichem Markenabschnitt](https://github.com/daisyaudio/Seed3-DevKit-Desktop/blob/987297fb7c9a8c58721ef40abf6d6eedd1fcda2a/README.md#trademarks).
- **[LAW]** [MarkenG§14](https://www.gesetze-im-internet.de/markeng/__14.html),
  [MarkenG§23](https://www.gesetze-im-internet.de/markeng/__23.html),
  [UrhG§53](https://www.gesetze-im-internet.de/urhg/__53.html).
  Keine pauschale Aussage über einen konkreten privatenLogo-Nutzungsfall;
  maßgeblich bleiben konkreteRechte, Nutzung und gegebenenfallsErlaubnis.

### Tatsächliche lokale Prüfungen

1. `audit_support_locations.py` im Speicher mit ausgeschalteter Dateiausgabe
   ausgeführt; Ergebnis exakt gegen bestehendes JSON verglichen: **PASS**.
2. `audit_review_geometry.py` ebenso ohne Dateiänderung ausgeführt und
   gegen gespeichertes JSON verglichen: **PASS**.
3. `check_stl_orientation.evaluate()` und `check_right_access.evaluate()`
   nur lesend aufgerufen: beide **PASS**; keine bestehenden Prüfberichte überschrieben.
4. Eigene Ø4,6-mm-Prüfzylinder an allen16 LEDs, von Z14 bis28, gegen die
   vier realen montierten STL-Schichten gerechnet: **16/16** im Träger und
   **16/16** im Papier frei; beide Acrylplatten dort jeweils geschlossen.
5. Eigene Nennrechnungen für Fingerkuppe, Hex-Eckmaße, Pin-/Achslagen,
   Schraubentiefen, Blickwinkel und Stege; Hersteller-FBX nur lesend in
   Blender5.2.1 importiert, keine Szene gespeichert.

Python wurde mit `-B` verwendet; keine Bytecode-Dateien für die Prüfungen
geschrieben. Es wurde kein Firmwaretest gestartet. Abschließend wurden
HEAD, bestehender `git diff HEAD` und SHA-256 der geschützten Text-/STL-/SVG-
Dateien gegen den Eingangszustand verglichen; diese Review-Datei ist die
einzige von dieser Prüfung neu angelegte Datei.
