# Grain Cloud: rules and behaviour

This file is the written behaviour of the host core 0.1.0. Every row points at its test.
Nothing here ran on hardware: all sound judgements and all timing on the kit are NOT MEASURED.

## One block

All of this runs in the audio context, in this order, so nothing needs a lock:

1. **Sample hand-over** at the block boundary.
2. **Events**, at most 16 per block, in input order. They never wait for the tick.
3. **Control tick**, every `tick_blocks` blocks: matrix, derived values, window morph, voices.
4. **Births**, at most 16 per block across all voices, each with its frame offset.
5. **Render**: grains into a voice bus, the bus through the voice gain ramp, float sum.
6. **Effect point**: `RenderSum()` ends here. The product runs its FX contract stages.
7. **Output stage**: `Output()` is the one clip point.

`Process()` is steps 1 to 7 with nothing in step 6.

## Rules

| Rule | How the core does it | Test |
| --- | --- | --- |
| R1 Control tick | `ControlTick()` runs before rendering in the same context. Default `tick_blocks = 1`, 1 kHz at 48 frames; 4 gives 250 Hz. Note events are taken every block, and a new voice gets its first gain target at once | `TestControlTick`, `TestOneWriter` |
| R2 Effects after the sum | No effect in the core. One stereo point between `RenderSum()` and `Output()`, usable with FX contract 1.0 | `TestEffectPoint` |
| R3 Expensive work at control rate | Period, window step, pitch ratio, count law and envelope coefficients are computed per tick. `grain_renderer.h` has no transcendental call and no division | `make test-rt-scan` |
| R4 Ramps | Voice gain and master gain ramp linearly to the tick's target. The window moves by a slew limit | `TestRamps`, `TestRamp`, `TestWindowSweep` |
| R5 One clip point | Sums are plain float. `OutputStage` is the only limiter, unchanged below 0.8 | `TestSingleClip`, `TestOutputStage` |
| R6 Level follows the grain count | Voice gain includes `CountGain(n)`, n smoothed over 30 ms, ramped per R4 | `TestCountLaw`, `TestCountGainSweep` |
| R7 Note start | Four modes, see below | `TestNoteStarts`, `TestAttackShapesCloud`, `TestKeep`, `TestRetrigger` |
| R8 One matrix, one writer | The matrix is Mod on Daisy. `Modulation::Tick()` is the only write of modulated values. `CellEdit()` is the simplified view and edits only the patch | `TestOneWriter`, `TestCellEdit`, `TestEmptyMatrix` |
| R9 Birth or running | Table below, one test per row | see the table |

## Items of the design list

| Item | How the core does it | Test |
| --- | --- | --- |
| B3 Direction | `Direction` is the probability of a reverse grain, drawn per grain | `TestDirection` |
| B4 Density | Knob and modulation act on octaves of rate: 0.5 to 1024 grains per second | `TestDensityLaw`, `TestDensityDepth` |
| B5 One-sided modulation | Spray, Size and Density: a bipolar source moves upward from the base over its whole cycle. Negative depth inverts the phase and still moves upward. Wheel and pressure rest at zero and keep their sign | `TestOneSided` |
| B6 Shared window | One 257-point live table, morphed between four own shapes at control rate. A grain holds only `win_phase` and `win_step`. Full range takes 1 s | `TestWindowTable`, `TestWindowRow`, `TestWindowSweep`, `TestWindowTarget` |
| B10 Triangular spray | Sum of two draws from the voice's own generator, no table | `TestTriangularSpray`, `TestSprayDistribution` |
| B11 Stealing | The chosen grain gets a larger `win_step` and reaches its window's end within 192 frames. Its slot stays taken until then | `TestGrainStealing`, `TestStolenGrainEndsInTime` |
| C7 Envelope | Stepped at the tick. Linear attack, exponential decay and release, plus `FastRelease`, 5 ms, for stolen notes and All Sound Off | `TestEnvelopeCurves`, `TestFastRelease`, `TestStealAndPanic` |

## Note start (R7)

| Mode | Note-on | Old grains on a retrigger of the same note |
| --- | --- | --- |
| `Grow` | Nothing extra; the first grain starts in this block | Old note fades fast in its own slot |
| `FullCloud` | About density x duration grains with random ages. The window starts part way and the position is advanced by the age | Same |
| `FullCloudAtStart` | The same number and ages, the position is not advanced. Spray still applies | Same |
| `Keep` | Like `Grow` for a new note | Same voice, same generation. Running grains go on under the new envelope, which continues from its current level |

* The cloud is limited by the voice's fair share, the active limit and the pool.
* It is spread at 16 births per block. The rest is a counter on the voice. It lapses at note-off.
* **The attack waits for the cloud.** A grain that enters a rising attack mid-window would step, so
  the envelope starts when the cloud is complete. The delay is cloud size / 16 blocks: about 2 ms
  for 20 grains, 16 ms for 256 grains on one voice. Several clouds at once share the 16 births.
* **Minimum attack** is 4 ms (`kMinAttackSeconds`), fixed by `TestAttackShapesCloud`.

A **stolen note** stays in its own slot, with its own gain and generation. A new note can therefore
never take over old grains. Voice slots are twice the polyphony for this reason. In `Keep` mode the
old grains run out at the level they had (`Hold`). In every other mode they use the fast release.
If all slots are taken, the oldest `Hold` tail switches to the fast release and the note-on waits
for a block or more, at most about 5 ms.

## Birth or running (R9)

The table in code is `kParamInfo` in `params.h`.

| Parameter | Acts | Test |
| --- | --- | --- |
| Position | At birth | `TestBirthTable` position |
| Spray | At birth | `TestBirthTable` spray |
| Size | At birth | `TestBirthTable` size |
| Direction | At birth | `TestBirthTable` direction |
| Pan Spread | At birth | `TestBirthTable` pan |
| Pitch, bend | At birth. Following running grains is a later option (B2) | `TestBirthTable` pitch |
| Scan | At birth: moves the birth point of new grains | `TestScanRow` |
| Density | Sets when births happen; takes effect at the next tick | `TestScheduler`, `TestCountGainSweep` |
| **Window** | **Running** (owner decision B6): running grains follow the shared table | `TestWindowRow` |
| Level, count law | Running, ramped | `TestRamps`, `TestCountGainSweep` |
| Attack, Decay, Sustain, Release | Running on the envelope, read at the next tick | `TestEnvelopeRows` |
| Start | At note start | `TestNoteStarts` |
| Active Limit | Refuses later births; running grains end normally | `TestLimitRow` |
| Filter, effect levels | Not in the core: effects sit behind the effect point | none yet |

## Grains and voices

* **Pool full:** the birth is dropped and counted. There is no backlog, also for a start cloud.
* **Stealing starts** when a voice below its fair share is refused. The share is the active limit
  divided by the playing notes, at least 4. A voice that already has its share only drops its own births.
* **Victim:** a note in release first, then the voice furthest over its share. Inside it, the
  grain closest to its end. At most 4 steals per block.
* **Reserve:** while a voice below its share is waiting, voices at or above their share drop
  births, so a freed slot cannot go back to them.
* **Silent frees:** when a voice reaches gain zero, its remaining grains are returned at once.
  `Stats::hard_kills` counts grains freed at a gain above zero and must stay 0.

## Reading the sample

| Case | Rule |
| --- | --- |
| Loop set, step below the loop length | Wraps in the loop. The right neighbour of the last loop frame is the loop start |
| No loop, or a loop shorter than the step | Holds the first or last frame past the ends; the birth position is clamped |
| Sample of one frame | Constant value |
| Sample of two frames | Linear between both, then held |
| No valid sample | No birth |

Grain length is at least 2 ms by the Size law.

## Samples and events

* `PublishSample()` hands over at a block boundary. New grains read the new sample; old grains
  finish on the old one. `SampleRetired(id)` turns true when the last one has left, and only then
  may the owner reuse the memory (`TestSampleHandOver`, under ASan a late read fails).
* Note-offs and other releases have their own queue. If that overflows too, a sticky all-notes-off
  takes over, so a note cannot hang (`TestNoHangingNotes`, `TestBridge`).

## Paper cost

`make cost` prints operation counts multiplied by assumed cycle figures. **All of these figures are
NOT MEASURED.** Fixed sizes come from the code: `sizeof(Grain)` is at most 48 bytes (`static_assert`).

| Control work | Per second at 1 kHz | At 250 Hz |
| --- | --- | --- |
| Window lerps while morphing | 257,000 | 64,250 |
| Matrix ticks | 1,000 | 250 |
| Voice control steps, 8 notes | 8,000 | 2,000 |

## What is not in this core

Sub-oscillator, effects, keyboard position, clock sync, the load regulator of the active limit, a
Daisy adapter, a shared parameter register and panel pages. Stage 3 starts with the adapter.
