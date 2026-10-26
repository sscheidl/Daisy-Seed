# Einfacher Druckstart: ein Teil

**Nutzerentscheidung vom 26.10.2026:** Die Originalkappen bleiben auf den
Tastern gesteckt. Eine gemeinsame Platte führt sie seitlich; eine zusätzliche
Sicherung gegen Herausziehen wird nicht verlangt. Damit entfallen die
separaten Haltehülsen als Anforderung an das Gehäuse.

## Was drucken?

**Nur eine der beiden Dateien wählen:**

| Deine Kappen | Ein einziges Druckteil |
| --- | --- |
| Eckige E-Switch 1SBLK | `C2-direct-S.stl` |
| Runde E-Switch 1RLGR | `C2-direct-R.stl` |

Keine Hülse, keine zweite Druckplatte, kein PCB-Dummy. Für einen Vergleich
beider Kappenformen sind es insgesamt zwei Teile. Der bisherige Katalog mit
38 STL-Dateien ist eine Sammlung älterer Funktionsproben, kein Pflichtdruck.

Die Probe hat drei nebeneinanderliegende Öffnungen: von links nach rechts
0,1 / 0,2 / 0,3 mm Spiel je Seite, beim Rundteil radial. Die kleinen Punkte
neben der Kennung dienen der Dateiorientierungsprüfung, nicht der Passungswahl.
Die drei Öffnungen gehören zur selben Platte; keine zusätzlichen Einsätze.

## Drucken und ausprobieren

1. Flache Unterseite aufs Bett. PETG-CF schwarz, 0,12-mm-Schichten,
   40 % Infill, vier Perimeter, Maßstab 1:1, keine Lochkompensation.
   Keine Stützen. Nicht spiegeln. Die Kennungsfahne gehört zum Teil.
2. Zunächst eine echte Kappe ohne Kraft durch alle drei Öffnungen führen.
   Die kleinste Öffnung auswählen, durch die sie frei gleitet.
3. Danach am stromlosen Board auf seitliche Führung und freien Tastenhub
   prüfen. Die Probe stellt nominal eine Platte von PCB+9 bis PCB+10 dar;
   diese Höhe anhalten bzw. abstandsgerecht unterstützen. Nicht auf
   benachbarte Bauteile drücken oder die Platte als Tasteranschlag benutzen.
4. Kappe auf ihrem Taster belassen: drücken, loslassen, wiederholen.
   Sie muss von selbst vollständig zurückkehren. Die gemeinsame Platte
   hält die Kappe seitlich; sie darf weiterhin nach oben abgezogen werden.

Alle Maße **NOMINAL**, Hardware **NICHT GEMESSEN**. Montierte Kappenhöhe13
und Tasterhub0,25 sind weiterhin Annahmen. Die FDM-Platte hat LED-Freilöcher;
echtes transparentes Unteracryl darf dort geschlossen bleiben. Dies ist
eine Passprobe, keine optische oder mechanische Fertigungsfreigabe.

| Messung | GEMESSEN / Beobachtung | Datum |
| --- | --- | --- |
| Kappenform, tatsächliche Breite / Durchmesser | | |
| Gewählte Öffnung links / Mitte / rechts | | |
| Istmaß dieser Öffnung, Nacharbeit | | |
| Montierte Kappenhöhe und tatsächliche Plattenhöhe | | |
| Freier Hub, vollständige Rückkehr, keine Vorspannung | | |

## Vereinfachtes Ziel für das spätere Gehäuse

Eine Bodenwanne und ein gemeinsamer Druckträger schützen die Platine.
Eine separate Rückblende kommt nur hinzu, wenn die Buchsenmontage sie
erfordert. Dazu gehören die bereits gewünschten zwei Acrylplatten mit
Papier sowie die vorhandenen Kappen. **Keine 16 losen Haltehülsen.**

C5-Dummystapel und Lichtleiter-Vergleiche sind keine Vorbedingung für diese
erste Probe. Bolzen, Acryl und Papier lassen sich später mit den echten
Teilen vermessen. Lichtleiter erst wieder betrachten, falls die LEDs ohne
sie schlecht sichtbar sind. Buchsenstützung und Zugang zum Seed bleiben
Anforderungen; das vollständige Gehäuse folgt nach den realen Passmaßen.

## Dateien und Reproduktion

Im Repository liegen beide Modelle und Prüfberichte in `coupons/`.
Im jeweiligen ZIP liegt nur das gewählte Modell direkt neben dieser Anleitung.
Aus dem Repository-Root:

```powershell
.tools/case/Scripts/python.exe hardware/desktop-case/v4/build_simple_guide.py
```

Das erzeugt zusätzlich zwei ZIPs unter `build/case-v4-print/`, jedes mit
**genau einer STL**, dieser Anleitung und Prüfbericht. Beide Modelle nutzen
unverändert den zentralen Maßsatz `v4-parameters.json`. Geschlossenheit,
eine Komponente, RH-Orientierung samt gespiegelter Gegenprobe und der
Kappenfreiraum werden an den tatsächlichen STL-Dateien geprüft.
