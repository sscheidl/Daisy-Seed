# Toy Piano on Cumulonimbus v0.1.1

Own products: Toy Piano → [Cumulonimbus](../../../effects/cumulonimbus/) stereo grains → one stereo
Torus reverb on that mix → stereo output. Composition by build flags `CUMULO=1`,
`CUMULO_RATE=48|32` (internal grain rate) and `CUMULO_PROFILE=0|1`. The regular `toy-piano`,
`toy-piano-ambient`, `-tape`, `-fv1` and `-filter` images stay byte-identical (checked against
`origin/main` 241f583 on 2026-11-09).

| Project ID | Image | Package label |
| --- | --- | --- |
| `toy-piano-cumulonimbus` | `build/cumulonimbus48/ToyPianoCumulonimbus48-v0.1.1.bin` | **P48** playable |
| `toy-piano-cumulonimbus-32` | `build/cumulonimbus32/ToyPianoCumulonimbus32-v0.1.1.bin` | **P32** playable |
| `toy-piano-cumulonimbus-profile` | `build/cumulonimbus48-profile/ToyPianoCumulonimbus48Profile-v0.1.1.bin` | **D48** profiling |
| `toy-piano-cumulonimbus-32-profile` | `build/cumulonimbus32-profile/ToyPianoCumulonimbus32Profile-v0.1.1.bin` | **D32** profiling |

```bash
make PROJECT=toy-piano-cumulonimbus test          # all four variants' host tests
make PROJECT=toy-piano-cumulonimbus -j4
make PROJECT=toy-piano-cumulonimbus-32-profile -j4
python scripts/check-firmware.py instruments/toy-piano/build/cumulonimbus48/ToyPianoCumulonimbus48-v0.1.1.bin
```

Status: host tests (also under ASan/UBSan) pass for all four variants, all four ARM images build.
**Everything on the kit is NOT MEASURED** (CPU, listening, switch polarity, flashing).

## Signal path

The mono piano sum (the regular soft-limited Toy Piano output) feeds a **mono** grain ring; each
grain gets Clouds' sin/cos pan law with a random position (Stereo Spread), and the Torus reverb is
stereo. The chain's output guard is the identity up to |x| = 0.8, so the dry piano passes bit-exactly.

## Controls

SW17 selects the edit page only. Both audio stages keep running and keep their settings.

| Pot | SW17 off: Piano page | SW17 on: FX page |
| --- | --- | --- |
| VR1 | Attack, 2–300 ms | Grain position (0 = newest audio, 1 = 2 s back) |
| VR2 | Decay, 50–3000 ms | Grain length, 32–512 ms |
| VR3 | Transpose ±24 st, centre detent | Density: left regular, centre none, right random |
| VR4 | Second-harmonic amount (0.24 = the regular Toy Piano) | Grain pitch ±24 st, centre detent |
| VR5 | Piano level (0.5 = the regular Toy Piano) | Grain mix |
| VR6 | LFO 1 rate (Mod on Daisy) | Stereo spread |
| VR7 | LFO 2 rate | Hall decay |
| VR8 | Random/S&H rate | Hall mix |

| Element | Function |
| --- | --- |
| SW18 | up **Freeze** (ring held, grains and reverb keep playing), centre **Run**, down **FX Bypass** (feeds fade, 2 s tail, then exact dry and the wet work suspended). The physical polarity is checked in the board run |
| S1–S16, USB and DIN MIDI | play the piano on both pages; the 16 key and 16 MIDI voices as in Toy Piano 0.5 |
| S9 + S16 (5 s) | bootloader |

* **Pickup:** after a page change a pot changes its destination only once it reaches the stored
  value (within 2 %) or crosses it. The same applies after an external write (MIDI CC, app).
* **Boot:** if SW17 is on the Piano page at boot, the Piano page starts at the pot positions. The FX
  page always starts at its safe defaults (position 0.1, length 0.45, density 0.65, pitch 0, grain mix
  0.2, spread 0.7, hall decay 0.5, hall mix 0.2) and picks up. Boot cap: 16 grains (conservative;
  the production cap follows the board measurements).
* **Source rates:** VR6–VR8 on the Piano page edit the Mod on Daisy patch (LFO 1, LFO 2, Random
  rates) from the main loop. An app edit (`SET_SOURCE`, patch load) reaches the register through a
  per-source last-value mailbox with its own generation; the pot then has to pick up, and a pot value
  from before the edit is never written back over it (an edit of one source never blocks another).
  The pot edit uses `LoadPatchLocal` (`MOD_ON_DAISY_LOCAL_EDITS`, this product only): an editor write
  retried after its reply was lost is answered, not executed again over the pot, and the reply carries
  the current patch CRC, so the editor reloads. An idle editor (Daisy Control Panel / VST3 0.5.1,
  browser 0.4.1) polls the patch CRC every 2 s and reloads, so a pot change appears there within about
  2 s; older editors see it only after their next pull or edit.

### MIDI

| Message | Function |
| --- | --- |
| Notes, any channel | piano voices (note-offs ignored: one-shot AD envelopes) |
| CC70–77, channels 1–15 | external write of FX VR1–VR8 (0–127 → 0–1); a full queue drops the write (counted as `control_drops` in the profiling images) |
| CC86–90, channels 1–15 | external write of Piano attack, decay, transpose, harmonic, level |
| CC1, aftertouch, SysEx 0x7D | Mod on Daisy as in Toy Piano |
| Channel 16 CC20/21/22/119 | profiling images only: scenario, grain cap, piano load, telemetry ([probe](../../../effects/cumulonimbus/tools/cumulonimbus_probe.py)) |

### Mod on Daisy

Key `toy-piano-cumulonimbus`. Columns 0–4 as in Toy Piano (Pitch, Level, Harmonic, Attack, Decay);
the extended monitor reports the Piano-page bases. New columns: **5 GrainPos**, **6 GrainMix**,
**7 HallMix**, reported with their FX-page values. Six modes report page and SW18 to the app:
Piano Run/Freeze/Bypass, FX Run/Freeze/Bypass. Whole-callback load: `GET_STATS` (all images).

## Profiling images (D48, D32)

Same DSP, compiler options, memory placement and audio priority as P48/P32. Additions: DWT stage
timers (piano, grain write, grain render, rate conversion, reverb, total), a 1 % histogram, callback
start intervals (missed DMA periods), active voices and grains, rejected grain starts, refused MIDI
notes and control writes, stale rate reports, the meter's own cost (`accounting_max`, measured after
the last DWT sample, so outside the reported peak) and the untouched DTCM stack (painted at boot), and
the channel-16 controls. A snapshot carries its own window and metadata. Playable images keep only
`GET_STATS`.

| CC20 scenario | Route |
| --- | --- |
| 0 | full: grains + reverb |
| 1 | dry: both stages off (no wet work; output = dry Toy Piano) |
| 2 | grains only |
| 3 | reverb only |
| 4 | grain ring writes only |
| 5 | grain rendering from a held ring |
| 6 | full work, both wet mixes forced to 0 |

CC21 sets the grain cap (0–64), CC22 the piano load: 0 none, 1 = 1 MIDI voice, 2 = 8, 3 = 16 MIDI,
4 = 16 MIDI + 16 key voices, all with 3 s decays and a chord retrigger every 500 ms. Every change
restarts the statistics.

## Memory (ARM link map, 2026-11-09)

| Region | P48 | P32 | Contents |
| --- | --- | --- | --- |
| SDRAM `.sdram_bss` | 385 056 B | 257 056 B | grain ring storage (sized for stereo; the mono ring uses one channel) |
| AXI SRAM `.axi_bss` | 94 592 B | 94 592 B | host + chain (grain state, Clouds tables, converters), Torus reverb 64 KiB |
| DTCM `.data` + `.bss` | 12 724 + 107 192 B | same | Toy Piano and Mod on Daisy as before |
| Image | see the run sheet | | |
