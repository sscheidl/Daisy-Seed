# Provenance and distribution

* [Qu-Bit MIT](Qu-Bit-MIT.txt): Qu-Bit Electronix, Inc. (2019), instrument by
  Stephen Hensley (2017), source commit
  `4be42b5159ae5c63466d84f0a8ab3b2fb4afd5a6`.
* [De-Pattern MIT](De-Pattern-MIT.txt): Kenny Rakentine (2026), including the
  original Qu-Bit notice. Selected window/FFT/vocoder/RMS/control-law code from
  `src/nebulae_dsp.{h,cpp}` at `073a1ae4f4ce41bda54f8c78819f3b2a3319df4a`.
* [Csound LGPL-2.1-or-later](Csound-LGPL-2.1.txt): mincer is recognizably adapted
  from `Opcodes/pvlock.c`, copyright (C) 2009 V Lazzarini. Its notice is preserved
  in `reference_dsp.cpp`. Window generators correspond to `Engine/fgens.c`.
  Audited commit: `efbcabb4d56e6237e2cabc9949cfadadda946cf9`. The Pedal repo's MIT
  label does not remove the underlying Csound LGPL obligations.

New adapter/control/loader/scheduler/tests use [Project MIT](Project-MIT.txt).
The combined DSP is not represented as MIT-only. Pinned libDaisy, DaisySP, STM32
middleware and FatFS retain their licenses in their submodules. No manual PDFs,
factory samples, Terrarium driver, alternate instruments or Pedal BINs are shipped.

Corresponding application source, linker additions and build instructions are
included. Rebuild with pinned submodules/compiler: `make PROJECT=granular clean`,
then `make PROJECT=granular -j4`. DSP objects are generated in `build/`; modifying
`src/reference_dsp.cpp` and invoking the same Makefile recompiles and relinks a
replacement LGPL component. No signature restricts replacement. Preserve the full
corresponding source and dependency pins with binary distributions; a standalone
BIN upload is not a corresponding-source distribution substitute.

The Qu-Bit notice is kept byte-for-byte, including its original trailing space;
the local `.gitattributes` disables whitespace lint only for that license file.
Csound window-generator copyright lines are preserved in `Csound-FGENS-NOTICE.txt`.
