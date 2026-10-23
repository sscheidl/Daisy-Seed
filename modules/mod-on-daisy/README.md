# Mod on Daisy

Schlanke, universell einsetzbare **Modulationsmatrix** für Instrumente und Effekte auf
dem Seed3 Desktop Development Kit. Das Modul ist eigenständig, wird in vorhandene
Projekte eingebunden und ist nicht fest in eine Firmware eingebaut. Der grafische
Editor entsteht getrennt unter [`editors/mod-on-daisy/`](../../editors/mod-on-daisy/).

**Status: Phase 1 — Architektur und technische Spezifikation v0.1.** Es gibt noch keinen
Code und keine Firmware. Die vorhandenen Instrumente sind unverändert.

## v0.1 auf einen Blick

| | |
| --- | --- |
| Matrix | 8×8-Raster, Tiefe −100…+100 je Kreuzungspunkt, höchstens 8 aktive Verbindungen |
| Quellen | LFO 1, LFO 2, Random/S&H, CV 1, CV 2, Modwheel (CC1), Channel AT, Poly AT (stimmenbezogen) |
| Ziele | 8 instrumentspezifische Spalten; nicht unterstützte Zellen ausgegraut |
| Rate | Control-Rate 1 kHz (bzw. einmal pro Audio-Block), **keine Audio-Rate** |
| Bedienung | Editor (JUCE, Windows) über USB-MIDI/SysEx `0x7D` |
| Ressourcen (Schätzung) | ≈ 1,2 KiB RAM, < 0,3 % CPU bei 8 Verbindungen |
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
| [../../editors/mod-on-daisy/docs/GUI.md](../../editors/mod-on-daisy/docs/GUI.md) | Raster-GUI und Editor-Architektur |

## Leitgedanke

Die Hardware bleibt unmittelbar spielbar. Modulationszuordnungen gehören in die
grafische Oberfläche, nicht in Hardware-Menüs.
