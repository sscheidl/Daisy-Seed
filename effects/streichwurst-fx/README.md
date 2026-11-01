# Streichwurst FX

Standalone FX building block (`streichwurst-fx`, version `STREICHWURST_FX_VERSION`
0.1.0, multi-bus contract `STREICHWURST_FX_CONTRACT_VERSION` 1.0.0) with the functions
of the Streichfett effects section: **Animate, Ensemble, Phaser and Reverb**. Our own code
and our own data; documented relationships and our own substitute methods are listed separately in
[FIDELITY.md](../../instruments/streichwurst/docs/FIDELITY.md).

**Status 2026-11-01:** implemented, host tests passed (including ASan/UBSan), integrated into the product
`streichwurst`; its own standalone FX firmware 0.1.0 built (below).
Hardware acceptance and listening comparison with the original NOT MEASURED.

## Contract ([include/streichwurst_fx/fx.h](include/streichwurst_fx/fx.h))

* `Prepare(FxConfig{fs, max_block}, FxStorage&)` once outside the callback; the host
  owns and places the memory (reverb lines, ≈32 KiB).
* **String stage** `ProcessStrings(in, out_l, out_r, gain, n)`: mono string sum →
  Animate shaping → Ensemble sum → Phaser → stereo tap correction (−21/128). `gain`
  returns the Ensemble gain word (off 1, mode 0/1/2 = 0.8125/0.625/0.75), which the host
  applies **after** solo mixing and reverb.
* `AnimateRegistrationOffset()`: typed control value for the instrument's
  registration (one block delay, 1 ms).
* **Global stage** `ProcessReverb(in_l, in_r, wet_l, wet_r, n)`: wet-only; `ReverbWet()`
  is the smoothed mix amount for the host.
* `Reset()`, `ClearTails()`; no allocation, no locks, no I/O; invalid blocks
  are rejected before any buffer access.

The building block owns no voices, no board controls and no MIDI transport.
[reverb_adapter.h](include/streichwurst_fx/reverb_adapter.h) provides the reverb as a
stage of the shared [FX contract 1.0](../../common/audio/fx_contract.h); conversely, the
product can use any other contract stage (e.g. the
Stereo Delay) in the global slot without changing the instrument or the string stage (product test).

## Testing

```bash
make -C effects/streichwurst-fx test           # FX alone and the firmware on host fakes
make -C effects/streichwurst-fx test-sanitize  # on Windows HOST_CXX=clang++
make PROJECT=streichwurst-fx                   # ARM image
python scripts/check-firmware.py effects/streichwurst-fx/build/StreichwurstFx-v0.1.0.bin
```

## Standalone FX firmware 0.1.0 ("Modular FX first")

`make PROJECT=streichwurst-fx` builds `StreichwurstFx-v0.1.0.bin` (version
`STREICHWURST_FX_FIRMWARE_VERSION` in [src/version.h](src/version.h), separate from the
component version). Stereo line input (left only = mono) → mono feed of the
string stage → global stage → host mix with gain word, input and output level and
output protection. Same FX code and same parameter contract as in the product (the
firmware uses the Streichwurst register only for the FX entries); it owns only
board mapping, routing and mixing.

* Pots (take effect only after being moved): VR1 Animate, VR2 Animate rate, VR3 Phaser,
  VR4 Phaser rate, VR5 Reverb, VR6 Reverb decay, VR7 input level, VR8 output level.
* Buttons: S1–S3 Ensemble mode 0/1/2, S12 Ensemble on/off, S16 clears line, reverb and
  filter states (on release, not in the S9+S16 bootloader chord).
* MIDI CC 102–109 on all channels (USB and DIN), SQ over USB with one component
  `streichwurst-fx`; the control app shows only the FX page.
* With Ensemble off and Animate/Phaser/Reverb at 0, the string stage passes the input
  through exactly (host fake test).

Acceptance on the kit: [docs/HARDWARE_TEST.md](docs/HARDWARE_TEST.md) (F1–F14, **NOT
MEASURED**). Only after that does the FX count as an accepted building block for Streichwurst.
