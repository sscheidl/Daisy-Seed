# Tape FX

Wiederverwendbare Stereo-Bandmaschinen-Komponente (`tape-fx` 0.1.0), portiert aus
den drumlogue-Master-Effekten **TapeVibe** (Kern, klangliche Referenz) und
**MixForge** (zuschaltbare Zusatzstufen) in `sscheidl/drumlogue_fx_units`. Dazu
zwei Buildziele zum Anhören:

| Projekt | Inhalt | Status |
| --- | --- | --- |
| `tape-fx` | Stereo-Line-Eingang → Tape FX → Ausgang | Host-/Sanitizer-/ARM-Prüfungen bestanden, Hardware NICHT GEMESSEN |
| `toy-piano-tape` | Toy Piano → Tape FX → Ausgang ([Beschreibung](../../instruments/toy-piano/docs/TAPE.md)) | Host-/Sanitizer-/ARM-Prüfungen bestanden, Hardware NICHT GEMESSEN |

Funktionen: Wow, Flutter, Drive mit dynamischer Bandkompression, Head Bump,
Tone/Age, Hiss und Dropouts in drei Charakteren (Studio, Tape, Cassette) – alles
TapeVibe. Optional aus MixForge: weitere Drive-Kurven (TAPE mit Pre-/De-Emphasis,
HARD, FOLD), stereo-gekoppelter Kompressor und Bitcrusher.

## API

```cpp
#include "effects/tape-fx/src/tape_fx.h"    // DSP, nur Wet (common/audio/fx_contract.h)
#include "effects/tape-fx/src/tape_mixer.h" // Host: Dry/Wet, Bypass, Ausgangsschutz

static DSY_SDRAM_BSS seed::fx::TapeFx::Storage memory; // 16 KiB, Region wählt der Host
seed::fx::TapeFx tape;
seed::fx::TapeMixer mixer;

seed::fx::tape::Parameters p;   // Standard = TapeVibe-Initialwerte, MixForge aus
tape.SetParameters(p);          // jederzeit; Werte gleiten 15 ms
tape.Prepare({48000, 48}, memory); // nur beim Start; nur 48 kHz
mixer.Reset(0.4f, false);

tape.Process({in_l, in_r, out_l, out_r, n}); // Wet; In-place je Kanal erlaubt
mixer.Process(in_l, in_r, out_l, out_r, n);  // out = Mix aus Dry und Wet, geschützt
tape.Reset();                                 // Zustand und Rauschfolge wie TapeVibe
```

`Process` ist allokations-, sperr- und I/O-frei und begrenzt. Parameter sind
normalisiert (`drive`, `tone`, `age`, `wow`, `flutter`, `hiss`, `dropout` 0..1;
`Parameters::FromPercent(n)` ist die exakte drumlogue-Umrechnung). Ungültige Werte
werden begrenzt, NaN/Inf durch Standardwerte ersetzt.

## Build und Tests

```bash
make PROJECT=tape-fx test            # DSP-Hosttests + Firmware-Bedienung
make PROJECT=tape-fx test-sanitize   # ASan/UBSan
make PROJECT=tape-fx test-thread     # TSan
make PROJECT=tape-fx -j4             # ARM-Image build/TapeFx-v0.1.0.bin
make PROJECT=tape-fx manifest
make -C effects/tape-fx bench        # Host-Zeitmessung (nur relativ)
make test-fx                         # gemeinsamer FX-Vertrag, alle drei Adapter
python3 scripts/check-tape-elf.py effects/tape-fx/build/TapeFx-v0.1.0.elf
```

Die Hosttests vergleichen den Referenzpfad bitgenau mit dem unveränderten
drumlogue-Code in `tests/reference/` ([Herkunft](tests/reference/UPSTREAM.md)) und
messen Wow-/Flutter-Raten und -Tiefen, Drive-Kennlinien, Hiss-Pegel,
Dropout-Raten, Stereo-Trennung, Bypass, Parameterwechsel, Reset und 10 Minuten
Laufzeit bei Extremwerten.

* [Technischer Bericht](docs/REPORT.md): Architektur, Abweichungen, Optimierungen,
  CPU- und Speicherbedarf.
* [Hardware-Hörtest](docs/HARDWARE_TEST.md): Bedienung und Prüfablauf beider Images.

Stardust (`instruments/looper/`) ist nicht verändert. Die Komponente ist für eine
spätere Integration dort vorbereitet (16 KiB Delay-Speicher im SDRAM-Rest).
