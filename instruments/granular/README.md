# Granular — Instrumentprojekt

**Stand: Projektgerüst / Entwurf, keine Firmware und kein Hardwaretest.**
Projekt-ID: `granular`. Ziel: eine eigenständige, begrenzte Granular-Engine für
das Seed3 Desktop Dev Kit, mit GR-1 als wichtigem musikalischem Bedienvorbild.

## Referenzen und Vergleich

* **GR-1:** vorhandene Hardware sowie getrennte Forschungsberichte zu Grainplanung,
  Sampleposition und Bedienung. Erkenntnisse sind Referenzen, keine eigene Engine.
* **Iridium Particle:** zweite Forschungsreferenz für Granularorganisation und
  Parameterrollen; keine Herstellerfunktionen oder Koeffizienten übernehmen.
* **DaisySP GranularPlayer:** bereits verfügbarer Ausgangspunkt für kontrollierte
  Versuche mit unabhängigem Tempo und Pitch. Kein vollständiges GR-1-Modell.
* **Clouds → Nimbus:** öffentliches Original und vorhandene Daisy-Patch-/Field-Ports.
  Die Audioeingangs-/Freeze-Ausrichtung ist besonders für die spätere Effektvariante interessant.
* **Beads:** als mögliche Bedeutung von „Beats“ aufgenommen. Öffentliches Original
  vorhanden; der Autor nennt fehlende Verifikation auf dem Modul. Keine zugesagte Daisy-Portierung.

Commitfeste Links und Grenzen: [Quellenübersicht](../../docs/REFERENCES.md).

## Eigener nächster Meilenstein

1. Kleinen synthetischen Samplepuffer und eine einzelne testbare Grain-Lesestimme
   mit eigenem Fenster, Position und Pitch erstellen.
2. Scheduler mit fest vorreserviertem Pool und definiertem Überlastverhalten ergänzen.
   Poolgröße und maximale Dichte erst nach Messungen festlegen.
3. Basisposition, effektive Scanposition und einzelne Grain-Lesepositionen getrennt
   führen. Spätere Ansichts-/Zoomzustände dürfen die musikalische Position nicht ersetzen.
4. Endliche Ausgabe, Buffergrenzen, Samplewechsel und CPU-/Speichergrenzen testen;
   danach manuellen Hardwaretest vorbereiten.

Dateizugriffe und Speicherallokation gehören nicht in den Audiocallback. Der M7
besitzt kein NEON; Leistungswerte der Referenzgeräte sind nicht übertragbar.
Eigene erzeugte Testdaten zuerst; Sampleverwaltung, Visualisierung und höhere
Polyphonie folgen in separaten Schritten. Eine Tulip-Anbindung ist hier nicht implementiert.

Der [Granular-Effekt](../../effects/granular/) soll später denselben nachweislich
gemeinsamen DSP-Kern mit einem Live-/Freeze-Eingang verwenden.
Bis zur Implementierung weist `make PROJECT=granular` den Build ausdrücklich ab.
