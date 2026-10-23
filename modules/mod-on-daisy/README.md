# Mod on Daisy

Schlanke, universell einsetzbare **Modulationsmatrix** für Instrumente und Effekte auf
dem Seed3 Desktop Development Kit. Das Modul ist eigenständig, wird in vorhandene
Projekte eingebunden und ist nicht fest in eine Firmware eingebaut. Der grafische
Editor entsteht getrennt unter [`editors/mod-on-daisy/`](../../editors/mod-on-daisy/).

**Status: Phase 2 als Software umgesetzt.**

* Der header-only Kern, die Host-Tests (ASan/UBSan/TSan, Fuzzing) und die Messfirmware
  `ModBench` sind fertig, ebenso das Testskript `tools/mod_probe.py` und CI.
* Messungen und Funktionstests auf dem Desktop-Kit stehen aus
  ([docs/VALIDATION.md](docs/VALIDATION.md), [bench/README.md](bench/README.md)).
* **Integriert:**
  * [Toy Piano v0.3](../../instruments/toy-piano/README.md#modulation-v03) als
    Entwicklungsumgebung;
  * eine getrennte Fassung von
    [Physical Modeling (Rings/Torus)](../../instruments/physical-modeling/docs/MODULATION.md)
    mit Poly AT pro Stimme.
* Beide sind mit leerer Matrix bitgleich zum vorherigen Klang (Golden-Tests). Die
  reguläre Physical-Modeling-Firmware ist bytegleich geblieben.

## v0.1 auf einen Blick

| | |
| --- | --- |
| Matrix | 8×8-Raster, Tiefe −100…+100 je Kreuzungspunkt, höchstens 8 aktive Verbindungen |
| Quellen | LFO 1, LFO 2, Random/S&H, CV 1, CV 2, Modwheel (CC1), Channel AT, Poly AT (pro Stimme; auf globalen Zielen die zuletzt gespielte Note) |
| LFO | Sinus, Dreieck, Sägezahn steigend/fallend, Rechteck, S&H; Sync frei / MIDI-Clock / Gate 1 oder 2 |
| Ziele | 8 instrumentspezifische Spalten; nicht unterstützte Zellen ausgegraut |
| Rate | Control-Rate 1 kHz (bzw. einmal pro Audio-Block), **keine Audio-Rate** |
| Bedienung | Editor (JUCE, Windows) über USB-MIDI/SysEx `0x7D` |
| Ressourcen | 1 936 B RAM (gemessen, ARM-Build), CPU < 0,3 % (Schätzung, Messung ausstehend) |
| Speicherung | QSPI spezifiziert, noch nicht implementiert |
| Pilot | Toy Piano (technisch), dann Rings mit Poly AT, später Nebulae |

## Dokumente

| Dokument | Inhalt |
| --- | --- |
| [docs/DECISIONS.md](docs/DECISIONS.md) | Entscheidungen v0.1, **technische Einwände**, Architekturänderungen, offene Punkte |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Modulstruktur, Datenstrukturen, Verarbeitung, globale und stimmenbezogene Modulation, Instrumentenschnittstelle, Abschätzung |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | MIDI-Protokoll v1 |
| [docs/STORAGE.md](docs/STORAGE.md) | QSPI-Aufteilung, Schreib- und Löschverfahren, Datenintegrität |
| [docs/TEST_PLAN_PHASE2.md](docs/TEST_PLAN_PHASE2.md) | Host-Tests, Messfirmware, Szenarien, Hardwaretests, Abnahme |
| [docs/VALIDATION.md](docs/VALIDATION.md) | Ergebnisse Phase 2 |
| [bench/README.md](bench/README.md) | Messfirmware: Bauen, Bedienung, Messablauf |
| [../../editors/mod-on-daisy/docs/GUI.md](../../editors/mod-on-daisy/docs/GUI.md) | Raster-GUI und Editor-Architektur |

## Bauen und testen

```bash
make -C modules/mod-on-daisy test          # Host-Tests (auch: test-sanitize, test-thread)
make PROJECT=mod-bench -j4                 # Messfirmware (Toolchain wie in docs/BUILD.md)
python3 modules/mod-on-daisy/tools/mod_probe.py selftest
```

Einbindung in ein Instrument: [docs/ARCHITECTURE.md § 6](docs/ARCHITECTURE.md#6-instrumentenschnittstelle).

## Leitgedanke

Die Hardware bleibt unmittelbar spielbar. Modulationszuordnungen gehören in die
grafische Oberfläche, nicht in Hardware-Menüs.
