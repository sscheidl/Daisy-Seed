# Brief for Codex: second review, hop scheduling of the Nebulae vocoder (0.2.2)

Written 2026-11-09 by Claude at the owner's request. Read-only review, as before: no code,
firmware or protocol changes, no kit access (it runs a diagnostic image). Your first
report is [CODEX_SECOND_OPINION.md](CODEX_SECOND_OPINION.md); what was done with it is in
[OVERLOAD_DIAGNOSIS.md](OVERLOAD_DIAGNOSIS.md) sections 9 and 10. Branch
`claude/nebulae-overload-9c0df4`, head d090a3c.

## 1. Where 0.2.2 stands (MEASURED 2026-11-09 on the kit, details in section 10)

The kit no longer freezes. With the Engine in AXI SRAM, CMSIS real FFTs, `sqrtf`,
`PcmReader`, `WrapFmod` and the mono shortcut, the hop block of both vocoder channels on
a stereo file is 3.2 ms (PITCH centre) to 4.1 ms (PITCH maximum, 4x) of the 5.333 ms
block: fill 1.0 to 2.0 ms (SDRAM cache misses; a `PLD` prefetch experiment gained 0.4 ms
at high pitch and cost 0.35 ms at normal pitch and was dropped), FFT 1.35 ms for six
transforms, phase loops 0.65 ms. Grains cost 0.17 to 1.57 ms, the rest 0.17 ms.

* PITCH centre, any grain setting: no overruns (at most about 92 % of the block).
* PITCH maximum with maximum DENSITY/OVERLAP/BLEND: about 5.7 ms, 107 %: the overload
  protection (second overrun within 16 blocks leaves the vocoder out of 8 blocks and
  restarts it from a reset with the 256-sample ramp) carries the kit, the vocoder layer
  stutters about ten times a second. Audibility NOT MEASURED (no listening yet).
* The 70 % target of your report is not reachable on the common hop grid with
  bit-identical changes.

## 2. The variants on the table

The owner wants to hear the variants before deciding and asks you for a technical review
of each, in particular of the inter-channel effects.

| | Variant | CPU per block (worst case) | What changes in the sound |
| --- | --- | --- | --- |
| A | Keep the common grid; protection handles the extreme | 5.7 ms (107 %) at PITCH max + max grains, else fine | none, except the stutter at the extreme settings |
| B | Plain channel stagger: the right channel computes its hop one block after the left, with its own (newer) position and pitch | about 3.8 ms (71 %) | the right channel's vocoder frames appear 256 samples (5.3 ms) later than the left's and may read a source window 512 frames further on; on correlated stereo material a comb filter in the mono sum (notches every 188 Hz) and a shift of the centre image |
| C | Stagger with compensation (your synchronised two-block pipeline in its simplest form): the right channel computes one block later with the **stored** positions and pitch of the left channel's block, the left vocoder output is delayed by one block | about 3.8 ms (71 %) | both channels exactly as today relative to each other (bit-identical to today's output delayed by 256 samples for identical inputs); the whole vocoder layer is 5.3 ms later than grains, dry path and what an overdub records |
| D | Rounding-level arithmetic: fast inverse square root in the phase loops, float window fill | perhaps 0.5 to 0.8 ms saved; worst case about 94 % | last-bit level, same chaotic amplification as any rounding change (section 10) |

The owner's remark: the inter-channel effect of B might even be attractive as a sound.
Claude's view: B changes the stereo relation of the original instrument and should not
be the default; it could be a deliberate, switchable character, but not the fix.

## 3. Questions

1. **B's inter-channel effect, precisely.** For the right channel one block late with the
   newer position: when does it read the same 512-frame grid point as the left and when
   the next one, as a function of SPEED and the position's phase on the grid? Is the
   resulting effect a constant 5.3 ms delay, a jittering window choice, or both? How
   audible is that on (a) decorrelated stereo, (b) correlated stereo (near-mono mixes),
   (c) transients, (d) mono fold-down, and under FREEZE (position constant)?
2. **C's correctness.** With the left channel's positions and pitch stored for one block
   and the right channel processed on them one block later, and the left vocoder output
   delayed by 256 samples: is the result exactly today's stereo output delayed by one
   block? Check the state hand-over at Reset (generation change, SOURCE, bank retirement,
   the overload protection's reset), the first block after a reset, the mono shortcut
   (right channel copies the delayed left), and what the recorder captures in Live
   overdub/circular mode, where the mix is written back into the bank: does the 5.3 ms
   vocoder offset accumulate over repeated overdubs?
3. **C's audibility.** Vocoder 5.3 ms later than grains and dry path, with the vocoder's
   own 42.7 ms window: is there any setting where the offset is audible (Live mode with
   dry at BLEND 0.5, transients, short loops)?
4. **CPU of C.** Each block then computes one channel's hop: left hop with its fill, or
   right hop plus the left's one-block delay copy. Do you see a hidden cost (the stored
   positions, the delay line, cache effects of alternating channels) that undoes the
   halving?
5. **Listening method.** The owner wants to hear A, B and C against each other. Claude
   proposes one listening image (`PROFILE=1` build) with a runtime switch of the hop
   scheduling mode through a new diagnostic SysEx (NB `04 <mode>`), so the owner switches
   while a sample plays instead of flashing three images; the production image would
   carry only the chosen mode. SW17 has no function outside SAMPLE mode (README,
   `control.cpp` `bank_view`), so a listening image could also switch on the kit itself.
   The owner finds a switchable image complicated and asks for the simplest method that
   still gives a fair comparison: separate images per variant, a SysEx switch, SW17, or
   something else? A mode change implies a vocoder reset. What should the listening sheet
   contain (material, settings, what to listen for, blind order)?
6. **D.** Is 0.5 to 0.8 ms realistic for a fast inverse square root (two Newton steps) and
   a float fill on Cortex-M7, and would you take D in addition to C, or not at all?
7. Anything in the measurements that you would not trust before the decision (peaks right
   after CC changes, the minimum as "steady" value, no mono file reached on the kit).

## 4. Deliverable

`instruments/granular/docs/CODEX_SECOND_OPINION_2.md` on this branch: per question
**agree / disagree / unsure** with evidence (file and line or a calculation), your ranking
of A to D, and the listening sheet of question 5. Host commands as in the first brief. Do
not touch the kit; it runs a diagnostic image and the owner is the only one who flashes
or listens.
