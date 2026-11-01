# Changelog

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
