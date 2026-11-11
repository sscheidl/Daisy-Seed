# Stage 1 board run sheet — Toy Piano on Cumulonimbus

For the joint board session (owner + Codex). Every result below is **NOT MEASURED** until the run.
Flashing and all physical tests happen only on the owner's explicit request, with the kit in
bootloader mode confirmed by the owner (Daisy-Seed `AGENTS.md`). Record what was observed, with date,
as `MEASURED`; never fill in expected values.

The package header (generated) lists the exact images, SHA-256, versions, Git revision, compiler
options, memory regions and flash layout. Write results into `results_template.csv` (one row per
measured point) and the notes section at the end.

## 0. Setup

* Kit: Seed3 Desktop Kit, Daisy bootloader v6.4 (`dsy_bootloader_v6_4-extdfu-2000ms`, installed),
  480 MHz core (budget 480 000 cycles per 48-frame block at 48 kHz = 1 ms). Codec 48 kHz, 48 frames.
* Host: USB on J7. Python 3 with `mido` and `python-rtmidi` for `tools/cumulonimbus_probe.py`;
  `tools/mod_probe.py` (Mod on Daisy) for `stats` on the playable images.
* Audio: line in L/R (standalone images), line out L/R to an interface/monitor.
* **Flash** (owner): enter the bootloader (S9 + S16 for 5 s, or the editor button), then either the
  [Daisy Web Programmer](https://flash.daisy.audio/) → Custom → the `.bin`, or
  `dfu-util -a 0 -s 0x90040000:leave -D <image>.bin -d ,0483:df11`. Check the SHA-256 against
  `SHA256SUMS` before flashing.
* Recommended order: **P0 → F48 / F32 → D48 / D32 → the selected P48 / P32**. Keep P0R (released
  Toy Piano v0.2) as the rollback.

### Switch polarity check (once, on the first Cumulonimbus image)

1. F48: SW18 to each position; the S1–S7 LEDs stay; play the impulse source (S15) with Grain Mix up.
   Expected mapping: **up = Freeze** (sound keeps repeating the held texture when the input stops),
   **centre = Run**, **down = FX Bypass** (dry after about 2 s). Record the mechanical direction
   (towards/away from the keys) that gave each behaviour.
2. P48/D48: SW17 **off = Piano page**, **on = FX page**: turn VR5 on each page and note which page
   changes Level vs Grain Mix. Record the lever direction.

## 1. Probe commands

```bash
python tools/cumulonimbus_probe.py --list
python tools/cumulonimbus_probe.py set --scenario 0 --cap 16 --stimulus 4
python tools/cumulonimbus_probe.py reset          # measured window starts now
python tools/cumulonimbus_probe.py snapshot       # prints loads, stages, grains, verdict
python tools/cumulonimbus_probe.py sweep --scenarios 1,2,3,0 --caps 8,16,24,32 --stimulus 4 \
    --warmup 5 --seconds 30 --csv results.csv --label B3 --image ToyPianoCumulonimbus48Profile-v0.1.1 --stop-on-fail
python tools/cumulonimbus_probe.py play --seconds 1800 --voices 16 --cc-traffic   # B4, playable image
python tools/mod_probe.py stats                    # GET_STATS on P48/P32: callback load mean/max, overruns (not P0)
```

`sweep` resets the statistics after each warm-up, measures for `--seconds` (≥ 30 s), and writes one
CSV row per point with the derived peak/mean/p99 load, the expected vs counted blocks and the
verdict: **PASS ≤ 70 % peak** with no missed periods or overruns; **MIDDLE 70–80 %** (report
separately); **FAIL > 80 %** or any missed period/overrun (lower the cap, change placement, reader
or rate before acceptance). `--stop-on-fail` ends a failing sweep after saving its row. A block
count that does not match the window (1 block per ms) is flagged: a duration-only counter could hide
skipped DMA periods, so `missed_periods` comes from callback start intervals.

Scenarios (CC20): 0 full, 1 dry, 2 grains only, 3 reverb only, 4 grain write only, 5 grain render
only, 6 full with wet muted. Stimulus (CC22): standalone 0 input, 1 impulses, 2 bursts, 3 noise;
Toy Piano 0 none, 1 = 1 MIDI voice, 2 = 8, 3 = 16, 4 = 16 MIDI + 16 key voices (3 s decays,
chord retrigger every 500 ms).

## 2. Rows

| Row | Image / scenario | Procedure | Record |
| --- | --- | --- | --- |
| **B0** | D48 and D32 scenario 1 (dry route) for timing; P0 (regular Toy Piano 0.5) for the dry sound only | **Timing:** `sweep --scenarios 1 --caps 16 --stimulus N` for N = 0..4 (0, 1, 8, 16 MIDI voices, 16 MIDI + 16 keys; 3 s decays, chord attacks every 500 ms) on D48 and on D32. P0 has no callback meter (`GET_STATS` is not supported in the regular build), so it gives no comparable timing. **Sound:** P0 vs the D48 dry route at the same *logical* settings. P0 0.5 maps VR1 transpose, VR2 attack, VR3 decay; the Cumulonimbus Piano page maps VR1 attack, VR2 decay, VR3 transpose (harmonic 0.24 and level 0.5 match P0). Play the same notes, e.g. `play --voices 8` | dry load per voice count; `voices_peak` (actual concurrent DSP voices); output equivalence by ear (the host test shows bit-exact) |
| **B1** | F48 and F32 | External stereo input with independent L/R material, then stimuli 1/2/3. `sweep --scenarios 4,5,2 --caps 8,16,24,32 --stimulus 3` (write-only, render-only, grains only) and scenario 0 for the full chain. Check: L-only impulse comes out L (Spread 0), R-only R; Spread right widens; Freeze holds and re-enters with a crossfade; first grains after boot and after Bypass warm up from silence | stage costs per cap, `grains_peak` reached, stereo routing, freeze/re-entry by ear |
| **B2** | D48 and D32, one effect at a time | Same note load (stimulus 3 or 4) and pot snapshot (write down VR1–VR8 on both pages): `sweep --scenarios 2 --caps 8,16,24,32`, then `--scenarios 3 --caps 16` | marginal grain and reverb cost, actual grains per cap |
| **B3** | D48 and D32, full graph | Stimulus 4. Points: long overlapping grains (Length right, Density far right), short high-birth grains (Length left, Density far left), Spread right, Pitch ±24, simultaneous retriggers (the stimulus-4 chord attacks), page flips with pickup, Freeze and Bypass during the measurement, normal USB/MIDI/LED traffic (`play --cc-traffic` in parallel). Then a **deliberate over-cap case** (cap 8 with dense settings): every refused start appears in `rejected` | worst peak per cap; missed periods; rejected starts accounted |
| **B4** | Candidate P48 or P32 | Heaviest viable B3 setting by hand; `mod_probe.py stats` before/after; soak **30 minutes** with `play --seconds 1800 --cc-traffic`. By ear: stereo spread, tails, clicks at Freeze/Bypass/page changes, dry bypass, note response, pickup behaviour | `GET_STATS` mean/max/overruns after 30 min, listening notes |

Use at least 30 s of measured time per sweep point after the warm-up. Start with low caps and
raise them. The owner chooses the production rate and cap from the measured curve and the sound.

## 3. Things to look for

* Exact dry: in Bypass (after ~2 s) and with Grain Mix and Hall Mix at 0 the output must equal the
  regular Toy Piano.
* Long tail across Bypass: set Hall Decay right, play, switch to Bypass, wait well over 2 s, return to
  Run. The reverb keeps its state while suspended, so a remnant of the old tail fades back in with the
  mix (state policy, by design). Note whether it is audible.
* Probe acceptance: every MIDI note or control-queue drop (`midi_overruns`, `control_drops`) and every
  block-count mismatch fails the point. Record `stack_free` from the heaviest D32 point (DTCM headroom
  for the soak) and `accounting_max` (the meter's own cost, outside the reported peak).
* Pickup: after an SW17 flip, a pot does nothing until it reaches its stored value; after a MIDI
  CC70–77 write (or an app rate edit) the same. Held notes keep sounding across switch changes.
* Freeze: the texture continues from the held ring; releasing crossfades (256 samples).
* Bypass exit: no click; the grains return gradually as the ring refills.
* Load differences between D48/D32 and P48/P32: the profiling images add the DWT reads and the
  meter (bounded loops). Confirm the heaviest pass on the playable image with `GET_STATS`.

## 4. Notes (fill in during the run)

| Date | Image | Observation (MEASURED) |
| --- | --- | --- |
| | | |
