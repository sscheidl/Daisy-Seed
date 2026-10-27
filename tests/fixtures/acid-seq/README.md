# Referenz-Fixtures Acid Seq

Eingangssignale für die Pegel- und Impulstests von Drive und Delay (Phase 0, R11).
Mono, 48 kHz, 1 s, float32 little-endian (`.f32`). Die Dateien sind die Referenz.
[`generate.py`](generate.py) dokumentiert ihre Herstellung und muss sie auf 1e-6
genau reproduzieren. [`manifest.json`](manifest.json) enthält Version, SHA-256,
Spitze und RMS jeder Datei.

| Datei | Inhalt |
| --- | --- |
| `saw_lp1…3.f32` | bandbegrenzte Säge 93,75 Hz durch RBJ-Tiefpass 300 Hz, 1,2 kHz, 5 kHz (Q 0,707), Spitze −6 dBFS |
| `square_lp1…3.f32` | bandbegrenztes Rechteck, sonst wie oben |
| `saw_resonance.f32` | Säge durch RBJ-Tiefpass 1 kHz mit Q 8 (Resonanzspitze) |
| `impulse.f32` | Einheitsimpuls bei Sample 0 |
| `silence.f32` | digitale Stille |

93,75 Hz = 48 000 / 512: Eine Periode hat genau 512 Samples. Die 303-Fixture für
die Abnahme des Auto-Levels kommt versioniert aus Phase 2 dazu.

```bash
python3 tests/fixtures/acid-seq/generate.py --check
```
