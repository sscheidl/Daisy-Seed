# Torus Reverb as an FX component

The versioned component `torus-reverb` provides the existing MIT-licensed
Torus reverb through the [shared audio contract](../../common/audio/fx_contract.h).
The DSP source stays in one place at the
[existing provenance path](../../instruments/physical-modeling/torus/UPSTREAM.md).
No manufacturer binaries or further DSP copies were taken over.

`seed::fx::TorusReverb` owns no board, MIDI or editor code.
`Prepare({48000, max_block}, storage)` binds the memory provided by the host
(32,768 uint16 words, 65,536 bytes). `SetParameters` takes the
normalized Torus controls Decay, Damping, Diffusion and Input Gain.
`Process(StereoBlock)` returns wet only; the host mixes Dry/Wet and limits the
output. Exact per-channel in-place processing is possible.

The original 48 kHz tuning and all setter expressions are preserved.
The host tests compare the result sample-exactly with the original
`amount=1` processing and check invalid blocks, tails and the audio path
for the absence of heap allocations. In the new Torus composition the
existing golden hash must also match.

```bash
make test-fx
make PROJECT=physical-modeling-fx-torus test
make PROJECT=physical-modeling-fx-torus -j4
```

This is a reusable FX core. A dedicated board firmware only for
Torus Reverb with external inputs is not yet a build target; the existing
standalone Ambient firmware remains the external FX test device.
