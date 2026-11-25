# FERRUM changelog

Earlier versions (up to 0.6.1, as *Looper*) are described in the [README](README.md) and the reports it links.

## Third-party analysis removed — 2026-11-25 (version unchanged: 0.6.1)

* The firmware analysis of a third-party reference device, its feasibility study and the analysis tools
  (`docs/` and `tools/`) were removed from the repository; references in current docs were neutralized.
  FERRUM's code is an independent implementation and does not depend on them. No code or sound change.

## Rename to FERRUM — 2026-11-25 (version unchanged: 0.6.1)

* Public name FERRUM; directory `instruments/looper` -> `instruments/ferrum`, project IDs `looper` -> `ferrum` and
  `looper-mod` -> `ferrum-mod`, firmware `Looper-v0.6.1.bin` -> `Ferrum-v0.6.1.bin` and
  `LooperMod-v0.6.1.bin` -> `FerrumMod-v0.6.1.bin`, CI suite key `looper` -> `ferrum`.
* No code, DSP, control or sound change: both rebuilt 0.6.1 images are byte-identical with the previous ones.
* Kept for compatibility: SD folder `0:/looper`, USB drive name `Looper SD`, control identifiers
  (`app=LooperOnDaisy`, `loopmgmt`, `LOOPER`, SysEx `LP`), Mod on Daisy key `looper`, registry component `looper`,
  version markers `LOOPER-VERSION=` / `LOOPER-MOD-VERSION=` and the C++ names.
