# Validation — Toy Piano v0.2

Date: 2026-10-17. Software checks ran on Linux x86_64 without an attached Daisy.
Operator-reported hardware checks are listed separately below.

Project: `instruments/toy-piano/`. The project-layout build remains byte-identical
at 126,012 bytes. DSP, callback, version header, queue and their tests were taken
unchanged from current main. Documentation translation changes no firmware.

## Executed software checks

| Check | Result |
| --- | --- |
| Official Desktop API/wiring | Hardware class, button/LED order, VR1/VR2 and bootloader selection checked |
| Real ARM cross-compile | Passed with xPack GCC 13.2.1, Cortex-M7 hard float |
| BOOT_SRAM BIN | 126,012 bytes, embedded version 0.2; stack `0x20020000`, reset `0x24000749` |
| ELF | ELF32 ARM EABI5, hard float, SRAM entry point |
| Memory budget | SRAM 126,012 / 491,520 bytes; DTCM 116,072 / 131,072 (88.6%, including about 37 KB MidiUsbHandler); DMA RAM 18,084 / 32,768 |
| 16 key mappings | Exact requested table, including duplicate C5 |
| Rendered key pitches | One-second zero-crossing estimate, error under 1.1 Hz |
| AD envelopes | Minimum/middle/maximum attack × decay: sample-count timing, monotonic stages, exact idle zero |
| Retrigger | Preserved level at trigger; retrigger during attack and decay |
| Time mapping | Endpoints, geometric midpoint and monotonicity across 1,001 positions |
| Pot filter | Simulated ±0.05% noise suppressed; both endpoints reachable |
| Callback/debounce | Real callback plus pinned DigitalControl: 1–6 ms dropouts on every key do not retrigger; qualified release/repress does |
| Callback/MIDI | Real callback and main-loop service against hardware fakes: channel-1 key Note On/Off, velocity 100, ordered bundles of at most eight messages, no send without configured host, no extra Note On on dropouts, consistent output after 80 key changes without main-loop service |
| Callback/MIDI input | Only positive-velocity Note On, all channels; at most 16 notes per block in order; input queue overflow bounded; received notes not echoed |
| LED mailbox / stereo | Startup qualification after seven samples; all 16 bits; held LEDs survive envelope completion; identical output channels; no LED transfer in audio |
| Key polyphony | Polyphonic output compared against 16 independent single-key instances, including C5 retrigger |
| Key level | Render peak 0.279373; finite values; linear worst-case bound 0.8 |
| ASan / UBSan | DSP/callback tests passed with `-fno-sanitize-recover=all` |
| MIDI voices | Pitches 24–108, linear velocity, invalid input ignored, 16-voice allocation, oldest-voice stealing, same-note reuse, envelope/phase continuity |
| Keys plus MIDI | Sum of independent key/MIDI instances matches combined processing |
| Limiter | Monotonic, odd, below 0.8 even at 1e12; small-signal gain within 0.5%; 32 voices remain below ceiling; exact silent zero |
| Aliasing | Second harmonic present at note 100; disabled at note 127, whose second harmonic exceeds Nyquist; mirror component below 1% |
| SPSC queue | FIFO/full/empty/wrap, 300,000 elements across two threads; TSan clean; relaxed-order mutation detected |
| Versioning | Make version matches header; changing version without cleaning changes name/marker; checker rejects wrong name or marker; restoring version restores identical image |
| Mutation checks | Seven DSP defects each caught: alias guard, voice stealing, same-note reuse, limiting, velocity, envelope reset, phase reset |
| Release preflight | Eight tests cover existing tags, draft/published releases, later API pages, similar versions, API failure and invalid versions |

Firmware checksum:

```text
22950050583241cc77fb29380681ac3077bb4475990000e33a4d4f8b5d61d2cd  ToyPiano-v0.2.bin
```

From the repository root:

```bash
make PROJECT=toy-piano test
make PROJECT=toy-piano test-sanitize
make PROJECT=toy-piano test-thread
make test-release
python3 scripts/check-firmware.py
```

The callback test uses production application code and the real pinned debouncer,
with fake GPIO/ADC, time and audio/LED/USB hardware. The original condition from
9e0a975 fails the dropout regression; the held-state guard passes. These tests do
not simulate physical switch mechanics or DMA hardware. The zero-crossing check
is coarse, not a precision tuning measurement. The firmware check validates size,
vector table, name and embedded version; it does not guarantee startup on a board.

## Real-time source review

Audio uses fixed loop bounds and static storage. It does no allocation, file I/O,
logging or LED/USB transmission. Shift-register clock delays are short and bounded.
Sine tables and note frequencies are initialized before audio starts. At most two
log-time conversions occur for a block of key/MIDI strikes. At most 16 incoming
notes are consumed per block. Receive happens in USB interrupt; event consumption
and transmission happen in main.

Key output reconciles debounced state rather than queuing edges. Limiter square
root/division compile to hardware instructions (disassembly reviewed), not libm
calls. Phase/envelope level are continuous at retrigger; releasing only changes
LED state. Left/right samples are identical. The note-127 harmonic transition
exception is documented in [MIDI.md](MIDI.md).

This is source/disassembly review, not measured worst-case CPU time. The earlier
instruction-based estimate is about 100–130 cycles per active voice, up to roughly
40% of a 1 ms block at 480 MHz with 32 voices. It is an estimate, not a guarantee.

## Build warnings

Unchanged upstream startup/Makefiles and Newlib emit warnings for unimplemented
nosys file-descriptor functions, an RWX SRAM load segment and the startup
FPU/interrupt attribute combination. Linking and BIN generation succeed. The app
does not use file descriptors; SRAM execution follows the official linker script.
Host DSP/callback builds use `-Wall -Wextra -Werror`.

## Hardware checks reported by the operator

For v0.1 keys: flashing through J7, all 16 keys, AD envelope, polyphony, fast playing,
LED mapping, equal S8/S9 pitch, VR1/VR2 direction and no clicks were reported.
These were not cloud-operated tests.

For v0.2 USB-MIDI with Studio One: the device appears as Daisy Seed External;
Studio One plays the kit and key presses arrive in Studio One. Initially a keyboard
entry was missing from track inputs; deleting/recreating it with a new name fixed
the setup. The reported cause was DAW configuration rather than firmware.

On October 17, 2026, the operator reported “all tests green” for the final
v0.2 release BIN, which includes the held-key correction and bundled key-state
transmission, and approved publication. The report did not provide a case-by-case
measurement log for the checks below.

## Remaining hardware checks

* MIDI semitones/velocity/16+ incoming notes together; outgoing chords and all
  Note On/Off messages; power-only operation; reconnect while running.
* Measured callback load with 16 keys plus 16 MIDI voices, including MIDI bursts.
* Stack high-water mark: nominal remaining DTCM is about 14.9 KB, not measured headroom.
* Real switch bounce; the synthetic 1–6 ms dropout regression covers MIDI output too.
* Stereo/headphone comparison and limiter loudness on chords.

The reported functional tests are positive; remaining analog and timing measurements
are listed above. Known LED timing/DMA and USB-send limitations are documented in
[HARDWARE.md](../../../docs/HARDWARE.md).
