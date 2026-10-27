# V4 – günstige mechanische Passprobe

Stand 27.10.2026. **Vier Druckteile mit unveränderter, korrigierter Geometrie.**
Dieses Sparpaket dient der Pass- und Montageprobe vor einer gestalterischen
Überarbeitung. Hardwaremaße weiterhin **NOMINAL / NICHT GEMESSEN**.

## Text für den Druckauftrag

> Bitte die vier STL-Dateien jeweils einmal als mechanischen Passprototyp
> fertigen. Gewünschtes Sparprofil: PLA in einer preisgünstigen vorhandenen
> Farbe, 0,20 mm Schichthöhe, drei Wände, 20 % Füllung, mindestens 0,8 mm
> geschlossene Deck- und Bodenschichten. Maßstab 100 %, Einheit mm.
> Keine kosmetische Nachbearbeitung, Lackierung oder Glättung. Erforderliche
> Stützen bitte ergänzen und vollständig entfernen; Passflächen und Bohrungen
> sauber entgraten. Keine automatische Größenänderung und keine pauschale
> Vergrößerung der Löcher. Bitte kalkulieren, ob dieses Profil gegenüber
> PETG-CF / 0,12 mm / 40 % Füllung tatsächlich günstiger ist. Falls 0,20 mm
> nicht angeboten wird, bitte das günstigste geeignete Standardprofil nennen.

Die 0,8 mm sind eine Dicke: bei 0,20 mm mindestens vier geschlossene Schichten
je Ober-/Unterseite. Dies ist eine Fertigungsanweisung. Die 3MF-Dateien
enthalten keine Druckerprofile, erzeugten Stützen oder G-Code.

| Datei | Anzahl | Drucklage |
| --- | --- | --- |
| `print/tray.stl` | 1 | Boden unten |
| `print/carrier.stl` | 1 | Wie STL, Stützen von unten erforderlich |
| `print/rear-panel.stl` | 1 | X −90°, äußere Rückseite unten |
| `print/seed-service-cover.stl` | 1 | Flach |

Alternativ enthalten drei 3MF-Platten dieselben vier Teile bereits in Drucklage.
**STL oder 3MF verwenden, nicht beide Sätze bestellen.** Der Online-Konfigurator
von 3ddruck-express.de weist STL/OBJ als Upload aus; dafür die vier STLs nutzen.

## Was wird eingespart?

Material-/Druckprofil und kosmetische Arbeit; große Renderbilder fehlen im ZIP.
Am Modell gibt es praktisch keinen Zierrat: die winzigen Orientierungsmarken
zu entfernen wäre kein sinnvoller Sparhebel. Befestigungen, Kappenführungen,
Anschlussanschläge, Hauben, Lüftung und deren Höhen gehören zur Passprobe.
Sie werden deshalb vollständig mitgedruckt. Auch die nötigen Stützen bleiben
Fertigungshilfen; sie lassen sich nicht einfach aus dem Auftrag streichen.

Das PLA-Sparprofil prüft die Passung dieses Drucks, nicht Festigkeit oder
Temperaturverhalten der späteren Ausführung. Material-/Profil-/Formänderungen
können die Passung erneut beeinflussen. Gewindeeinsätze und deren Montage
sind auch im Probedruck noch praktisch zu prüfen.

Die SVGs sind Referenzen; **die endgültige Acrylbestellung zunächst zurückstellen**,
weil sich die Außenkontur ändern kann. Ohne zwei echte 1-mm-Platten und Papier
ist die komplette Acryl-/Knurlie-Passung noch nicht bestätigt. Eine provisorische
Schablone hilft bei Lage/Überstand, ersetzt aber keine Acryl-Passprobe.

Metallpfosten, Schrauben, Muttern und Einsätze werden weiterhin benötigt.
Montage/BOM: [V4.md](../full/V4.md). Im ZIP liegt diese Anleitung als
`MONTAGE_V4.md`; deren älteres Fertigungsprofil wird für diesen Auftrag durch
`DRUCKAUFTRAG.md` ersetzt. Die Rückblendenmuttern am abgenommenen Träger von
innen einsetzen; Schaltermuttern nur bei passender Auflagehöhe anziehen.

## Prüfung und Kosten

Die STLs sind bytegleich zum aktuellen korrigierten V4-Stand. Das Paket
übernimmt die vorhandenen digitalen Prüfungen, behauptet aber keine neuen
Ergebnisse für das geänderte Druckprofil. `package_fit_print.py` prüft Quell-
und Datei-Prüfsummen und sämtliche ZIP-Einträge.

Preis und Druckzeit ohne Slicing/Angebot **NICHT GEMESSEN**. Größere Schichthöhe
kann Zeit sparen; auch Perimeter, geschlossene Schichten, Stützen und Material
wirken mit. 20 % Füllung sind nicht 20 % Gesamtmaterial und bedeuten keine
Halbierung der Kosten gegenüber 40 %.

Quellen, gelesen am 27.10.2026:

- [Prusa: Schichten und Perimeter](https://help.prusa3d.com/article/layers-and-perimeters_1748)
- [Prusa: Füllung](https://help.prusa3d.com/article/infill_42)
- [3ddruck-express.de: Konfigurator](https://3ddruck-express.de/angebots-konfigurator/)
  (PLA und 20 % Füllung wählbar; konkretes 0,20-mm-Profil gegebenenfalls anfragen)

Erzeugen: `python hardware/desktop-case/v4/package_fit_print.py`.
Ergebnis: `V4-PASSPROBE-SPARPROFIL.zip` in diesem Ordner.
