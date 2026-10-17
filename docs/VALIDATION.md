# Validation report — Toy Piano v0.1

Recorded on 17 October 2026, Linux x86_64, in a cloud environment without a Daisy kit.
This report documents software validation; translation does not add hardware evidence.

## Checks actually performed

| Check | Result |
| --- | --- |
| Official Desktop API and tables | Compared `Hardware`, button/LED indices, VR1/VR2, and bootloader selection |
| Actual ARM cross-compilation | Passed with xPack GCC 13.2.1, Cortex-M7 hard float |
| BOOT_SRAM BIN | 106,388 bytes; stack `0x20020000`, reset vector `0x24000691` |
| ELF | ELF32, ARM, EABI5, hard float; entry point in SRAM |
| Memory budget | SRAM 106,388 / 491,520 bytes; DTCM 62,812 / 131,072; DMA RAM 17,096 / 32,768 |
| All 16 MIDI mappings | Exact comparison against the requested table, including duplicate C5 |
| All 16 oscillator frequencies | Host render: zero crossings over one second, error < 1.1 Hz |
| AD envelopes | Minimum/midpoint/maximum attack × decay: sample-exact duration, monotonic progression, exact final zero |
| Retrigger | Current level preserved; new attack tested during both attack and decay |
| Time curves | Endpoints, geometric midpoint, monotonic progression over 1,001 positions |
| Knob noise filter | Simulated ±0.05% noise suppressed; both endpoints reachable |
| Callback/debounce | Actual callback with pinned DigitalControl: 1–6 ms dropouts on all 16 buttons do not retrigger; pressing after a confirmed release works |
| Callback/LED mailbox | Startup strike after seven samples, all 16 bits together, LED mask retained after envelope end, identical stereo; no LED transfer in callback |
| 16 independent voices | Polyphonic render compared against the sum of 16 separate instances, including retriggering one C5 button |
| Output level | Test peak 0.279373; no nonfinite values; mathematical upper bound 0.8 |
| Memory/access errors | DSP and callback tests passed ASan + UBSan with `-fno-sanitize-recover=all` |

Checksum of the validated firmware:

```text
a37bddb180ea530e58d2f3c5b4c061c3d5660bae900c8ef93a92e6101346ffe0  ToyPiano-v0.1.bin
```

Commands: `make test`, `make test-sanitize`, and
`python3 scripts/check-firmware.py build/ToyPiano-v0.1.bin`.

The callback test uses unmodified application code and libDaisy's actual
DigitalControl class. GPIO/ADC, time, audio, and LED hardware are host stubs.
With the original condition from `9e0a975`, the test fails on an extra envelope
rise after a contact dropout; it passes with held-state tracking. The tests model
neither real switch mechanics nor LED DMA hardware.

Zero-crossing counting is a coarse frequency check, not a precision tuning test.
The binary check verifies size and stack/reset vectors; it does not guarantee
a successful hardware startup.

## Real-time source review

The audio callback uses bounded loops and static storage. It performs no
allocation, file access, console output, or LED I²C transfer. The shift-register
scan contains only short, bounded GPIO clock delays. LED buffer swaps that may
wait run in the main loop.

Sine tables and frequencies are prepared before audio starts. Per-sample work
covers active voices, interpolated table lookups, and simple AD arithmetic. Two
logarithmic time conversions occur at most once per block when a note triggers,
even if all 16 buttons are pressed together. Retriggering preserves phase and
envelope continuity. Release edges change only the LED mask. Both output channels
receive the same sample.

This is a source review, not a measurement of Seed3 CPU load or worst-case callback
time. Host tests do not replace missing hardware tests.

## Build warnings

Unmodified official Makefiles/startup files and Newlib emit warnings about
unimplemented `nosys` file-descriptor functions, an RWX SRAM load segment, and
FPU/interrupt attributes in startup code. Linking and binary generation succeed.
The application does not use file descriptors. SRAM execution comes from the
official BOOT_SRAM linker script. Project DSP code passes host compilation with
`-Wall -Wextra -Werror`.

## Still to check on hardware

* External bootloader installed and flashing through the carrier USB port works.
* Every physical S1–S16 and D1–D16 maps correctly.
* Real switch bounce, chords, and rapid repeated strikes.
* VR1/VR2 direction, ADC endpoints, noise, and usable time ranges.
* Stereo/headphone output, tuning, tone, volume, and clicks.
* Sixteen voices while adjusting controls without dropouts; actual callback timing and CPU headroom.

The README contains the test procedure. No known errors remain in the validated
DSP behavior; analog characteristics and actual audio playback remain outside
this software validation. See [HARDWARE.md](HARDWARE.md) for short LED flashes and
possible blocking after an LED DMA start failure.
