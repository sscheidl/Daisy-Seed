# Gesicherter Stand – 25.10.2026

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
