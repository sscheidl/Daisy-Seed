# Changelog

Versions: component `TAPE_FX_VERSION` and standalone firmware
`TAPE_FX_FIRMWARE_VERSION` in [src/version.h](src/version.h); the Toy Piano test
product `TOY_PIANO_TAPE_VERSION` in `instruments/toy-piano/src/composed_version.h`.

## 0.1.0 — 2026-10-26

* First port of drumlogue TapeVibe (`sscheidl/drumlogue_fx_units` @ 0db9c37): wow,
  flutter, drive with tape compression, head bump, tone/age, hiss and dropouts in
  three characters. Reference path bit-identical to the original in host tests.
* Optional MixForge stages: TAPE/HARD/FOLD drive curves, stereo-linked compressor,
  bitcrusher; each bit-identical to MixForge stage by stage.
* Optimized firmware path: polynomial sine on TapeVibe's own phase (max output
  difference 7.75e-7), block-rate coefficients, compressor shortcut below the knee.
* Products `tape-fx` 0.1.0 and `toy-piano-tape` 0.1.0. Hardware test pending.
