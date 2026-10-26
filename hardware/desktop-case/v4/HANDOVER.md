# Gesicherter Stand – 26.10.2026

**Aktuell: vollständiger V4-Erstdruck.** Der Nutzer hat ausdrücklich entschieden,
wegen Bestell-/Abholaufwand direkt das ganze Gehäuse zu drucken. Diese Freigabe
ersetzt die ältere Stufe-2-Sperre in diesem Dokument. Keine Couponmessung als
Voraussetzung verlangen. Originalkappen aufstecken und seitlich führen reicht;
keine 16 Hülsen und keine Lichtleiter.

Branch `codex/case-v4`, Worktree `case-v4-full`, auf `codex/case-v4-coupons`
9372cc9 / PR #50 aufbauend. Alle neuen Geometrien in `full/`, Generator
`build_v4.py`, unabhängige Datei-/Kollisionsprüfung `validate_v4.py`,
Druckpaket/3MF-Prüfung `package_v4.py`. Maße/Quellen zentral in
`v4-parameters.json`, keine Hardwaremesswerte ergänzt. Vier Druckteile:
Wanne, gemeinsamer Träger, separate Audio-Rückblende, Seed-Serviceverschluss.
Zwei 1-mm-Acrylplatten und neutrale Papierkontur als SVG. Anleitung und BOM:
[full/V4.md](full/V4.md). Keine Firmwareänderung, keine Actions, kein Hardwarezugriff.

Lokale Prüfung: sieben tatsächliche STL-Dateien geschlossen, außenorientiert,
je eine Komponente; RH mit gespiegelten Gegenproben und SVG-Landmarken bestanden.
5.732 Intersektionsprüfungen ohne unerwartete Kollision, darunter
Lüftungsschlitze (Wanne links unter dem Seed, Dach der Seed-Haube; 2 mm,
offen für Ø1,6, sperrt den Ø2,5-IP3X-Prüfdraht; Temperatur NICHT GEMESSEN). Hersteller-KiCad- und
Seed-Modellboxen konservativ einbezogen. Bericht `full/validation.json` bindet
Parameter und Geometrien per SHA-256. `package_v4.py` prüft die drei 3MF-Platten
nach erneutem Einlesen aller Vertices/Transformationen und den ZIP-Inhalt.

Reale Passung, USB-HYCW-Anschlagflächen, Kappen-/Seed-Steckhöhe,
Schalterschulter und Muttern bleiben **NOMINAL / NICHT GEMESSEN**.
USB-Anschläge mit isolierenden Beilagen spannungsfrei anpassen;
Schaltermuttern ohne passende Auflage nicht anziehen. Audioblende axial über
die vier Buchsennasen montieren. Seed-Taster durch Abschrauben des Deckels
zugänglich. Nächster praktischer Schritt: Komplettdruck mit den Dateien aus
`full/V4-KOMPLETTDRUCK.zip`, dann nur tatsächlich beobachtete Werte dokumentieren.

## Historischer Stand – Coupons

**Aktualisiert 26.10.2026:** Der Nutzer möchte deutlich weniger Druckaufwand
und hat ausdrücklich „Stecken und seitlich führen reicht“ gewählt.
Eine zusätzliche Auszugssicherung ist nicht mehr gefordert. Keine 16 losen
Haltehülsen als Serienanforderung. Neuer Einstieg: `simple-guide/README.md`
und `build_simple_guide.py`, je Kappenform eine einzige Lochprobe mit drei
Spielmaßen. Der nachstehende Stand mit 38 Teilen bleibt historischer,
optionaler Prüfkatalog. Keine geänderten Hardwaremesswerte, keine Stufe2.

Neue Probe: beide STLs geschlossen, außenorientiert, je eine Komponente;
RH-Gegenproben und unabhängige PCB-Landmarken bestanden. 24 Prüfungen von
Kappe/Tastergehäuse im Ruhezustand, beim Drücken und Abziehen: kein
Schnittvolumen. Alte 38 RH-Dateiprüfungen als Regression bestanden.
`build/case-v4-print/C2-direct-S-EIN-TEIL.zip` bzw. `...-R-EIN-TEIL.zip`
enthält jeweils genau ein unverändertes STL, Anleitung und Bericht.
ZIPs sind lokal; `build_simple_guide.py` erzeugt sie reproduzierbar.

Branch `codex/case-v4-coupons`, eigener Worktree `case-v4-coupons`, Basis
`main` 5f25359 einschließlich PR #43. Alten Worktree nicht weiterverwenden.
Auf Nutzerwunsch wegen verbleibendem Tagesbudget gesichert, danach Stopp.

Stufe 1: 38 einzelne Coupon-STLs C1–C7, zentraler Maßsatz,
Generator, unabhängiger Dateiprüfer, Papierkontur C6 und Messanleitung.
Alle Hardwaremaße **NOMINAL / NICHT GEMESSEN**. Keine Firmware geändert,
keine Actions gestartet, kein Hardwarezugriff. Stufe 2 nicht begonnen.

Letzter lokaler Lauf:
`python hardware/desktop-case/v4/build_v4_coupons.py`:
38 geschlossene, außenorientierte, zusammenhängende STL-Dateien;
38/38 RH-Prüfungen samt gespiegelten Gegenproben bestanden;
354 Intersektionsprüfungen, keine unerwarteten Kollisionen.
22 getrennt klassifizierte positive Anschläge/Versuchsinterferenzen:
Flanschauszug, Kappen-Klemmzähne, PLP2-Presshülle, Gabelschlüssel und X200.
Auch die bisherigen vier V1/V2/V3-Orientierungsprüfungen bestanden.
Details und Grenzen in `coupons/validation.json`, Anleitung in `COUPONS.md`.

Bewusste offene Punkte:

- C2 sichert den Flansch digital. Sichere Haltekraft der Originalkappe
  durch elastische Zähne ist unbewiesen; kein bestätigter Hinterschnitt.
  Hülse unten offen, ab Z5, gegen konservatives Tastergehäuse geprüft.
- C3 X199 ist der bevorzugte Versuch. X200 kollidiert mit der Fingerkugel.
  H4 enthält freien Durchgang für Metallpfosten, keinen konkurrierenden
  massiven Druckpfosten. Echter Finger-/Stecker-/Werkzeugversuch offen.
- C4: tatsächlicher J7 HYCW ist nicht als GCT-USB4110 bestätigt.
  Anschlagflächen/SMD-Freiheit vorher messen. Seed-Halter vor Modul entfernen.
- C6: Nutzer vermutet, dass Lichtleiter entbehrlich sind. Erst O ohne
  Lichtleiter vergleichen; A Ø3-PMMA und B PLP2-250 bleiben optional.
  Nichts bestellt. Unterer Acrylabschluss sichert die eingelegten Teile;
  reale Luftspalte und Helligkeit müssen geprüft werden.
- Herstellerzeichnungen und zusätzliche Prüfmeshes liegen nur lokal unter
  `build/`; keine fremden CAD-Modelle ins öffentliche Repository übernommen.

Nächster Schritt ist die Nutzerprobe mit den leeren Formularen in
`COUPONS.md`, einschließlich S9+S16 an Toy Piano. Nur tatsächlich gemessene
Werte mit Datum/Methode in `v4-parameters.json` eintragen. Vollständiges
V4-Gehäuse erst nach weiterer ausdrücklicher Nutzerfreigabe.
