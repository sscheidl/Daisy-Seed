# Stereo Delay

Ping-pong (default) and stereo L/R with clock sync up to the dotted whole
note at 30 BPM (12 s per line in SDRAM), feedback up to 95 %, Dry/Wet and damping.
Core per FX contract 1.0 (wet-only), its own firmware `delay-fx` for the stereo inputs.

| Status | |
| --- | --- |
| Version | Core and firmware 0.1.0 (`src/version.h`) |
| Software | Host, sanitizer and ARM checks passed (see CHANGELOG) |
| Hardware | NOT MEASURED, [hardware test](docs/HARDWARE_TEST.md) |

## Behavior

* **Delay time** is the spacing between two consecutive echoes. With ping-pong that is
  the spacing from side change to side change, with stereo L/R the spacing per channel.
* **Ping-pong:** (L + R) / 2 goes into the left line; the feedback alternates
  sides. Echoes: left after T, right after 2T, left after 3T and so on.
* **Stereo L/R:** two separate lines with their own feedback.
* **Time:** free 10 ms – 2 s, or sync with note values 1/32 to 1/1, also dotted and
  triplet, from the host's tempo. Up to 12 s nothing is clamped. Above that the
  delay clamps to 12 s and reports it (LED D3).
* **Time changes** glide with at most 5 % pitch change. From a 10 % jump on, it crossfades
  between two read pointers over 20 ms.
* **Feedback** 0–95 %; a soft clip in the loop keeps it bounded. **Damping:**
  first-order low-pass in the feedback, 20 kHz down to 1 kHz.
* **Switching off** (host, `host.h`): first the feed is faded out over 10 ms;
  the tail keeps running. It ends when the wet level has been below −90 dBFS for 500 ms plus the delay time
  (so no echo that is still in flight is lost), at the latest
  20 s after switching off with a 1 s fade-out. Then Dry/Wet fades to 0 % in 10 ms;
  the output is exactly dry. If it is switched on again during the tail, the
  feed fades in and the lines are kept. After the tail has ended, the
  delay starts with empty lines. Old echoes do not come back, without the callback having to
  clear the 4.6 MB.

## Controls (firmware `delay-fx`)

| Element | Function |
| --- | --- |
| VR1 | free time 10 ms – 2 s (logarithmic) |
| VR2 / VR3 | Feedback 0–95 % / Dry/Wet |
| VR4 | Damping 1–20 kHz (logarithmic) |
| VR5 | Tempo 30–300 BPM (without external clock) |
| VR6 | Note value (18 steps: 1/32 … 1/1, each normal, triplet, dotted) |
| SW17 | Delay on/off (with tail), LED D8 |
| SW18 | up ping-pong free, middle ping-pong sync, down stereo L/R sync |
| S9 + S16 (5 s) | Bootloader |

LEDs: D1 sync, D2 stereo L/R, D3 clamped, D4 external tempo, D16 callback deadline
exceeded. **MIDI clock** over USB or DIN sets the tempo (average over 24 clocks);
after 2 s without clock VR5 applies again. MIDI CC 102–109 (omni) and SQ parameters over USB.
Pots and switches take effect when they are moved.

## Build and test

```bash
make PROJECT=delay-fx -j4                 # build/DelayFx-v0.1.0.bin
make PROJECT=delay-fx test                # core and firmware on fakes
make PROJECT=delay-fx test-sanitize
make test-fx                              # shared FX contract test (with Stereo Delay)
python3 scripts/check-firmware.py effects/stereo-delay/build/DelayFx-v0.1.0.bin
python3 scripts/check-elf-placement.py effects/stereo-delay/build/DelayFx-v0.1.0.elf --sdram delay_memory:4608000
```

Memory: 2 lines × (12 s × 48 000 + 64 samples margin) as float = 4 608 512 bytes in
SDRAM (`DSY_SDRAM_BSS`), provided by the host and cleared before audio starts.
