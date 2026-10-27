# Übergabe: nach dem Testdruck optimieren und einen gefälligeren Stil finden

Stand 27.10.2026. Dies ist der **aktuelle Einstieg für den nächsten Chat**.
Die älteren Abschnitte in `HANDOVER.md` sind Entwicklungsgeschichte.

## Ziel und aktuelle Entscheidung

Der vorhandene vollständige V4-Satz wird als **Pass- und Montageprototyp**
gedruckt. Danach sollen reale Fehler korrigiert und eine professioneller
wirkende Gehäuseform entwickelt werden. Der Nutzer stört sich vor allem an
den **Höhensprüngen und den einzeln aufgesetzten rechteckigen Hauben**.
Rundungen allein lösen diesen gestalterischen Hauptpunkt nicht.

Der Nutzer hat ausdrücklich entschieden, jetzt den bestehenden vollständigen
Satz zu bestellen und erst am Druck weiterzuarbeiten. **Keine weitere
Geometriekorrektur am laufenden Druckstand, keine erneute Couponrunde und
keine neue Materialberatung ohne konkreten Anlass.** Auf reale Rückmeldung
warten; Auftragseingang, Versand und Lieferung sind hier noch nicht belegt.

## Verbindlicher Stand des Testdrucks

- Repository `sscheidl/Daisy-Seed`, Druckbasis auf `main`:
  `cd02379e3be88a56088a2d896bb6a6da008e92eb`.
- PR #65, Korrektur `1d0d41a`: zugängliche Rückblendenbefestigungen und
  entfernte 0,1-mm-Stege zwischen den eng gepaarten CV-Buchsentaschen.
- PR #66, `7f9945e`: separates Spar-Anfragepaket, **bytegleiche Geometrie**.
- Vier Druckteile: `tray.stl`, `carrier.stl`, `rear-panel.stl`,
  `seed-service-cover.stl`. Unteres/oberes Acryl und Papier sind keine
  zusätzlichen FDM-Druckteile.
- Vollständiges Paket: [full/V4-KOMPLETTDRUCK.zip](full/V4-KOMPLETTDRUCK.zip).
  SHA-256: `149744844e458cf70defb956b921fbe456a8cf2aa84202ca4ef05e46d20a13c9`.
- Spar-Anfrage: [fit-print/V4-PASSPROBE-SPARPROFIL.zip](fit-print/V4-PASSPROBE-SPARPROFIL.zip).
  SHA-256: `37e77416012026e7782121c7d9d67f9be9bf878070a1c2150890f01e927d0b43`.

Die ZIPs und STLs dieser Basis erhalten. Die nächste Konstruktion separat
versionieren; einen bereits beauftragten Stand nicht stillschweigend ersetzen.
Das ursprüngliche Paket aus `b904a7e` enthält noch verdeckte Rückblenden-
befestigungen und ist **nicht** die aktuelle Druckbasis.

## Material-/Farbversuch des Nutzers

Die frühere Spar-Empfehlung PLA / 0,20 mm ist **nicht** die zuletzt gezeigte
Auswahl. Da die Preise ähnlich waren, wählt der Nutzer einen Materialmix:

| Teil | Gezeigte Auswahl | Einzelpreis im Warenkorb |
| --- | --- | --- |
| Wanne | PETG grau | 12,62 EUR |
| Träger | ASA weiß | 13,96 EUR |
| Rückblende | PETG rot | 5,10 EUR |
| Seed-Serviceverschluss | PETG rot | 1,00 EUR |

Quelle: Nutzer-Screenshots vom 27.10.2026, keine Rechnung. Summe 32,68 EUR;
zusätzliche Kosten und Zahlung nicht belegt. In allen vier Positionen sind
FDM, Maßstab ×1 und 20 % Füllung zu sehen. **Schichthöhe und Wandzahl sind
aus diesen Screenshots nicht bekannt.** Keine 0,20 mm als bestellt eintragen.

Die Achsenmaße im Warenkorb legen eine Hochkant-Ausrichtung von Träger und
Rückblende nahe. Der Nutzer wurde darauf hingewiesen und überlässt die
Druckausrichtung ausdrücklich dem Dienstleister: „er macht das schon“.
Eine Rotation ist kein Nachweis einer Spiegelung. Tatsächliche Drucklage erst
an Fertigungsangaben oder gelieferten Teilen beurteilen.

Die unterschiedlichen Teile geben einen Eindruck von Farbe/Oberfläche und
Handhabung. Daraus keinen kontrollierten Werkstoffvergleich oder pauschalen
Nachweis von Kratzfestigkeit ableiten. Endmaterial und Farbe der Folgeversion
sind offen; ASA bleibt ausdrücklich eine Option.

## Nutzerbeobachtungen und noch fehlende Maße

**GEMESSEN / beobachtet, qualitativ, Nutzerbericht 27.10.2026:** Die gelieferten
Kappen sitzen nach dem Aufstecken fest und fallen nicht wieder ab. Eine
zusätzliche Auszugssicherung ist nicht gefordert. Die Aufnahme ist nach
Nutzereinschätzung zu kurz, um die Kappen zuverlässig höher aufzustecken.

Keine quantitative Messung von Auszugskraft, Steckhöhe, Außenmaß oder Hub.
Exakte Kappenvariante (1SBLK eckig / 1RLGR rund) bisher nicht bestätigt.
Das Gerät wird überwiegend über MIDI gespielt; gelegentliche Tasterbedienung
muss trotzdem zuverlässig möglich bleiben. Der Nutzer bietet reale Messungen
an. Maßdaten nie aus Erwartungswerten als GEMESSEN übernehmen.

Zuerst tatsächlich benötigte Maße erfragen, Bezug jeweils Oberseite der großen
Haupt-PCB. Offen: Kappenoberkante ungedrückt/gedrückt und Außenmaß, Seed-Sitzhöhe,
reales HYCW-USB-Gehäuse und Auflagehöhe der Kippschalter. Nominal bisher:
unteres Acryl unten +9,0; oberes Acryl oben +11,15; Kappen oben +13,0 mm.

## Bekannte Restfehler, vor dem Druck bewusst nicht mehr geändert

1. **Sehr dünner Reststeg an der Wanne zwischen MIDI und Radöffnung.**
   Nutzerbild und hochgeladene `plate-1-tray.3mf` untersucht; Datei entsprach
   dem aktuellen Paket. Letzte MIDI-Öffnung endet bei PCB-Y 79,8; Radnische
   beginnt bei Y80. Der 0,2-mm-Streifen wird durch die Fase und die PCB-
   Freistellung zu einem praktisch unbrauchbaren dreieckigen Fähnchen.
   Digitale lokale Schnittfläche nahe PCB-Z0 etwa 0,000034 mm²; keine reale
   Messung. Topologisch ist das Netz noch eine Komponente. Das zeigt:
   **„geschlossen/einteilig“ beweist keine druckbare oder tragfähige Verbindung.**
2. Zwei weitere schmale MIDI-Trennstege: nominal je 1 mm, Y31,8–32,8 und
   Y55,8–56,8. Im Druck auf Tragfähigkeit/Abbruch und saubere Öffnungen prüfen.
3. Zusätzlich bei der Analyse aufgefallen, noch nicht korrigiert: obere
   Randstreifen links über microSD nominal 0,1 mm (6,6−6,5) und über J7
   nominal 0,6 mm (6,6−6,0). Beim gelieferten Druck gezielt ansehen.

Der Nutzer akzeptiert den aktuellen Probedruck trotz des ersten Befunds:
„ist egal – ich ordere das vorgesehene komplette Teil, dann sehen wir weiter“.
Diese Restfehler nicht mit den **bereits entfernten CV-Taschenstegen** verwechseln.
Für die Folgeversion Mindestquerschnitte, dünne Restflächen und Montagewege
gezielt prüfen; weitere bloße Intersektionszahlen reichen nicht aus.

## Nach Lieferung: Passprobe und Dokumentation

Den Nutzer zuerst um Erfahrungen/Bilder des Testdrucks bitten. Nicht behaupten,
dass der Druck bereits vorliegt oder eine Funktion bestanden hat. Mit wenigen
gezielten Rückfragen arbeiten; keine vollständige neue Messkampagne verlangen.

| Bereich | Festhalten |
| --- | --- |
| Druckqualität | Verzug, gebrochene Fähnchen, Stützreste, grobe Maßabweichungen; Material und tatsächliches Profil soweit bekannt |
| Grundmontage | PCB spannungsfrei, Metallpfostenhöhe, Schraubenlängen und Gewindeeinsätze, Nylon-Unterstützungen |
| Kappen | Voller Hub/Rückstellung, Überstand über Platten, seitliches Spiel, alle 16 Positionen |
| Anschlüsse | Audio-/USB-Abstützung, volle Stecktiefe, MIDI, CV, microSD und Werkzeugzugang |
| Rechte Seite | Kopfhörerstecker und echte flache Lautstärkeradscheibe bei geschlossenem Aufbau erreichbar |
| Seed | Deckel abnehmbar; beide Modul-Mikrotaster erreichbar; keine Drucklast auf Modul/Bauteile |
| Platten/Papier | Knurlies, Buchsenmuttern, wechselbare Beschriftung, Acrylzunge bei H3 |

Ergebnisse mit Datum, Methode und Quelle als GEMESSEN bzw. NICHT GEMESSEN
führen. Ohne echte Acrylplatten ist die komplette Platten-/Papier-Passung noch
nicht bestätigt; Ersatzschablonen belegen nur einen Teil davon. Temperatur,
Luftstrom und Anschlussentlastung unter Last sind bisher NICHT GEMESSEN.

## Gestalterischer Folgeauftrag

Nach der Funktionsauswertung zunächst zwei oder drei **grobe Formvarianten**
zeigen, dann mit dem Nutzer eine Richtung wählen und erst diese ausarbeiten:

- ruhige, zusammenhängende Gehäusesilhouette statt vieler einzelner Podeste;
- höhere Anschlüsse gestalterisch zusammenfassen, Übergänge etwa als Schulter
  oder leichte Schräge integrieren; flache Papier-/Tasterfläche berücksichtigen;
- bewusst einheitliche Kanten, Fugen und Befestigungen; zugängliche Werkzeuge
  und nachvollziehbare Montage trotz optischer Integration;
- wenige Druckteile und Standard-Metallpfosten, kein Rückfall zu 38 Coupons
  oder 16 separaten Hülsen; Lichtleiter bleiben optional und zunächst entfallen.

Die tatsächlichen Bauhöhen begrenzen eine durchgehend flache Oberfläche.
Keine Höhenlösung durch nur teilweise aufgesteckte Kappen voraussetzen.
Materialwahl allein löst die Höhensprünge nicht. Auch stimmige Papiergrafik und
Potiknöpfe sind später relevant, Hauptkritik bleibt jedoch die Gehäuseform.

Weiterhin verpflichtend: Schutz von PCB/Seed, zugängliche Bedienelemente und
Buchsen, zugentlastende Anschlussabstützung, zwei Seed-Taster zugänglich,
Papier zwischen zwei nominal 1-mm-Acrylplatten, schneller Wechsel mit Befaco
Knurlies. Endgültiges Acryl erst nach Festlegung der Folgekontur bestellen.

## Orientierung, Werkzeuge und Dateien

Physischer RH-Raum: **+X rechts/MIDI, +Y hinten/Audio, +Z oben**. Seed links.
Quelle hat PCB-Y zur Spielerseite; Export transformiert einmal `(x,-y,z)`
mit passender Flächenorientierung. Kein zusätzliches Spiegeln im Renderer
oder Slicer. Der frühere Spiegelungsfehler ist für den Nutzer besonders kritisch.

- [full/V4.md](full/V4.md): Montage, BOM und Grenzen des Druckstands.
- `v4-parameters.json`: nominaler Maßsatz mit Quellen; noch keine zahlenmäßigen
  Hardwaremesswerte aus diesem Chat eingetragen.
- `build_v4.py`, `validate_v4.py`: Generator und unabhängige Dateiprüfung.
  Letzter korrigierter Stand: **5.830** Intersektionsprüfungen, sieben geschlossene,
  außenorientierte und einteilige Netze, RH-Gegenproben und SVG-Landmarken.
- `verify_access_regression.py`: reproduziert alte verdeckte Schrauben aus
  `b904a7e`; neue Zugangsprüfungen und vier Landmarken verwerfen alte Geometrie.
- `package_v4.py`, `package_fit_print.py`: ZIP-/3MF-/Prüfsummenbindung.
- `render_v4.py`: aktuelle Ansichten aus echten STLs; große Elektronikgehäuse
  teilweise nominal vereinfacht. Lokale Herstellerdaten unter `build/case-reference/`.

Repository-Hauptordner:
`D:\Eigene Dateien\Eigene Dokumente\Playground\Daisy-Seed`.
Er wurde mit `main` aktualisiert, dabei lokale Hardware-Messergebnisse und zwei
unversionierte Granular-Dokumente erhalten. Aktiven Branch und `git status`
vor neuer Arbeit lesen, fremde Änderungen nicht überschreiben.

Gehäuse-Worktree:
`C:\Users\ssche\.codex\worktrees\case-v4-full\Daisy-Seed`.
Aktuelle lokale Blender-Szene:
`build/v4-full/V4-ZUGANG-KORRIGIERT-2026-10-27.blend`.
Die frühere `V4-MIT-LUEFTUNG-b904a7e.blend` ist veraltet. Blender-Szenen und
Hersteller-CAD liegen lokal; sie sind nicht Bestandteil der GitHub-Druckpakete.

Anbieter: **3ddruck-express.de** für FDM, **Lazycut** für Acryl-Laserzuschnitt.
Links und historische PreenFM2-Aufträge stehen in der [Gehäuse-README](../README.md#fertigungsanbieter).
Private Rechnungen nicht ins Repo kopieren.

AGENTS.md beachten: auf Branch arbeiten und per PR integrieren; nur passende
lokale Prüfungen, keine Actions manuell starten, keine Firmware-/Hardware-
Aktionen für diesen Gehäuseauftrag. Der Nutzer übernimmt die realen Messungen.
