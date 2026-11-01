# Streichwurst

Eigenständiges String-/Solo-Instrument mit Ensemble und FX für das
**Electro-Smith Seed3 Desktop Development Kit**. Ziel ist eine möglichst
originalnahe Annäherung an den Waldorf Streichfett auf Grundlage lokaler,
statischer Pluginforschung und selbst entwickelter DSP-Daten und Algorithmen.

**Stand 01.11.2026: Firmwareprototyp 0.1.0** (Instrument 0.1.0, FX 0.1.0,
Control App 0.3.0). Host-, Sanitizer-, App- und ARM-Builds lokal bestanden
([Prüfstatus](docs/VALIDATION.md)). **Hardware, CPU-Last und Klangvergleich zum
Original: NICHT GEMESSEN** ([Laufzettel](docs/HARDWARE_TEST.md)). Abweichungen vom
Original je Modul: [FIDELITY.md](docs/FIDELITY.md).

## Modularer Aufbau

| Baustein | Ort, Komponenten-ID, Version | Inhalt |
| --- | --- | --- |
| Instrument | [include/streichwurst/](include/streichwurst/), `streichwurst-instrument`, `STREICHWURST_INSTRUMENT_VERSION` | Stringsection (zwölf Tonklassentakte, Teilerkette, Ereigniskern, Leckintegration, Hüllkurven, Registrierung, Oktaven) und acht Soli |
| FX | [effects/streichwurst-fx/](../../effects/streichwurst-fx/README.md), `streichwurst-fx`, `STREICHWURST_FX_VERSION` | Animate, Ensemble (3/5/3 Taps), Phaser im Summenarm, Reverb; Stringstufe und Globalstufe getrennt |
| MIDI/Control | [src/register.h](src/register.h), [src/controls.h](src/controls.h), SQ-Gerät aus `sequencers/step-seq`, Mod on Daisy | ein Parameterregister für Kit, MIDI CC, App und Matrix |
| Firmwareprodukt | [src/](src/), Build-ID `streichwurst`, `STREICHWURST_VERSION` | Audiograph ([product.h](src/product.h)), Speicher, Board, Queues, Bootloader-Einstieg |
| Control App | [editors/mod-on-daisy](../../editors/mod-on-daisy/README.md) 0.3.0 | Seiten Strings/Solo/Tastatur/FX/Global, Matrix am selben Port, `.swpatch` |

Signalfluss (Belege in [FIDELITY.md](docs/FIDELITY.md)): Strings → Animate-Formung →
Ensemble-Summe → Phaser → Stereo-Tapkorrektur; Soli umgehen die String-FX; Balance;
globaler Slot (Reverb oder jede andere Wet-Stufe des FX-Vertrags 1.0, z. B. der
Stereo-Delay); Ensemble-Gainwort × Master; Ausgangsschutz. Die Produktschicht ist
Build-Zeit-Komposition; einen Laufzeitlader gibt es nicht.

## Bauen und prüfen

```bash
make PROJECT=streichwurst                         # ARM-Image build/Streichwurst-v0.1.0.bin
make -C effects/streichwurst-fx test               # FX allein
make -C instruments/streichwurst test              # Instrument, Produkt, Firmware auf Hostfakes
make -C instruments/streichwurst test-sanitize
make -C instruments/streichwurst compare-rates     # 40 kHz + Resampler gegen 48 kHz
make -C instruments/streichwurst render            # eigene Testsignale nach build/render/*.wav
python scripts/check-firmware.py instruments/streichwurst/build/Streichwurst-v0.1.0.bin
make PROJECT=streichwurst manifest                 # Produktmanifest mit Komponentenversionen
```

Unter Windows vorher `. ./instruments/streichwurst/tools/enter-local.ps1` und
`HOST_CXX=clang++` verwenden. Flashen nur durch den Menschen ([FLASHING.md](../../docs/FLASHING.md)).

## Bedienung

Kit: [CONTROLS.md](docs/CONTROLS.md) (drei Reglerseiten, Soft takeover, Taster S1–S16,
S9+S16 5 s = Bootloader). MIDI: USB (J7) und DIN, Kanal einstellbar (Standard Omni),
CC-Plan und alle Parameter: [PARAMETERS.md](docs/PARAMETERS.md). Mod-Matrix: acht eigene
Ziele, 1 kHz. Control App: Verbinden liest zuerst SQ, dann die Matrix; „Matrix“/
„Klangparameter“ schaltet die Ansicht.

## User manual (English)

[Streichwurst-Manual.pdf](../../releases/streichwurst-0.1.0-test1/Streichwurst-Manual.pdf),
generated from [docs/print/](docs/print/) (`python build_manual.py && node render.js`;
the parameter tables come from `contracts/parameters.json`).

## Dokumente

[Arbeitsauftrag](docs/CLAUDE_AUFTRAG.md) · [Modulvertrag](docs/MODULAR_ARCHITECTURE.md) ·
[Herkunft](docs/PROVENANCE.md) · [Forschung](docs/research/) · [Klangtreue](docs/FIDELITY.md) ·
[Parameter](docs/PARAMETERS.md) · [Bedienung](docs/CONTROLS.md) ·
[Abtastrate](docs/RATE_COMPARISON.md) · [Prüfstatus](docs/VALIDATION.md) ·
[Hardwaretest](docs/HARDWARE_TEST.md) · [Änderungen](CHANGELOG.md)

Eigener Code unter der MIT-Lizenz des Repositorys. Herstellerprogramme, Originaldaten,
kopierte Tabellen und rohe Analyseausgaben gehören nicht hierher.
