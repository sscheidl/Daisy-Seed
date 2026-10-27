# Changelog Drive

## 0.1.0 (2026-10-27), Phase 3

Erster Kern und erste Firmware `drive-fx`. Hardware NICHT GEMESSEN.

* **3a, eigener Commit:** Die MixForge-Kurven liegen jetzt in `common/dsp/saturation.h`
  (`seed::dsp`, Version 1.0.0). `tape::Saturate*` sind Wrapper, die Tape-FX-Versionen
  bleiben unverändert. Vorher und nachher, gleicher Compiler und gleiche Flags: dieselben
  Host-Testergebnisse von `tape-fx` und `toy-piano-tape` (inklusive des bitgenauen
  MixForge- und TapeVibe-Vergleichs), identische Ausgabe-Hashes aller vier Kurven,
  **bytegleiche** `TapeFx-v0.1.0.bin` und `ToyPianoTape-v0.1.0.bin`.
* **3b, Kern:** Kurven Soft, Hard, Tape und Fold mit bis zu +30 dB Drive, Tone 1–20 kHz,
  DC-Blocker, Level. Auto-Level in drei Stufen (README). Wet-only nach FX-Vertrag 1.0,
  im gemeinsamen Test `make test-fx`. Dazu `host.h` mit der 10-ms-Überblendung.
* **Firmware `drive-fx`:** Stereo-Line-Eingang, Bedienung nach README, MIDI-CC und SQ.
* **Golden-Hash** (FNV-1a, saw_lp2 durch alle vier Kurven, Drive 70 %): `0x50B9CF6E`.
* `common/dsp/fastmath.h` (Komponente `fast-math` 1.0.0) bringt deterministische
  exp2/log2/dB-Funktionen ohne libm mit.

Begründete Abweichungen vom Auftrag (4.7):

* **Feste Ausgleichskurve:** Sie hängt vom Pegel in die Kurve ab (Eingangs-RMS des
  300-ms-Fensters plus Drive-Verstärkung), nicht vom Drive-Regler allein. Mit einer Kurve
  über den Drive allein lag die 303-Fixture bis +10 dB neben dem Ziel. Sie ist rund 17 dB
  leiser als die Phase-0-Fixtures und sättigt deshalb viel weniger. Die ±3-dB-Nachregelung
  kann das nicht ausgleichen. Die Tabelle nimmt den Median über die Fixtures, weil
  Rechtecke in Fold einen Mittelwert verzerren.
* **Fold auf Rechtecken** erfüllt ±1 dB nicht (bis −17 dB, nie lauter). Das liegt am
  Falten flacher Dächer. Der Test prüft dort nur „nie lauter als das Ziel“, alle anderen
  Fälle ±1 dB.
* **Soft und Tape:** Ihr Eingang ist auf ±3 begrenzt. Dort erreichen die rationalen
  Kurven ±1 und steigen danach wieder an (Tape FX begrenzt stattdessen die
  Drive-Verstärkung).

Korrekturen und Entscheidungen nach der Codex-Fehlersuche (2026-10-27,
`docs/acid-seq/CODEX_FEHLERSUCHE.md`):

* **F10 Revision:** SQ-Parameter tragen ihre Revision bis zur Übernahme im Callback.
  Das Gerät meldet danach `applied` gleich `accepted`.
* **Q3 Ausgleich nach Eingangspegel:** vom Benutzer akzeptiert (Auftrag 4.7 angepasst).
* **Q4 Fold auf Rechtecken:** vom Benutzer als begrenzte Ausnahme akzeptiert. Die
  Dreiecksfaltung bildet flache Dächer bei bestimmten Drive-Werten genau auf 0 ab. Bei
  `square_lp3` und Drive 48 % bleiben nur die Flanken, −22 dB gegenüber dem Eingang
  (1-%-Raster, GEMESSEN 2026-10-27 am Host). ±1 dB hätte bis zu +24 dB Anhebung dieser
  Flankenreste verlangt. Ein Versuch mit Anhebung und Schutzfenster ließ bei einem
  Signalwechsel +7 bis +13 dB für 10–50 ms durch. Es bleibt deshalb bei „nie lauter als
  das Ziel“.

Der Golden-Hash `0x50B9CF6E` bleibt unverändert.
