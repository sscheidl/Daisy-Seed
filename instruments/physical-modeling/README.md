# Physical Modeling — Instrumentprojekt

**Stand: Projektgerüst / Entwurf, keine Firmware und kein Hardwaretest.**
Projekt-ID: `physical-modeling`. Ziel: ein eigenständig entwickeltes Instrument
für das Seed3 Desktop Dev Kit mit kontrollierter Anregung und einem Resonator.
Ein dauernd selbstmodulierendes Verhalten ist noch keine festgelegte Anforderung.

## Referenzen und Vergleich

* **Rings → Torus:** Original-Rings bietet mehrere Resonatormodelle; Torus ist ein
  vorhandener Daisy-Patch-Port. DSP und Board-Bedienung müssen getrennt betrachtet
  werden: Patch-Regler, Encoder und Audioanschlüsse passen nicht direkt zum Desktop-Kit.
* **Elements:** zweite Originalreferenz für die Trennung zwischen Anregung und
  resonierendem Körper. Eine vollständige Elements-Portierung ist nicht beschlossen.
* **DaisySP ModalVoice / Resonator:** bereits in unserer gepinnten Bibliothek;
  laut Quellkommentaren aus **Plaits**, keine vollständige Rings-/Elements-Engine.
* **Iridium Resonator:** zusätzliche Konzept-/Forschungsreferenz für Resonator-
  und Bedienungsentwurf. Kein Hersteller-DSP wird in dieses Projekt übernommen.

Commitfeste Links, Lizenz- und Vergleichsgrenzen: [Quellenübersicht](../../docs/REFERENCES.md).

## Eigener nächster Meilenstein

1. Original, Daisy-Port und vorhandene DaisySP-Bausteine anhand derselben Kategorien
   vergleichen: Anregung, Resonanzstruktur, Dämpfung, Pitch, Speicher und Blockverarbeitung.
2. Eine eigene monophone Minimalstimme mit Impuls-/Rauschanregung und begrenzter
   Modalbank implementieren; Pitch, Klangstruktur, Helligkeit und Ausklingzeit regeln.
3. Host-Tests für endliche Werte, Stabilität, Ausklang und Parametergrenzen;
   anschließend echten ARM-Build und manuelle Audioprüfung vorbereiten.

Polyphonie und Modulation folgen erst nach Messung der Laufzeit und des
Speicherverbrauchs am Seed3. Keine zugesagte Stimmenzahl oder Klanggleichheit.
Nutzbare gemeinsame DSP-Teile können später auch den
[Resonator-Effekt](../../effects/resonator/) versorgen.

Bis dahin weist `make PROJECT=physical-modeling` den Build ausdrücklich ab.
Bei der Implementierung: eigener `src/`-, `tests/`- und `docs/`-Bereich,
Makefile mit `../../common/desktop.mk`, Eintrag in `BUILDABLE_PROJECTS` und CI ergänzen.
