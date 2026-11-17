# SC Reverb

The Sean Costello reverb from DaisySP-LGPL (`ReverbSc`) as a component: stereo, eight delay lines, wet-only under FX contract
1.0, with its own firmware `sc-reverb-fx`. Used by [Audrey 3](../../instruments/audrey-3/), inside its feedback loop.

| Status | |
| --- | --- |
| Version | Core and firmware 0.1.1 (`src/version.h`) |
| Software | Host and ARM checks, see CHANGELOG |
| Hardware | NOT MEASURED, [hardware test](docs/HARDWARE_TEST.md) |

## Contract

| Item | Value |
| --- | --- |
| Interface | `Prepare(Config, Storage&)`, `SetParameters`, `Process(StereoBlock)`, FX contract 1.0 |
| Format | 48 kHz only; any block size up to `max_block`; exact in-place use allowed |
| Memory | `ScReverb::Storage`, about 396 KB of raw memory from the host; `Prepare` builds and clears the reverb in it, so SDRAM must be up first |
| Output | Wet-only. The host owns dry/wet, routing and output protection |
| Tail | Rings on by its decay. No reset while running |

| Parameter | Range | Default | Note |
| --- | --- | --- | --- |
| `feedback` | 0–1 | 0.85 | Decay, not smoothed |
| `lp_hz` | 100–20 000 Hz | 12 000 | Damping in the delay lines |

Values outside the range are clamped, and NaN keeps the previous value.

## Controls (firmware `sc-reverb-fx`)

| Element | Function |
| --- | --- |
| VR1 | Decay 0–1 |
| VR2 | Damping 100 Hz – 20 kHz (logarithmic) |
| VR3 | Dry/Wet |
| SW17 | Feed on/off, the tail rings on (D1 on while feeding) |
| S9 + S16 (5 s) | Bootloader, also by SysEx over USB |
| D16 | At least one audio callback overran its time budget |

Pots act when they move. There is no MIDI control. Dry/wet and feed fade over 10 ms (`common/audio/wet_host.h`).

## Build and test

```bash
git -C DaisySP submodule update --init DaisySP-LGPL
make PROJECT=sc-reverb-fx -j4
make PROJECT=sc-reverb-fx test
make PROJECT=sc-reverb-fx test-sanitize
```

`tests/test_sc_reverb.cpp` also shows that the component's output is bit-identical to `ReverbSc` called directly.

## Licence

The wrapper and firmware are MIT. `ReverbSc` is **LGPL-2.1** and stays in the `DaisySP/DaisySP-LGPL` submodule, pinned by the
DaisySP commit; nothing is copied here. The `Makefile` compiles `reverbsc.cpp`.

**Passing on a binary** brings LGPL-2.1 duties: the licence text, the library's source, and a way for the recipient to relink
the firmware with a changed library, for example the object files. Publishing a binary is the owner's decision.
