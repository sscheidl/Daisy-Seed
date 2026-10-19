# Architecture

## Ownership and signal flow

Main loop: USB Listen/PopEvent, bounded batches of 64; retry one pending event
when the 256-entry SPSC queue is full. No MIDI echo or transmit path. LED mailbox
is a lock-free uint32_t; LED transfers happen only after changes and at most at
100 Hz. USB framing/running status uses the pinned libDaisy parser with a local compatibility fix.
`src/midi_parser.cpp` provides the same Parse/Reset symbols before archive linking;
the library archive does not pull its original parser object. Reset initializes
all state, and system-subtype checks are qualified by SystemCommon. UBSan exposed
uninitialized enum reads in a non-static parser; an MTC test exposed subtype state
leaking into subsequent notes. No submodule or other firmware is changed.

Audio IRQ: official controls at 1 kHz, 16 key edges, pot movement detection,
stable switch combination, at most 32 MIDI events, four 12-sample DSP quanta.
Only the audio IRQ mutates parameters and DSP. No locks, heap allocation, USB
operations, file access, logging or LED DMA occurs there. The placement new in
startup explicitly clears the NOLOAD SDRAM backing storage, then constructs the DSP
after Hardware::Init. A poisoned-storage regression exercises this path; it does not
allocate heap memory. All loops have fixed bounds. The MIDI queue is the tested
Toy Piano SPSC implementation, copied under this project's namespace.

Keys / USB -> shared note FIFO -> Torus Part (four original voices) -> stereo
Dry -> one Torus Reverb (wet-only) -> dry/wet crossfade -> 0.8 * Master -> peak
protection -> outputs. Input ADC audio is intentionally unused.

## Voice allocation

`Part::Trigger` advances the original round-robin index, latches MIDI note and
velocity * excitation separately for each slot, and flags that slot's strum.
Each quantum consumes at most four notes, then renders before another note can
replace any slot's pending strum. Consequently all sixteen local keys get distinct
excitations inside one audio callback. A 256-note audio-owned FIFO preserves
larger MIDI bursts across callbacks. At most four resonator slots sound; fifth
and subsequent strikes retune/steal in deterministic order. Note Off has no DSP
action, and never silences a tail. Tests exercise repeated unisons and duplicate C5.

USB queue consumption stops when the note FIFO exceeds 192 entries, leaving room
for local keys and a bounded MIDI batch. There is a diagnostic OverflowCount for
an explicitly exhausted audio FIFO; software cannot store an unbounded event
stream. Incoming USB data beyond libDaisy's own transport/parser capacity can
still overrun its upstream buffers. No unbounded drain occurs in the IRQ.

Torus' CV NoteFilter and single external strum allocation are bypassed. Stored
voice notes are absolute MIDI; tonic=0 and `PerformanceState::fm` contains the
smoothed sum of transpose and pitch bend. That offset affects all sounding
voices without retriggering. Modal, sympathetic, inharmonic, FM, quantized chord,
and string/reverb routines remain upstream DSP. The BRYAN_CHORDS table from the
pinned Daisy port is retained. At four voices Western Chords uses the original
two-string intervals. Modal resolution remains the original 64/polyphony-4.

The upstream maximum buffer length stays 48. Rendering uses valid 12-sample
sub-blocks; interpolators use the actual size. Part LFO increments are adjusted
from 48/48000 to 12/48000 to preserve real modulation rates. The onset strummer
is unnecessary for explicit keyboard triggers; its original helper is retained.

## Excitation and model transitions

Modal impulse amplitude and plucker noise are scaled before resonance; FM
velocity drives its internal amplitude/brightness envelopes and its oscillator
baseline at maximum damping. No finished-output volume substitution is used.
Existing resonator states decay naturally. A zero-excitation strike adds no energy.

A model change fades to zero over 192 samples (4 ms), configures the original
resonators at zero gain, and fades in over 192 samples. Notes arriving during
fade-out stay queued. The dry and wet outputs share the fade, so changing model
6's right-channel polarity cannot abruptly invert a full-level reverb tail.
The global reverb state is retained across switches.

## Global reverb and output

Part no longer owns a Reverb instance or initializes a second buffer. Model 6
retains its dispersion, special damping curve, stereo cross-routing by Position,
and final right-channel polarity. Polarity is applied after the global mix:
applying it before the reverb's L+R input would cancel a centered stereo signal.

One unchanged-topology Torus 16-bit FxEngine reserves 32768 uint16_t words
(65536 bytes). amount=1 returns wet only. External linear mixing has exact dry
and wet endpoints, with no duplicate dry component. Reverb runs even at dry=1
so its state remains continuous. Feedback time=0.35+0.63*Decay; diffusion=0.9*x;
input gain=0.5*x; lp=0.05+0.9*(1-Damping). Defaults: normalized decay=.5,
damping=.5, diffusion=.625, input gain=.4, wet=.25. The diffusion setter is a
normalized musical control; actual coefficient is bounded at .9. No fabricated
RT60/seconds mapping is claimed. All feedback coefficients stay below unity.

The original Torus limiter retains model gains. The final protection is linear
through .6 FS after 0.8 * master gain, then a continuous-slope rational knee
approaches .8; a .799999 roundoff guard enforces a strict bound. Nonfinite samples
are silenced. This catches final mix peaks without altering normal small signals.

## Parameter ownership and memory

The official ADC oversampling/filtering is followed by 20 ms smoothing, 0.2%
movement hysteresis and 0.3% endpoint snapping. Only changed knobs write targets;
MIDI values persist while knobs stand still. A moving knob takes control without
pickup. Targets and 5 ms smoothed values live in one ParameterManager. Switch
changes share the Model target with CC28; a stationary switch never overwrites CC.

All DSP and the initialized sine table use preallocated SDRAM; USB and hardware
state remain in DTCM. The ELF checker verifies ARM hard-float ABI, every load
segment, BIN/ELF equivalence, BOOT_SRAM regions and at least 8 KiB DTCM reserve.
Compiler .su files provide static stack frame estimates; real IRQ nesting and
CPU deadline margin remain hardware tests. See VALIDATION for measured sizes.
