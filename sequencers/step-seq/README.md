# Step Seq

16-Step-Sequencer (2 × 8) nach dem Vorbild KORG SQ-1 für das Seed3 Desktop Dev Kit:
CV/Gate, MIDI über USB und DIN, drei Abspielmodi an SW18, Start/Stop an SW17.
Eigene Firmware `step-seq` zum Testen, Kern des Produkts [Acid Seq](../acid-seq/).

| Stand | |
| --- | --- |
| Phase | 0 (Gerüst und Verträge), noch kein Kern und keine Firmware |
| Version | `src/version.h`, Komponente `step-seq` 0.0.0 |
| Hardware | NICHT GEMESSEN |

## Verträge

* [SQ-Protokoll](docs/PROTOCOL.md) (`F0 7D 53 51`), Referenz-Codec
  [`tools/sq_codec.py`](tools/sq_codec.py), Testvektoren
  [`contracts/sq_vectors.json`](contracts/sq_vectors.json)
* [Parametervertrag und CC-Plan](docs/PARAMETERS.md) für alle Komponenten,
  maschinenlesbar in [`contracts/parameters.json`](contracts/parameters.json)
* [Transport- und Ereignisvertrag](docs/TRANSPORT.md) mit Sollfolgen in
  [`contracts/transport_vectors.json`](contracts/transport_vectors.json)

## Tests

```bash
make -C sequencers/step-seq test
python3 -m unittest discover -s tests -p test_acid_seq_contracts.py -v
python3 sequencers/step-seq/tools/sq_codec.py --check
```

Plan und Regeln: [Arbeitsauftrag Acid Seq](../../docs/acid-seq/ARBEITSAUFTRAG.md).
