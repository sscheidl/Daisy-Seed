# Echo Delay

The tape-like echo of the [Audrey II](https://github.com/Synthux-Academy/Audrey-II) as a component of its own: two independent
lines of up to 5 s, with a band-pass at 800 Hz and a soft clip in the feedback. Repeats get narrower, and feedback above 1
saturates instead of growing. Wet-only under FX contract 1.0, with its own firmware `echo-delay-fx`. Used by
[Audrey 3](../../instruments/audrey-3/).

| Status | |
| --- | --- |
| Version | Core and firmware 0.1.0 (`src/version.h`) |
| Software | Host and ARM checks, see CHANGELOG |
| Hardware | NOT MEASURED, [hardware test](docs/HARDWARE_TEST.md) |

## Contract

| Item | Value |
| --- | --- |
| Interface | `Prepare(Config, Storage&)`, `SetParameters`, `Process(StereoBlock)`, FX contract 1.0 |
| Format | 48 kHz only; any block size up to `max_block`; exact in-place use allowed |
| Memory | `StereoEcho::Storage`, about 1.9 MB of raw memory from the host; `Prepare` builds and clears the lines in it, so SDRAM must be up first |
| Output | Wet-only. The host owns dry/wet, routing and output protection |
| Latency | The delay time |
| Tail | Rings on with its feedback. No reset while running; above feedback 1 it does not end by itself |
| Stereo | Left and right never mix |

| Parameter | Range | Default | Note |
| --- | --- | --- | --- |
| `time_s` | 0.01–5 s | 5 | Glides with `lag_s`, so the pitch bends like tape |
| `feedback` | 0–1.5 | 0.5 | Not smoothed |
| `lag_s` | 0–10 s | 0.5 | Time constant of the glide; 0 jumps |

Values outside the range are clamped, and NaN keeps the previous value.

## Controls (firmware `echo-delay-fx`)

| Element | Function |
| --- | --- |
| VR1 | Time 50 ms – 5 s (logarithmic) |
| VR2 | Feedback 0–1.5 |
| VR3 | Dry/Wet |
| VR4 | Lag 10 ms – 2 s (logarithmic) |
| SW17 | Feed on/off, the tail rings on (D1 on while feeding) |
| S9 + S16 (5 s) | Bootloader, also by SysEx over USB |
| D16 | At least one audio callback overran its time budget |

Pots act when they move. There is no MIDI control. Dry/wet and feed fade over 10 ms (`common/audio/wet_host.h`).

## Build and test

```bash
make PROJECT=echo-delay-fx -j4
make PROJECT=echo-delay-fx test
make PROJECT=echo-delay-fx test-sanitize
```

`tests/test_echo_delay.cpp` also shows that the component's output is bit-identical to two upstream `EchoDelay` lines.

**Licence.** Our code is MIT. `third_party/audrey-ii/EchoDelay.h` is unchanged from Audrey II at `e5ab799`, MIT, see its
`LICENSE`. Its filters are in [`common/dsp/audrey-ii/`](../../common/dsp/audrey-ii/).
