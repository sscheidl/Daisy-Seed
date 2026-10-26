# Tape FX 0.1.0 – Hörtest-Images

Testfreigabe für den Hardware-Hörtest, keine abgenommene Version. Gebaut am
26.10.2026 aus Commit `bc0c7e9` (sauberer Arbeitsbaum, siehe `COMPONENTS-*.json`)
mit xPack GCC 13.2.1-1.1. Kein Image wurde auf einem Gerät getestet:
Klang, CPU-Last, Pegel und Schalterrichtung sind **NICHT GEMESSEN**.

| Datei | Inhalt |
| --- | --- |
| `TapeFx-v0.1.0.bin` | Stereo-Effekt `tape-fx` 0.1.0 (Line-Eingang → Tape FX → Ausgang), 125 264 B |
| `ToyPianoTape-v0.1.0.bin` | `toy-piano-tape` 0.1.0 (Toy Piano → Tape FX), 173 956 B |
| `SHA256SUMS` | Prüfsummen beider Images (`sha256sum -c SHA256SUMS`) |
| `COMPONENTS-*.json` | Komponenten-Manifeste (Versionen, Commit, Größe, SHA-256) |
| `Tape-FX-Bedienungsanleitung.pdf` | Bedienung, Erwartungen, Prüfablauf zum Ausfüllen |
| `Tape-FX-Cheatsheet.pdf` | Eine Seite: Belegung, Bereiche, Schnelltest |

Flashen manuell wie in [FLASHING.md](../../docs/FLASHING.md). Beide Images
bestehen `scripts/check-firmware.py`; die ELF-Prüfung (`scripts/check-tape-elf.py`)
lief auf den Build-Ergebnissen desselben Commits.

Neu bauen und vergleichen:

```bash
make PROJECT=tape-fx -j4
make PROJECT=toy-piano-tape -j4
sha256sum effects/tape-fx/build/TapeFx-v0.1.0.bin instruments/toy-piano/build/tape/ToyPianoTape-v0.1.0.bin
```

Die PDFs entstehen aus `effects/tape-fx/docs/print/` (`node render.js`, Playwright/Chromium).
Details: [Technischer Bericht](../../effects/tape-fx/docs/REPORT.md),
[Hörtest-Laufzettel](../../effects/tape-fx/docs/HARDWARE_TEST.md).
