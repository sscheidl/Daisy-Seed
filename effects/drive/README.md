# Drive

Sättigung mit den Kurven Soft, Hard, Tape und Fold aus Tape FX (gemeinsam in
[`common/dsp/saturation.h`](../../common/dsp/saturation.h)) und automatischer
Pegelanpassung um den Drive. Kern nach FX-Vertrag 1.0, eigene Firmware `drive-fx`.

| Stand | |
| --- | --- |
| Phase | 0 (Gerüst), Auslagern der Kurven und Kern folgen in Phase 3 |
| Version | `src/version.h`, Komponente `drive` 0.0.0 |
| Referenz-Fixtures | [`tests/fixtures/acid-seq/`](../../tests/fixtures/acid-seq/) |
| Hardware | NICHT GEMESSEN |

Plan und Regeln: [Arbeitsauftrag Acid Seq](../../docs/acid-seq/ARBEITSAUFTRAG.md) 4.7.
