# Cumulonimbus — Clouds granular FX

Component `cumulonimbus` 0.1.0, chain 0.1.0, meter 0.1.0, standalone diagnostic firmware 0.1.0.
Project IDs: `cumulonimbus-fx` (48 kHz grain kernel, image **F48**) and `cumulonimbus-fx-32`
(32 kHz grain kernel, image **F32**). The Toy Piano composition is
[Toy Piano on Cumulonimbus](../../instruments/toy-piano/docs/CUMULONIMBUS.md).

Status: host tests and sanitizer runs pass, both ARM images build. **Everything on the kit is
NOT MEASURED** (timing, listening, flashing). Stage 1 run sheet: [docs/STAGE1_RUN_SHEET.md](docs/STAGE1_RUN_SHEET.md).

The name stands for the Clouds granular path as cross-checked against its two Daisy ports, **Nimbus**
and **Cumuloid**. Provenance, licenses and the port comparison: [UPSTREAM.md](UPSTREAM.md).

## What it is

A stereo, wet-only effect under [FX contract 1.0](../../common/audio/fx_contract.h): a PCM16
recording ring and Clouds' granular player in Granular mode. The renderer
([src/clouds/grain.h](src/clouds/grain.h): reader, window, pan, accumulation) is separate from the
live-ring scheduling policy ([src/clouds/live_scheduler.h](src/clouds/live_scheduler.h)), so the
planned SD Grain Sampler can reuse the renderer with its own playhead. Gains, dry/wet, transitions,
output protection and controls belong to the host.

```text
input L/R ─► [48 kHz build] PCM16 ring ─► grains (2 × 24 frames) ─────────────► wet L/R
          └► [32 kHz build] 48→32 kHz ─► PCM16 ring ─► grains (1 × 32) ─► 32→48 kHz ─► wet L/R
```

Host chain shared by both firmwares ([src/chain.h](src/chain.h)): input → grains (dry/wet) →
one stereo [Torus reverb](../torus-reverb/) on that mix (dry/wet) → output guard (identity up to
|x| = 0.8, soft knee above, so a dry piano passes bit-exactly).

| Parameter | Range | Meaning (Clouds) |
| --- | --- | --- |
| `position` | 0..1 | POSITION: how far back in the 2 s ring a grain starts (0 = newest) |
| `size` | 0..1 | SIZE: 32 ms .. 512 ms, at either internal rate |
| `density` | 0..1 | DENSITY: below .47 regular grains, .47–.53 none, above .53 random; more towards the ends |
| `pitch` | ±24 st | grain playback pitch; pitch > 0 shortens grains as in Clouds |
| `texture` | 0..1 | TEXTURE: window shape (fixed .5 in the firmwares). The diffuser above .75 is not built |
| `stereo_spread` | 0..1 | random pan per grain |
| `freeze` | bool | holds the ring (256-sample crossfade on release); grains keep playing |
| cap (runtime) | 0..64 | grain pool bound; Clouds' 32 stereo / 40 mono stay the density reference |

Options: `channels` 2 (true stereo, standalone) or 1 (mono ring with Clouds' sin/cos pan law, Toy
Piano), `seed` for repeatable tests. One-shot `Trigger()` starts one grain in the next block, once.

**Freeze / Run / Bypass** (SW18 up / centre / down, `ChainMode`): Freeze holds the ring. Bypass fades
both feeds out over 10 ms, lets the tails ring for 2 s (the ring length), glides both wet mixes to 0
in 10 ms and then suspends the wet work: the output is the exact input. Leaving Bypass resumes with
feeds and mixes gliding from 0 and the ring refilling from silence.

## Two internal rates

| Build | Grain kernel | Calls per 48-frame block | Converter |
| --- | --- | --- | --- |
| `RATE=48` (default) | 48 kHz, sizes and smoother times scaled | 2 × 24 frames (Clouds ceiling 32) | none |
| `RATE=32` | 32 kHz, exactly the Clouds tables | 1 × 32 frames | [rate_converter.h](src/rate_converter.h): 72-tap Kaiser sinc at 96 kHz, 36 taps per 32 kHz sample down, 24 per 48 kHz sample up. Prototype response (computed): flat to 13 kHz, −0.3 dB at 14 kHz, −6 dB at 16 kHz, ≤ −70 dB from 19 kHz, so 16–19 kHz input folds into 13–16 kHz attenuated by 6–70 dB. Host test: 20 kHz round trip below −50 dB, 0.74 ms round-trip delay |

## Memory (ARM link map, 2026-11-09)

| Region | F48 | F32 | Contents |
| --- | --- | --- | --- |
| SDRAM `.sdram_bss` | 385 056 B | 257 056 B | stereo PCM16 rings (2 s) + crossfade tails |
| AXI SRAM `.axi_bss` (behind the image) | 94 368 B | 94 368 B | chain object (grain state, Clouds tables, converters) + Torus reverb 64 KiB |
| Image (`.bin`) | 135 292 B | 136 612 B | |

## Standalone diagnostic firmware (F48 / F32)

Line input L/R (a plug only in the left jack feeds both channels) or a built-in deterministic test
source → chain → output. No storage writes.

| Control | Function |
| --- | --- |
| VR1..VR8 | Position, Grain Length, Density, Grain Pitch (±24 st, centre detent), Grain Mix, Stereo Spread, Hall Decay, Hall Mix |
| SW18 | up Freeze, centre Run, down FX Bypass (polarity: board check) |
| S1..S7 | scenario 0..6: full, dry, grains only, reverb only, grain write only, grain render only, wet muted (LED shows the selection) |
| S9..S12 | grain cap 8 / 16 / 24 / 32 (boot: 16) |
| S14 | one-shot grain trigger |
| S15 | test source: external → impulses → tone bursts → noise (LED on = built-in source) |
| S8 LED | a callback overran its budget since the last reset |
| S9 + S16 (5 s) | bootloader, as every firmware here |
| USB-MIDI ch. 16 | CC20 scenario, CC21 cap, CC22 stimulus, CC119 = 1 telemetry / 2 reset ([probe](tools/cumulonimbus_probe.py)) |

Test source patterns: impulses (L at 0 ms, R at 250 ms, every 500 ms), tone bursts (50 ms of 440 Hz
left / 660 Hz right, alternating every 250 ms), independent L/R noise at −12 dBFS.

## Build and test

```bash
make PROJECT=cumulonimbus-fx test          # component + firmware-logic host tests, probe parser
make PROJECT=cumulonimbus-fx test-sanitize
make PROJECT=cumulonimbus-fx -j4           # F48
make PROJECT=cumulonimbus-fx-32 -j4        # F32
python scripts/check-firmware.py effects/cumulonimbus/build/fx48/CumulonimbusFx48-v0.1.0.bin
python effects/cumulonimbus/tools/cumulonimbus_probe.py selftest
```

The host tests cover the FX contract, genuine L/R routing, mono-to-stereo pan, freeze/unfreeze,
ring wrap and tiny rings, pitch bounds, quality change with occupancy, pool exhaustion and rejected
starts, one-shot triggers across sub-calls, grain durations at both rates, deterministic output for a
fixed seed, the rate converter (gain, alias rejection, delay), the chain (exact dry at mix 0 and in
suspended bypass, tail policy, freeze, stage isolation, output guard) and the meter (missed periods,
percentiles, telemetry encoding).
