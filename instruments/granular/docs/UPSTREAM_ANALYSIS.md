# Stage 0 - upstream audit and decisions

Audit date: 2026-10-19. Local base: `abc5a71` (main). Implementation branch:
`codex/nebulae-on-daisy-v0.1.0`. User explicitly selected full **local**
implementation/validation. The attached document supplied the specification;
its remote-push/PR and optional separate-Claude-review directives were not
additional user authorization. No remote write, release, flash or bootloader
operation is part of this local execution.

## Primary sources and pins

| Source | Pin / evidence |
| --- | --- |
| [Qu-Bit firmware](https://github.com/Qu-Bit-Electronix/QB_Nebulae_V2/tree/4be42b5159ae5c63466d84f0a8ab3b2fb4afd5a6) | master `4be42b5159ae5c63466d84f0a8ab3b2fb4afd5a6` |
| Shipped v2.1.2 instrument | `FirmwareUpdates/v_2_1_2/neb_update.zip` is a gzip/tar despite its extension; inspected `QB_Nebulae_V2/Code/instr/a_granularlooper.instr`, SHA256 `76526da69b6c68bf8d2649712d28685889b4e8dc0c0aa2a995aae3f62851514f` |
| Other original code | `Instruments/Granular Looper/a_granular_looper.instr`, `Code/instr/a_granularlooper.instr`, Python control handler/conductor; alternate instruments excluded |
| [Official v2.1.2 manual](https://www.qubitelectronix.com/s/Nebulae_v212-af64.pdf) | 31 PDF pages, cover v2.1.2, SHA256 `439f92c8caea23587069247d2d5bb4be8c5a59f2c28096231c45c3f32fc2a8d6` |
| [Current product page](https://www.qubitelectronix.com/shop/p/nebulae) | Also links the newer v2.1.2.1 manual; reviewed for discovery, older requested v2.1.2 is the reference here |
| [De-Pattern port](https://github.com/De-Pattern/Nebulae-Granular_Looper_Pedal/tree/073a1ae4f4ce41bda54f8c78819f3b2a3319df4a) | main `073a1ae4f4ce41bda54f8c78819f3b2a3319df4a`; README, VERSIONS, LICENSE, active DSP/engine/main, archive listing and recent history inspected |
| [Csound mincer](https://github.com/csound/csound/blob/efbcabb4d56e6237e2cabc9949cfadadda946cf9/Opcodes/pvlock.c) | `efbcabb4d56e6237e2cabc9949cfadadda946cf9`; phase-lock/interpolation/OLA implementation checked; `Engine/fgens.c` verifies window definitions |
| libDaisy | `f715e4875b2d28445d0c11a0a17f064a248f69d0` |
| DaisySP | `2c72eaf9eac5fc0dca1919d65d606da907832618` |

References below use **printed manual page numbers**; PDF page number is printed
page +2. This prevents confusing primary/secondary functions with alternate
instruments on printed pages 24 onward. Manual is linked, not redistributed.

## Control and behavior reference matrix

| Function | Manual | Original code evidence / local decision |
| --- | --- | --- |
| START | p10 | phasor config: normalized offset; stay inside final valid frame |
| SPEED | p8 | `gkspeed*8-4`, unity snap; fixed pot re-map puts +1 at physical center |
| SIZE | p10 | squared fraction of source remaining after START; minimum one source frame |
| DENSITY | p11 | tanh/cubic mixture, exponent between 0.12 and 2500; actual endpoints 0.12467168 and 983.25273 Hz, not a linear 0-2500 control |
| PITCH | p9 | `0.125*exp(gkpitch*log(32))`; center-pot re-map selects unison |
| OVERLAP | p11 | squared scalar; 4 periods below 4 Hz, 6 above, +1 ms, max 8000 ms |
| WINDOW | p12 | `ftgen`/window blend: Gaussian, Blackman-Harris, RampDown, Bartlett, RampUp, GEN20-9 sinc, rectangle; 100 ms smoothing |
| BLEND File | p7 | final mix uses square roots of b and 1-b, no independent dry control |
| BLEND Live | p7, p12 | three-way square-root coefficients; 0.48-0.52 snaps to dry center; no loop means wet silence |
| RECORD | p12-13 | `CplxBufRec1/2`, processed stereo feedback, bounded buffer, overdub; adapt to immutable PCM16 banks |
| FILE | p13 | sequential available file order; numbered FAT slots replace alphabetic USB scanning |
| SOURCE | p14 | latching on short release; hold opens secondaries; local threshold 300 ms |
| RESET | p14 | resets playback to START, not beginning of the underlying allocation |
| FREEZE | p14 | speed becomes zero, spectral/grain engine continues |
| Clear / save | p13 | FILE+RESET clear needs 600 ms armed hold locally; FILE+FREEZE save is explicitly unsupported/read-only |
| Startup/sample | p6, p20-22 | local boot without card and first existing slot; restricted PCM16 WAV replaces original broad format support; no file reloading delay animation |

## Secondary matrix

Manual p15-18 covers all secondaries; the local mapping uses the same eight fixed
pot roles as the primary page. SOURCE+RESET substitutes for the unavailable
pitch-encoder click that restores defaults.

| Original secondary | DSP / local mapping |
| --- | --- |
| START: grain position | Squared spray amount, bounded normalized source reads |
| SPEED: input gain | +0 to +20 dB fixed pot; zero defaults to unity |
| SIZE: random panning | Per-source grain pan between its original side and a random position |
| DENSITY: time displacement | Squared amount, random delay up to 10 s; pending grains consume the bounded pool |
| PITCH: random variation | Per-grain random +/-2 octaves per manual; v2.1.2 instrument uses `rand(amount*1200)` (+/-1 octave). Manual takes precedence for this ambiguity |
| OVERLAP: random grain size | Independent squared bipolar deviation, scalar clamped 0-1 |
| BLEND: FM | Stereo input modulates individual grain reads, bounded; disabled for Live and external-trigger-only minimum density |
| WINDOW: random masking | Independent per-grain probability, 0-99% |
| RECORD | Circular latch; input contribution is inverse dry blend, manual p17; bank-boundary publication replaces in-place updates |
| FILE | Forward/backward/random cycle; original three orders, p17 |
| FREEZE | External grain trigger latch; gate input 1, minimum Density disables internal generation, p18 |

## Port selection and exclusions

The port's VERSIONS distinguishes baseline 1.0/1.9, experimental 1.1-1.5 catch,
window/density/LFO changes, and 2.0-ALEXTBLEND. Later archived 1.26/1.27/1.29/1.32/
1.33 and 2.0/2.1 builds are not a single authoritative stock instrument. The
active source at the chosen commit includes additional frequency-shifter/phaser/
filter code. Only its window bank, radix-2 FFT, mincer, RMS and control-law
implementations were selected into `reference_dsp.*`, with explicit changes.
No archived binary, Terrarium GPIO map, mono float double-buffer recorder, LFO,
engine UI, effects or two-way-plus-separate-dry blend was imported.

`Mincer` preserves N=2048, four-way overlap, Hann analysis/synthesis, complex
phase differences, adjacent-bin phase locking and the 2/3 OLA scale. PCM reads
now use a channel-aware interleaved PCM16 view with rate-correct source positions.
Backward-frame interpolation uses its own fractional position rather than
Csound's reused front fraction; wrap interpolation includes the first frame.
The stereo grain scheduler is a bounded adaptation of the instrument's subset of
partikkel, not an import of the whole opcode. See README differences.

## Hardware/API and realtime findings

`desktop_devkit.cpp`: SW17 is a two-way Switch, SW18 is Switch3 initialized with
the official pins. `switch3.h` specifies active-low pina -> POS_UP=1, pinb ->
POS_DOWN=2, both pulled high -> POS_CENTER=0. Mode decode tests cover these three
API states and 20 ms stability; no guessed GPIO aliases are used. Actual lever
direction remains PENDING-HARDWARE. Toy Piano `src/config.h` supplies the exact
16-note layout, including duplicated 72, and external J7 USB routing.

Pinned `hid/audio.cpp` has a 1024-word buffer and **256-sample maximum**. The
Pedal's reported 512/64 timing cannot be applied to this checkout. Local block
is 256 and the actual API result is checked. Worst audio callback can perform
both channels' three FFTs; CPU timing is unmeasured, with DWT overrun diagnostics.
The nominal 1 kHz low-priority callback keeps controls independent of main SD
waits; its actual jitter during audio still needs hardware measurement.

Stock SD diskio assumes DMA-accessible buffers and waits up to 30 s. This project
provides its own **read-only** `SD_Driver` without modifying the submodule: one
aligned 512-byte AXI SRAM bounce sector, cache maintenance, 20 ms DMA timeout,
USB service between sectors, abort on timeout/error. Only main uses FatFS.
No full float copy of a file exists. Publication requires audio fade/ack, and
main waits for ack release before accepting another destructive load.

Raw `MidiUsbTransport` uses the same external port as Toy Piano, with an explicit
bounded parser and atomic queues. This avoids the pinned generic handler's
non-atomic event FIFO and parser initialization/running-status pitfalls while
retaining the official hardware and transport. No received message is transmitted.

## Licenses and status

Original MIT texts are unchanged under `LICENSES/`. Csound `pvlock.c` is LGPL
2.1-or-later despite the Pedal repo's MIT declaration; its notice, COPYING,
source provenance and relinking instructions are also included. No claim that
all imported DSP is solely MIT is made.

**Stage 0 PASS:** accessible pinned original code, exact requested official
manual, port/version comparison, full control/DSP matrix and license chain.
Mechanical mapping, audio quality equivalence and callback time are not software
audit results. [Validation](VALIDATION.md) records subsequent stage evidence.
