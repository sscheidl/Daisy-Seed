# Drei Formstudien für das Desktop-Gehäuse

Stand: 27.10.2026. **Formstudien auf nominaler CAD-Grundlage; Hardware
NICHT GEMESSEN.** Die Studien sind ein eigener Entwurfsstand. Die vier
V4-Druckteile und beide bestellten/anfragbaren ZIP-Pakete bleiben unverändert.

## Vergleich

![Drei Charaktere aus derselben Baugruppe](2026-10-27/comparison.png)

| Studie | Form und Charakter | Bewusster Kompromiss |
| --- | --- | --- |
| A · Studio | Graphit, helle Einlage, schmale farbige Zeiger. Eine durchgehende, gerade Schulter verbindet Rückseite, Seed-Bereich und MIDI-Flanke. Kurze schräge Übergänge. | Die tief liegende CV-Bucht bleibt als funktionale Aussparung erkennbar; keine durchgehend flache Oberseite. |
| B · Pebble | Salbeigrüne Hülle, warme Einlage. Breitere weiche Übergänge und eine quer gewölbte Krone lassen den oberen Körper zusammenhängend wirken. | Mehr Volumen zwischen den Funktionsbereichen; die Krone steigt nominal bis etwa +26,4 mm über die PCB. |
| C · Konsole | Dunkle Hülle, kühle Einlage, orange Akzente. Breite Fasen, nach hinten ansteigende Krone und facettierte vordere Wangen. | Der hintere Körper wird höher: Krone nominal bis +27,4 mm. Die feste Bedienebene selbst wird nicht geneigt. |

Alle drei sind monochrome Formideen mit einer ergänzenden Farbidee. Farben
und Oberflächen sind keine Materialbestellung oder Aussage über verfügbare
Filamentfarben. A und B unterscheiden sich durch die Übergangs- und
Kronengeometrie; C zusätzlich durch die geneigte Krone und Wangen.

## Tatsächliche Grundlage

- PCB-Kontur 197 × 111 mm, alle 8 Poti-, 16 Taster-/LED-, 6 CV/Gate-, 2
  Kippschalter- und 4 Pfostenpositionen aus dem vorhandenen Maßsatz
  [v4-parameters.json](../v4/v4-parameters.json).
- V4-Baugruppenszene `V4-ZUGANG-KORRIGIERT-2026-10-27.blend`, SHA-256
  `a471cdbda8f7d59b109c22fb05d82942b0790bac3421d5e98a2a6d464c177563`.
  Sie enthält die tatsächlichen V4-STLs, das aus KiCad exportierte Board
  und das Hersteller-Seed-Modell. Wie bisher sind mehrere große
  Buchsengehäuse und Befestigungsteile nominal vereinfacht.
- Derselbe physische RH-Raum: Seed links, MIDI rechts, Audio hinten.
  Die unveränderte Spielerseite liegt im Renderer bei negativem Y.
- Unteres Acryl unten +9,0, Papier +10,0 bis +10,15, oberes Acryl oben
  +11,15 mm relativ zur PCB. Kappen unverändert nominal +13,0 mm.
  Keine teilweise aufgesteckten Kappen und keine geänderten Tasterpositionen.
- Gemeinsame Grundfläche 203 × 128,6 mm; Boden bei −14,6 mm. Eine höhere
  Schulter spart daher keine Bauhöhe. A maximal nominal 39,0 mm, B etwa
  41,0 mm und C 42,0 mm über Boden, ohne herausragende Bediener.
- Die zusätzlichen Poti-Knöpfe sind **nur eine Größen-/Stilvorschau**:
  nominal Ø11 × 14 mm, kein bestätigtes Kaufteil. Die eckigen 5-mm-Kappen
  stammen aus der bestehenden Szene; die tatsächlich verwendete Kappenform
  ist noch nicht bestätigt.

## Was die Bilder zeigen und was noch konstruiert werden muss

Die Originalbaugruppe bleibt im Inneren erhalten. Darüber liegt eine neue
nominale Außenhaut mit zusammenhängender Schulter. Die bisherigen Hauben
werden optisch zusammengefasst; es gibt noch keine neue Fertigungsbaugruppe.
Portausschnitte, vier Eckzugänge, Seed-Serviceöffnung, Lüftung, Taster- und
Regleröffnungen sind in der neuen Haut tatsächlich ausgeschnitten.

Das obere transparente Acryl ist für die lesbare Formdarstellung ausgeblendet;
sein Objekt und die Höhenlage bleiben in der Szene erhalten. Die bisherigen
Plattenkonturen dienen als Positionsreferenz und müssen für die gewählte
neue Schulter zugeschnitten werden. Unter verdeckten Bereichen sind sie
noch nicht mechanisch auf die neue Hülle angepasst.

Die vier Knurlie-Positionen bleiben erhalten. Die tiefen hinteren Eckzugänge
sind ein **offener Konstruktionspunkt**, insbesondere Finger-/Werkzeugraum
und Abheben der neuen Papier-/Acrylkontur. Bei B/C ist auch der Übergang des
planen Serviceverschlusses zur gewölbten/geneigten Krone noch offen.
Die alte tragende Innenstruktur und Anschlussabstützung sind vorhanden;
ihre Vereinfachung, eine neue Teiletrennung, Schraubenzugänge, Mindeststege,
Wandstärken, Drucklagen und Stützentfernung sind für diese Studien noch nicht
nachgewiesen. Es werden bewusst **keine neuen Druck-STLs** ausgegeben.

Ein Rendering beweist keinen Montageweg und keine Belastbarkeit. Die
V4-Kollisionsprüfungen gelten für V4; sie sind kein Passnachweis für die
neue Außenhaut. Ergebnisse des realen Testdrucks liegen hier weiterhin
nicht vor. Der nächste Schritt ist die gemeinsame Wahl einer Formrichtung;
ihre mechanische Ausarbeitung berücksichtigt anschließend die echten
Druckbefunde.

## Erneute Referenzsuche, 27.10.2026

Gesucht mit `Seed3 Desktop case/enclosure`, `Seed3-DevKit-Desktop`,
`Daisy Desktop Dev Kit case`, sowie Einschränkungen auf Printables,
Thingiverse und das Daisy-Forum. **In dieser Suche kein nachweislich zum
Desktop Board passendes fremdes Gehäuse oder neues vollständiges
Desktop-Gehäuse-CAD gefunden.** Das ist keine Aussage, dass es keines gibt.

| Quelle | Befund und Nutzen |
| --- | --- |
| [Offizielles Desktop-Repository](https://github.com/daisyaudio/Seed3-DevKit-Desktop) | Weiterhin die belastbare Konstruktionsgrundlage mit KiCad, BOM und Anschlussanordnung; PCB 197 × 111 mm. Erneutes `git ls-remote … HEAD`: weiterhin `987297fb7c9a8c58721ef40abf6d6eedd1fcda2a`, also derselbe Herstellerstand wie unsere CAD-Basis. Kein fertiges Gehäuse im gelesenen Inhaltsverzeichnis. |
| [Offizielle Desktop-Dokumentation](https://docs.daisy.audio/product/Seed3-Desktop-Dev-Kit/) | Boardfoto, Pinout, Schaltplan, BOM und KiCad-Verweise. Bestätigt die Funktionsgruppen; kein neuer Gehäuseentwurf. |
| [PZD Daisy Pod Groovebox, Beitrag vom 26.10.2026](https://community.daisy.audio/t/pzd-daisy-pod-groovebox/9677/12) | Neu gezeigtes Gehäuse für **Pod mit Seed3 und 2,42-Zoll-OLED**. Als Gehäuse-/Integrationsreferenz interessant; andere Platine und andere Bedienung, daher nicht als Maßvorlage übernommen. |
| [Gus Vega · UMBRA](https://www.gusvega.com/) | Eigenständige physische Formstudie mit Seed3 und Raspberry Pi, 196,85 × 139,7 mm. Die Seite bezeichnet das interne Layout ausdrücklich als konzeptionell. Stilreferenz, kein Gehäuse für unser Desktop Board. |

Keine fremden Gehäusemodelle, Produktbilder oder Hersteller-CAD-Dateien
werden mit den Studien veröffentlicht. Eigene Ansichten sind gerendert,
keine KI-erzeugten Produktfotos mit erfundenen Anschlüssen.

## Ansichten und Reproduktion

- A: [Perspektive](2026-10-27/a-hero.png), [Rückseite](2026-10-27/a-rear.png), [Draufsicht](2026-10-27/a-top.png)
- B: [Perspektive](2026-10-27/b-hero.png), [Rückseite](2026-10-27/b-rear.png), [Draufsicht](2026-10-27/b-top.png)
- C: [Perspektive](2026-10-27/c-hero.png), [Rückseite](2026-10-27/c-rear.png), [Draufsicht](2026-10-27/c-top.png)

Blender 5.2.1, [render_studies.py](render_studies.py), lokale Quelldatei nötig:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe' --background `
  --python hardware/desktop-case/form-studies/render_studies.py -- `
  --assembly 'C:/Users/ssche/.codex/worktrees/case-v4-full/Daisy-Seed/build/v4-full/V4-ZUGANG-KORRIGIERT-2026-10-27.blend' `
  --output build/form-studies-2026-10-27
```

Lokale `.blend`-Szenen enthalten Herstellerobjekte und bleiben unter `build/`.
Die eingecheckten Bilder und Quellen stehen separat vom V4-Druckpaket.
