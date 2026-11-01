# Hardware test Step Seq 0.1.0

**As of:** 27 October 2026 · Firmware `sequencers/step-seq/build/StepSeq-v0.1.0.bin`
(`make PROJECT=step-seq`). The user carries out the test on the kit. The agent enters the
feedback. All results are **NOT MEASURED** until an entry with a date
is present.

> **Safety:** before step 1 (H14), **do not connect a cable from CV OUT 1/2 to an external
> synth.** The voltage at power-on and the polarity are unknown.

## 0. Preparation

1. Flash the firmware (the user, see [FLASHING.md](../../../docs/FLASHING.md)). Updates
   without BOOT/RESET: S9 + S16 for 5 s.
2. `python3 scripts/check-firmware.py sequencers/step-seq/build/StepSeq-v0.1.0.bin` → PASS.
3. `pip install mido python-rtmidi`, then `python3 sequencers/step-seq/tools/sq_probe.py identify`.
   The result contains `product: step-seq`, the version and the UID.

## 1. Diagnostics H14 first (work order 4.5)

With the diagnostics firmware (`diagnostics/`) or a multimeter on CV OUT 1 and 2:

| Check | Result |
| --- | --- |
| Voltage at DAC code 0, 1024, 2048, 3072, 4095 (both outputs) | NOT MEASURED |
| Polarity (does the voltage rise with the code?) | NOT MEASURED |
| Voltage directly after power-on and after reset, before initialisation | NOT MEASURED |
| Voltage with Step Seq after start-up (start code 0, CV locked) | NOT MEASURED |
| Resulting idle/gate-low code and gate-high code | NOT MEASURED |

## 2. Calibrate and enable with `sq_probe.py`

```bash
python3 sequencers/step-seq/tools/sq_probe.py cal-set 0 --offset <code at 0 V> --scale <codes per volt> --gate-low <code> --gate-high <code>
python3 sequencers/step-seq/tools/sq_probe.py cal-set 1 --offset <code at 0 V> --scale <codes per volt> --gate-low <low code from H14> --gate-high <high code>
python3 sequencers/step-seq/tools/sq_probe.py cal-save kit.json
python3 sequencers/step-seq/tools/sq_probe.py enable
```

* 0 V corresponds to C1 (MIDI 24). A negative scale describes an inverting stage.
* `enable` without calibration of both outputs is rejected (`NAK NOT_CALIBRATED`).
* After a restart, CV is locked again. `cal-load kit.json` only sends to the same kit (UID).

| Check | Result |
| --- | --- |
| `cal-set`, `cal-get`, `enable`, `disable` respond | NOT MEASURED |
| `disable` with the gate open: CV OUT 2 immediately low | NOT MEASURED |
| Locked after restart, `cal-load` on another kit is rejected | NOT MEASURED |

## 3. Controls

| Check | Result |
| --- | --- |
| S1–S16 select the step (LED blinks fast), a long press ≥ 400 ms toggles the gate on release | NOT MEASURED |
| Two or three buttons simultaneously or staggered: no gate toggling, in any release order | NOT MEASURED |
| VR1 octave (5 steps), VR2 semitone (12), VR3 gate length, VR4 accent/slide (4): detent without flutter | NOT MEASURED |
| After a step change, VR1–VR4 only change after a movement of more than 2 % | NOT MEASURED |
| VR5 tempo 30–300 BPM, VR6 swing 50–75 %, VR7 length 1–16, VR8 transpose ±12 | NOT MEASURED |
| With the sequencer stopped: VR1/VR2 play the note for 150 ms (CV/gate and MIDI) | NOT MEASURED |
| SW18 up/centre/down: Forward, Reverse, Random | NOT MEASURED |
| SW17 Start/Stop. The direction "pressed = Run" is correct (otherwise invert `kRunWhenPressed` in `src/main.cpp`) | NOT MEASURED |
| Power-on with SW17 on Run: starts running after initialisation | NOT MEASURED |
| LEDs: gate on bright, off dim, the running light dims or brightens briefly | NOT MEASURED |
| S9 + S16 under 5 s: nothing. From 5 s: all LEDs on, bootloader. Before that gates off, notes off | NOT MEASURED |

## 4. CV/gate on an external synth (only after steps 1 and 2)

| Check | Result |
| --- | --- |
| Gate opens and closes on the synth, retrigger audible on equal notes | NOT MEASURED |
| 1V/Oct over 3 octaves with a tuner (deviation in cent per octave) | NOT MEASURED |
| Slide glides for 60 ms without a new envelope | NOT MEASURED |
| CV IN 1 transposes in semitones, taken over at the start of a step | NOT MEASURED |

## 5. Clock and reset

Change the clock source: `sq_probe.py set step-seq clock_source 1` (Gate IN 1), `2` (MIDI), `3` (Auto).

| Check | Result |
| --- | --- |
| Gate IN 1, pulse width 2 ms, 5 ms, 10 ms up to 120 Hz: no lost step | NOT MEASURED |
| Shortest reliably detected pulse width | NOT MEASURED |
| Clock loss: gate off after 2 s, the next pulse plays the next step | NOT MEASURED |
| Gate IN 2 reset: the next step is the start point | NOT MEASURED |
| Auto: the gate clock takes over, after a loss the internal clock continues on the grid | NOT MEASURED |

## 6. MIDI

| Check | Result |
| --- | --- |
| USB in the DAW: notes on channel 1, accent with velocity 127, legato on slide | NOT MEASURED |
| DIN on an external device: notes and clock (DIN is new in the repo) | NOT MEASURED |
| DIN input: MIDI clock as source (Start, Stop, Continue) | NOT MEASURED |
| Sync in both directions; a received clock is not sent back | NOT MEASURED |
| Unplug USB: DIN notes and gate off. Plug back in: All Notes Off first | NOT MEASURED |
| Jitter of the MIDI clock (input and output) | NOT MEASURED |

## 7. Resource PASS (work order 5, R15)

Stress case: sequencer running at 300 BPM, MIDI clock input via DIN, panel traffic
(`sq_probe.py monitor`), pot sweeps. Then `sq_probe.py stats`:

| Value | Limit | Result |
| --- | --- | --- |
| `callback_overruns` | 0 | NOT MEASURED |
| `callback_peak_cycles` | < 480 000 000 / 1000 = 480 000 | NOT MEASURED |
| `stack_free_bytes` | > 4096 | NOT MEASURED |
| `queue_high_water` | < 64 | NOT MEASURED |
| `note_on_dropped`, `midi_queue_overflow` | 0 | NOT MEASURED |

Static values from the ELF check (`scripts/check-elf-placement.py`, MEASURED
2026-10-27 on the build computer, not a hardware value): DTCM 110 600 / 131 072 bytes,
static reserve 20 472 bytes. SRAM 152 824 bytes, SDRAM 0.
