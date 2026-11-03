# Changelog

## 0.5.0 — 2026-11-01

* Intentional control-range extension requested after the stable Toy Piano Ambient
  0.1.2 / FX 0.4.0 listening test: modulation depth maximum increases by 33%
  (3 to 3.99 ms), X maximum by 25% (1 to 1.25), and Bloom rise maximum by 25%
  (4 to 5 s). Shared mapping functions live in `src/control_mapping.h`.
* Standalone controls adopt these ranges first (VR3 depth, VR6 X). The core
  accepts up to 4.9875 ms to allow a consumer's Cloud X multiplier of 1.25;
  modulation delay storage is unchanged. Scatter's diffusion blend stays bounded
  at unity when X exceeds the previous normalized maximum.
* Existing direct DSP parameter values within the old ranges retain their meaning;
  physical pot positions deliberately produce stronger settings. Wet rate remains
  32–12 kHz. The wet-only adapter contract/version remains 0.2.0.
* Add shared control adapter 0.1.0 and an existing-SQ parameter contract for
  Reverb LFO rate (0.03–1.5 Hz). Control app 0.4.0 exposes it in standalone,
  VST3 and browser. On standalone FX, moving VR7 takes over from the app.
  See [control/app contract and validation](docs/CONTROL_APP.md).
* Hardware sound and CPU acceptance of 0.5.0: **NICHT GEMESSEN**. The earlier
  stable hardware report applies to FX 0.4.0, not this changed sound.

## 0.4.0 — 2026-11-01

Shared FX development version; hardware acceptance pending.

* Consolidate memory experiments and the separate 28 October gate/32 kHz branch.
* Make the 32–12 kHz wet clock and mode-specific pitch processing the common policy
  for standalone FX and every adapter consumer. Prepare the core at 32 kHz to
  preserve nominal timing at minimum Stretch; codec/dry remain at 48 kHz.
* Preserve outgoing pitch contributions during mode crossfades. Restart newly
  needed shifters with logically empty history and a window-long fade, without
  clearing large buffers in the callback.
* Intentional sound change: lower wet bandwidth, revised Stretch range and pitch
  entry behavior. No existing instrument golden hash is replaced.
* Adapter version 0.2.0 exposes wet rate and processing counters for host diagnostics.
  Reject obsolete experiment switches to avoid silently building different DSP.
* Develop and accept the standalone FX first; instrument release/versioning and
  hardware CPU/listening tests follow separately. See [PERFORMANCE](docs/PERFORMANCE.md).

## Earlier versions

0.3 added Shimmer; 0.3.1 added the shared bootloader entry and update-only USB MIDI.
Historical validation and hashes remain in [README](README.md).
