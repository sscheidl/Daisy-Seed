# Cumulonimbus provenance and source audit

Audit date **2026-11-09**. "Cumulonimbus" is this repository's name for the Clouds granular path
as cross-checked against its two Daisy ports, **Nimbus** and **Cumuloid**. All DSP derives from
one lineage: the original Mutable Instruments Clouds source. Nimbus and Cumuloid served only as
references for the adaptation choices. None of their code was copied.

## Pinned sources

| Source | Revision | Role |
| --- | --- | --- |
| [pichenettes/eurorack `clouds/`](https://github.com/pichenettes/eurorack/tree/f4cfa7697095a28d292bff758beb51deb49b5a86/clouds) | `f4cfa7697095a28d292bff758beb51deb49b5a86` (already pinned in [docs/REFERENCES.md](../../docs/REFERENCES.md)) | **The DSP lineage.** Grain, grain player, audio buffer, granular-mode parameter mapping, table formulas |
| [DaisyExamples `patch/Nimbus`](https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Nimbus) | `259ed82c7d4c0d7699f695945e6e111b7dc7cb27` (already pinned) | Reference: Daisy block limit, rate handling, memory |
| [hfl1967/cumuloid](https://github.com/hfl1967/cumuloid/tree/610efa877dc9afa8c74220a7e6f69c88bc4b5a00) | `610efa877dc9afa8c74220a7e6f69c88bc4b5a00` (HEAD on 2026-11-09, committed 2026-10-17) | Reference: 32 kHz operation, SDRAM placement |
| stmlib (as vendored for Torus) | `5dc8b89b9d166de5f47ba91ee9d25c8805c2bd86`, [provenance](../../instruments/physical-modeling/torus/UPSTREAM.md) | Used only by the reused Torus reverb; the grain code has its own small helpers |

SHA-256 of the audited upstream files, fetched read-only from the pinned revisions:

| File | SHA-256 |
| --- | --- |
| `clouds/dsp/granular_sample_player.h` | `1cabcbe9d39b5d9eeeaf31d9c71783d9349bf63f0ec9e92167ee181ccd22a406` |
| `clouds/dsp/grain.h` | `08feb5ac5408e2cb30eb037a844f6ee187d8dedd53fee913d1b5ab2c74ebfac3` |
| `clouds/dsp/audio_buffer.h` | `110c63276ec84cbb211f98a5897d075307471da25ae1845da82dac504d052657` |
| `clouds/dsp/granular_processor.cc` | `b269442f850c344b4598424c7ccc3750f1ca090979d975af3af6e2e96ecd8d88` |
| `clouds/dsp/frame.h` | `203a758cd626f7d5ca0d3bd70457ba15b36d2ddc9fea5a2c56eab3cdd15ec3b4` |
| `clouds/resources/lookup_tables.py` | `8124b7eac0599080beb0a5e86a98e1bb7a0ac93702ab84f2010a6238a61cd83e` |
| Nimbus `Nimbus.cpp` | `8e8a9a7db2cbac59cd547b1743021203406a3af520836262359929e72c571ce4` |
| Nimbus `dsp/granular_sample_player.h` | `91af290410640d9a1acc395920ac7ce7f4afabbcae5494d131c97f3bc7a871d1` |
| Nimbus `dsp/grain.h` | `45c82b3a2d211eeae52249b9c7f39ac6859654e543cf762820b5b02dc1b3409d` |
| Nimbus `resources.cpp` | `8d11bbfdfbebac9f8cb42ec9d188c8d64433fb11ffd5f1e7ecd14892f808ca95` |
| Cumuloid `README.md` | `11aa19086bf6c437de5c10b75fb92f94b694f6fc28758c23c3600ebe23c002c0` |

## Licenses

Clouds is MIT (Copyright 2014 Emilie Gillet). The notice is kept verbatim at the top of every
derived file in [src/clouds/](src/clouds/) and in [LICENSES/Clouds-MIT.txt](LICENSES/Clouds-MIT.txt).
Nimbus is part of DaisyExamples (MIT, Electro-Smith; its DSP keeps the Clouds notices). Cumuloid's
README declares MIT and states that it does not include the Clouds sources. The adapter, converter,
chain, meter and diagnostics files are this project's own MIT code.

## Comparison: sample-rate adaptation, tables, block limits and memory

| Topic | Clouds (original) | Nimbus (Daisy Patch) | Cumuloid (Daisy Petal) | Cumulonimbus (here) |
| --- | --- | --- | --- | --- |
| Codec / DSP rate | 32 kHz | Board rate (48 kHz), passed to `InitResources()` and `processor.Init()` | Codec at **32 kHz** "to match Clouds' internal DSP rate" (README) | Codec 48 kHz always. Build option: grain kernel at 48 kHz, or at 32 kHz behind a fixed 48↔32 kHz converter |
| Grain durations | `lut_grain_size` = floor(1024·2^(4x)) samples = 32–512 ms at 32 kHz | Same table **in samples**, not scaled by the rate, so 21–341 ms at 48 kHz. Index divides by the table size 257 instead of 256 | Unchanged (runs at 32 kHz) | Same table formula, **scaled to the internal rate**: 32–512 ms in both builds (bit-identical to Clouds at 32 kHz; tested) |
| Window table | `lut_window` from `lookup_tables.py`, step 1/4096 | Computed at boot, step 1/4097 | Unchanged | Computed at Prepare from the Clouds formula, step 1/4096 |
| Block limit | `kMaxBlockSize = 32` (`frame.h`) | `SetAudioBlockSize(32)`: "clouds won't work with blocks bigger than 32" | Not changed (32 kHz build) | The host block stays 48 frames. 48 kHz build: two calls of 24 frames. 32 kHz build: one call of 32 frames (48 · 2/3) per host block. A one-shot trigger applies once, in the first call |
| Per-call smoothers | `SLOPE(0.9, 0.2)` once per call, gain `ONE_POLE(0.01)` per sample, at 32 kHz | Unchanged (so faster at 48 kHz) | Unchanged | Converted to the same time constants for the configured rate and call size |
| Random | `stmlib::Random` (global LCG) | `rand()` | stmlib | Same LCG recurrence, per instance, explicit seed |
| Memory | 118 784-byte large buffer: about 0.93 s stereo PCM16 at 32 kHz | `block_mem[118784]` in internal RAM, `block_ccm` | "Grain buffers (182 KB) live in SDRAM"; `BOOT_QSPI` | 2 s PCM16 ring per channel in **SDRAM** (384 KB stereo at 48 kHz, 256 KB at 32 kHz). Grain state, Clouds tables and the Torus reverb's 64 KiB in **AXI SRAM** behind the image |
| Grain pool | 32 stereo / 40 mono (hi-fi) | Unchanged | Unchanged | Clouds' 32/40 stay the density reference. A runtime cap (0..64) bounds the pool; at cap = reference the scheduling is Clouds' |

## What is used from Clouds, and the local changes

* `clouds/dsp/grain.h` → [src/clouds/grain.h](src/clouds/grain.h): the shared grain renderer
  (reader, window, pan, accumulation). Templated on the PCM source, so the planned SD Grain Sampler can
  use the same renderer on an immutable sample. The window table is passed in. A negative start
  index now wraps correctly.
* `clouds/dsp/granular_sample_player.h` → [src/clouds/live_scheduler.h](src/clouds/live_scheduler.h):
  the scheduling policy around the live write head. Changes: rate-aware sizes and smoothers, the
  reference-pool/cap split, an instance RNG, telemetry counters (started per quality, rejected
  starts, peak active) and a guard against grains longer than half the ring (small test rings only).
  The quality rule (medium interpolation above a quarter of the reference pool) is Clouds'.
* `clouds/dsp/audio_buffer.h` → [src/clouds/pcm_ring.h](src/clouds/pcm_ring.h): 16-bit only. Fix:
  the unfreeze crossfade read one sample past its 256-sample tail; it now reads 0..255. Reads fold
  out-of-range indices defensively.
* `clouds/dsp/granular_processor.cc`, `ProcessGranular()`, `PLAYBACK_MODE_GRANULAR`: the DENSITY
  meta mapping (below .47 regular, .47 to .53 none, above .53 random, overlap = distance · 2.12) and
  TEXTURE → window shape, in [src/cumulonimbus.h](src/cumulonimbus.h). Freeze uses `WriteFade`.
* `clouds/resources/lookup_tables.py`: the `window`, `grain_size` and `sin` formulas, in
  [src/clouds/tables.h](src/clouds/tables.h). No generated `resources.cc` is imported.

Not built (Stage 1 scope): the 8-bit/mu-law buffers, low-fidelity mode and Clouds' own sample-rate
converter, the diffuser (TEXTURE above 0.75), feedback, the pitch shifter, the Stretch, Looping
Delay and Spectral modes, Clouds' own reverb and its dry/wet stage. The reverb is the existing
[Torus reverb adapter](../torus-reverb/) (Rings' reverb, the same Clouds-family design).
