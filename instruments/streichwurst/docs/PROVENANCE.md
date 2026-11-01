# Forschungsstand, Herkunft und Grenzen

2026-11-01. Übergabe selbst formulierter Ergebnisse aus dem getrennten privaten
Forschungsprojekt Custom-Firmware/Streichwurst. Der Nutzer hat die Vorbereitung
des neuen Instruments im bestehenden Daisy-Seed-Repository und seine GitHub-
Ablage ausdrücklich beauftragt. Dieses Repository ist öffentlich.

## Was hier enthalten ist

Sechs eigene Forschungs-/Entwurfstexte und ein eigenes mathematisches
Pythonmodell; keine Herstellerprogramme, Presets, Tabellen, Samples,
Dekompilate oder Disassemblierungen. Ein Snapshotkopf und relative Links
wurden für diese Übergabe angepasst. Original- und Zielhashes stehen in
[RESEARCH_MANIFEST.json](RESEARCH_MANIFEST.json). Im Pythonmodell wurde der
Import des Imageprüfers durch dessen eigene kleine Ausgabepfadprüfung ersetzt;
DSP und neun Tests bleiben unverändert. Seine funktionalen Grenzen sind dokumentiert.

Quellcheckpoint: `a7b81b61092b499eb4384905f1ebc4b9f62f27a2` im privaten
Forschungsrepository. Der Ressourcenbericht enthält zusätzlich die spätere
Nutzerpräzisierung zum Desktop-Board. Einzeldateihashes sind deshalb für die
genaue Zuordnung maßgeblich. Weitere Forschungsdateien werden nicht automatisch
importiert; der vollständige private Forschungsstand bleibt getrennt.

Untersuchte lokale Quelle: Streichfett-VST3, 4.180.992 Byte,
SHA-256 `ba3d175e63e8eae5a48a8c0d9382ad17dfd6f0a340eedf8fc69fba0555c89c2a`.
PE32+ AMD64; Ressourcen-Version 0.8.1, keine unabhängig bestätigte offizielle
Releasezuordnung. Die Nutzerquelle wurde nur statisch untersucht und blieb
unverändert. Keine Hersteller-Authentifizierung oder Weitergabelizenz behauptet.
Adressen in den Texten sind RVA, keine Laufzeitadressen und kein Firmwarecode.

## Lesereihenfolge und Beleggrade

1. [Audio-/Stimmenbefunde](research/audio-and-voices.md).
2. [Gezielte Vertiefung SF-F01..F08](research/focused-findings.md), die ältere
   OFFEN-Vermerke zu Envelope, Oktavtransformation, Balance und Bypass präzisiert.
3. [Ensemble](research/ensemble.md) und [Parameter](research/parameters.md).
4. [Eigener Entwurf 0.2](research/daisy-dsp-draft-0.2.md) und
   [Ressourcenannahmen](research/daisy-budget.md).

BESTÄTIGT bezeichnet den jeweils genannten statischen Prüfbereich. PLAUSIBEL
enthält verbleibende Interpretationen; OFFEN ist nicht rekonstruiert. Eigene
Entwurfsentscheidungen sind kein Originalbefund. Ein lokaler Zahlenabgleich
ersetzt keinen vollständigen Kontrollfluss-/Register-/Grenzfallbeweis.

Insbesondere sind 128 logische Notenpegel kein Versprechen von 128 unabhängigen
Oszillatoren oder einer bestimmten Daisy-Polyphonie. Modusgainwörter sind keine
fertige Gesamtlautheitsrekonstruktion. Die 128 untersuchten Envelope-Tabellen-
indizes belegen nicht die vollständige UI-Domain. Die komplette Ereignisamplitude,
Solo-Wellenbildung, Animate/Formung und Teile von Phaser/Reverb bleiben offen.

Historische Prüfung: 36 begrenzte Imageanker sowie 25 synthetische Tests über
zwei Checkpoints; Image-/Modellmetadaten lokal reproduziert. Diese Prüfungen
werden durch den Dokumentimport nicht erneut ausgeführt. Nur neun eigene
Modelltests werden hier als mitgelieferte Referenz erneut ausführbar.

## Vorhandene Hardware-/Toolchainbasis

[HARDWARE.md](../../../docs/HARDWARE.md), [BUILD.md](../../../docs/BUILD.md) und
[common/desktop.mk](../../../common/desktop.mk) sind die konkrete Daisy-Basis:

* libDaisy `f715e4875b2d28445d0c11a0a17f064a248f69d0`.
* DaisySP `2c72eaf9eac5fc0dca1919d65d606da907832618`.
* xPack GCC 13.2.1-1.1, GNU++17/M7/Hardfloat, BOOT_SRAM.
* Startkonfiguration 48 kHz/48 Frames, acht Potis, 16 Taster/LEDs,
  offizielle `desktop_devkit::Hardware`-Abstraktion und J7 USB MIDI.

Das sind die gepinnten Projektgrundlagen. Die konkrete Boardrevision und die
Streichwurst-Laufzeit-/Speichergrenzen sind noch nicht am Gerät gemessen.
Die früher nur als Studienannahmen bezeichnete Hardwareanbindung wird im
Implementierungsprojekt gegen diese bereits vorhandenen lokalen Quellen geprüft.

Codeherkunft und Notices vorhandener Bibliotheken erhalten. Kein Hersteller-
Quellcode wird unter der MIT-Lizenz des eigenen Projekts ausgegeben.
Künftige Befunde und Abweichungen in `FIDELITY.md` führen; unbekannte Werte
begründen statt aus Namen oder generischen Synthesizerrezepten als belegt ausgeben.

