# Toy Piano filter test build v0.1.0

Own test product `toy-piano-filter`: Toy Piano with one [filter core](../../../modules/filter-core/)
per voice and **no effects**, so all CPU goes to voices and filters. It exists to hear the filter
models and to measure how many voices with a filter the kit can play. Composition by build flag
`FILTER=1`. `toy-piano`, `toy-piano-ambient`, `toy-piano-tape` and `toy-piano-fv1` stay unchanged
(ARM images byte-identical, 2026-11-06).

```bash
make PROJECT=toy-piano-filter -j4
make PROJECT=toy-piano-filter test
python3 scripts/check-firmware.py instruments/toy-piano/build/filter/ToyPianoFilter-v0.1.0.bin
```

Status: software tested on the host, ARM image builds. **Everything on the kit is NOT MEASURED.**

## Controls

| Element | Page 1 (SW17 off) | Page 2 (SW17 on) |
| --- | --- | --- |
| VR1 | Pitch ±24 semitones, centre detent | same |
| VR2 | Wave, sine to saw | same |
| VR3 / VR4 | Attack / Decay | same |
| VR5 | Cutoff, 33 Hz to 19.9 kHz | Envelope amount, ±60 semitones, centre off |
| VR6 | Resonance | Filter model: Clean, Sum, Stages, Shared (four zones, left to right) |
| VR7 | Drive | Load test: 0, 4, 8 … 32 voices (nine zones) |
| VR8 | Age | same |
| SW18 | Filter type: up LP 24 dB, middle BP, down HP | same |
| S1–S16 | Keys | same |
| S9 + S16 (5 s) | Bootloader | same |

* **Pick-up:** after SW17 flips, VR5–VR7 do nothing until they reach their stored value, so
  nothing jumps.
* **Envelope:** each voice's own AD envelope also moves its cutoff. There is no second envelope.
* **Age** is a direct control, not a matrix target.
* **Models:** Clean ignores Drive. Sum saturates at the summing point only and is the cheap path.
  Stages saturates in every stage. Shared is one Stages filter on the sum of all voices, for
  comparison only: it has no envelope per note.
* **LEDs:** page 1 shows the keys. Page 2 shows the test: D1–D8 count the load-test voices in
  fours, D9–D16 the peak callback load in steps of 12.5 %. All of D9–D16 lit means the deadline was
  reached.

Over USB the [Mod on Daisy](../../../modules/mod-on-daisy/) matrix works as in Toy Piano. New
columns: 5 Cutoff (per voice, ±48 semitones), 6 Resonance, 7 Drive. Add depth with `mod_probe.py`
or the app, for example `set-cell LFO1 5 40` for an LFO on the cutoff.

## Filter per voice or one shared filter

Static estimate from the ARM compiler, **not a measurement** (2026-11-06). A voice costs its
filter, about 18 to 35 cycles per sample for the per-call work at 16 samples per call, and about
100 to 200 cycles for the voice itself. The last figure is read by hand from the disassembly. The
"voices" column assumes 9000 of the 10 000 cycles per sample are free for voices.

| Setting | Filter | Whole voice | Voices that fit |
| --- | --- | --- | --- |
| Clean | 67 to 118 | 185 to 353 | 25 to 32 |
| Age on, no drive | 132 to 232 | 250 to 467 | 19 to 32 |
| Drive, Sum | 164 to 280 | 282 to 515 | 17 to 31 |
| Drive, Stages | 272 to 480 | 390 to 715 | 12 to 23 |
| Drive, Stages, Age on | as Stages | as Stages | 12 to 23 |
| One shared filter, Stages | about 300 in total | about 100 to 200 | 32, about 3 % for the filter |

A shared filter is far cheaper, but a per-note envelope needs a filter per voice, so per voice
stays the main case. A voice in its 60 ms tail still costs its filter. The matrix adds its known
cost: about 130 000 cycles per block with an active matrix (MEASURED 2026-11-05 on regular Toy
Piano).

## Test sheet for the kit

The owner runs this; nothing here is measured yet. Bring the kit up with `ToyPianoFilter-v0.1.0.bin`
and connect USB. In a terminal, from the repository root:

```bash
python instruments/toy-piano/tools/live_load.py --seconds 600
```

It prints the callback load once a second. Read **Spitze** (the peak load) and **zu spät** (late
blocks, which are overruns). Without a computer, read D9–D16 on page 2.

**Set up:** SW18 up (LP). Page 1: VR2 half, VR3 left, VR4 right, VR5 at noon, VR6 at 70 %, VR1 centre.
Flip SW17 to page 2, turn VR5 to the centre, and VR6 to the model in the table. VR7 sets the number
of voices: no keys are needed, the load test holds them. Step VR7 up one zone at a time and wait
5 seconds per step. Each new zone starts a new peak. A voice count is done when overruns go above 0
or you hear crackle; note the last clean step.

Do each row, then write the highest voice count with 0 overruns and its peak load.

| Row | Model (VR6, page 2) | Drive (VR7, page 1) | Age (VR8) | Highest voices | Peak load | Result |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Clean | left | left | | | NOT MEASURED |
| 2 | Sum | right | left | | | NOT MEASURED |
| 3 | Stages | right | left | | | NOT MEASURED |
| 4 | Stages | right | right | | | NOT MEASURED |
| 5 | Clean | left | right | | | NOT MEASURED |
| 6 | Shared | right | left | | | NOT MEASURED |

Also please write down the peak load at 16 voices in rows 1 to 4: that answers "16 key voices" directly.

**Listening**, Stages unless noted:

| Check | Result |
| --- | --- |
| VR7 Drive from clean to clearly driven changes the character, not only the level | NOT MEASURED |
| Clean, Sum and Stages against each other at the same Drive: which do you prefer? | NOT MEASURED |
| Loud chord with resonance high: resonance gives way | NOT MEASURED |
| High notes with full Drive: is the aliasing a problem? (decides on oversampling) | NOT MEASURED |
| VR8 Age: subtle up to 30 %, clearly worn at 60 %, broken at 100 % | NOT MEASURED |
| Two keys of the same pitch (S8, S9) differ slightly at higher Age | NOT MEASURED |
| Page 2 VR5 up and down: each note opens or closes on its own | NOT MEASURED |
| Resonance 100 %: the note rings at the cutoff, and fades out cleanly | NOT MEASURED |
| No click when SW18 or the model changes while a note sounds | NOT MEASURED |
