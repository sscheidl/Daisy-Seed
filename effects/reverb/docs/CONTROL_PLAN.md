# Slöer-Inspired Controls on the Seed3 Desktop Kit

**Status: interaction plan, not implemented firmware.** This plan was written on
October 20, 2026, against `sscheidl/Daisy-Seed` main at `abc5a71` and the
[Walrus Audio Slöer digital manual](https://cdn.shopify.com/s/files/1/0906/8480/files/Sloer_Digital_Manual.pdf?v=1689191307)
(7-page PDF, SHA-256 `6c773b95e83fdcc2237f7a8005b3b94c99284cb1780defba77ea0d1d3d18962d`).
The [Thomann listing and front-panel photo](https://www.thomann.de/de/walrus_audio_sloeer_ambient_reverb_black.htm)
confirm the black pedal's physical layout: four top knobs, a Width switch,
X and Stretch faders, a Mode knob, and Bypass and Sustain footswitches.
The manual documents the pedal's controls, not its internal DSP algorithm.
The firmware is an original Ambient Reverb and no sonic equivalence is claimed.
The [current v0.1 controls](../README.md#what-it-does) remain authoritative until
an implementation and a new firmware build are validated.

## Manual and board facts

The manual describes Dream, Light, Rain, Dark, and Rise modes (p. 2). Its four
main knobs are Decay, Filter, Depth, and Mix; Width is a three-position switch,
X is a mode-dependent fader, and Stretch changes the pedal's internal processing
rate (pp. 3–4). Rate and Pan are secondary controls reached by holding Bypass
while moving Depth or Mix. Bypass has a momentary-on gesture when the effect is
off. Sustain is momentary in most modes and latching in Dream; Trails can be
switched off (p. 5). Three saved presets and color feedback are described on
p. 6. These are **documented pedal behaviors**, not functions already present
in our firmware.

The pinned `desktop_devkit::Hardware` exposes 16 debounced buttons, 16
single-color brightness-controlled LEDs, and eight pots. Index 0 corresponds
to S1/D1 or VR1 respectively; no extra GPIO assignment is required. The
physical orientation of the kit's controls should still be confirmed by the
operator at the first board test. The kit has no slider or RGB preset LED.

## Proposed direct mapping

The first row selects the pedal-inspired modes and handles live performance.
The second row gives each modulation shape one direct button. This keeps all
basic actions available with one finger and avoids a hidden bypass-plus-knob
layer. Modes are *functional references* to our original engine:

The two pedal faders each become a dedicated physical pot on the Desktop kit:
**X = VR6** and **Stretch = VR8**. They are never combined into one control or
hidden behind a shift gesture. VR8 remains the existing Predelay control in
v0.1 until the Stretch DSP path has been implemented and validated.

| Button | Proposed action | Current DSP mode/state | LED |
| --- | --- | --- | --- |
| S1 | Dream-inspired pad | Cloud | D1 selected |
| S2 | Light-inspired octave up | Halo | D2 selected |
| S3 | Rain-inspired audible taps | Scatter | D3 selected |
| S4 | Dark-inspired octave down | Abyss | D4 selected |
| S5 | Rise-inspired swell | Bloom | D5 selected |
| S6 | Sustain: latch in Dream; hold while pressed in other modes | `hold`, after the behavior change below | D6 active |
| S7 | Effect bypass: tap to latch; hold from bypass for temporary effect | `bypass`, after the gesture change below | D7 effect active |
| S8 | Toggle Trails / No Trails | `trails`, after tail reset is defined | D8 Trails enabled |
| S9–S13 | Direct Sine / Warp / Sink / Step / Random shape selection | `shape` | D9–D13 one selected |
| S14–S16 | Reserved; no action in the first control revision | — | D14–D15 unused; D16 keeps the overrun latch |

One selected mode LED and one shape LED stay lit at a time. LEDs indicate
*applied* switch states, including temporary bypass activation. At startup:
Dream/Cloud, Sine, Trails on, Sustain off, effect on. The default pot values
come from physical positions after ADC settling; there is no preset recall or
soft takeover in this revision. No RGB color or “modified preset” claim is made.

| Pot | Proposed role | Implementation status |
| --- | --- | --- |
| VR1 | Decay, exponential 1–45 s nominal range | Already present; nominal value is not measured RT60 |
| VR2 | Filter/tone, exponential 400–14,000 Hz | Already present; mode-specific pedal filter curves are unknown |
| VR3 | Modulation depth, 0–3 ms | Already present |
| VR4 | Dry/wet mix, 0–100% | Already present |
| VR5 | Continuous stereo width, 0–100% | Already present; 0/50/100% replace the pedal's three switch positions approximately |
| VR6 | X: behavior determined by selected mode | Partly present; see gap table |
| VR7 | Modulation rate, 0.03–1.5 Hz | Already present, promoted from the pedal's secondary control |
| VR8 | **Target:** Stretch, increasing wet-path time expansion and coloration | Not implemented; v0.1 still controls predelay. Keep it labeled **Predelay** until a real Stretch DSP path and new version exist |

The same VR6 position is reinterpreted immediately on each mode change, as on
a physical X control. Do not store hidden per-mode X values. Reuse the current
bounded parameter smoothing for a click-free change; test fast mode switching.

| Manual X role | Current original engine | Required change for the proposed interface |
| --- | --- | --- |
| Dream: vibrato over sustained pad | Cloud has no Texture effect; current Hold disables modulation | Add a bounded vibrato contribution while Dream sustain is active without destabilizing feedback |
| Light: +1 octave amount | Halo `texture` controls octave-up contribution | Map VR6 to that contribution and verify full wet range |
| Rain: diffusion of audible taps | Scatter `texture` changes early-tap level; Scatter also fixes a reduced diffusion factor | Make VR6 control the taps-versus-diffusion character directly; keep output bounded |
| Dark: −1 octave amount | Abyss `texture` controls octave-down contribution | Map VR6 to that contribution and verify full wet range |
| Rise: swell time | Bloom maps `texture` to `riseSeconds` | Retain the exponential 0.05–4 s mapping and verify retrigger behavior |

The pedal's Pan secondary control has no matching parameter in the current
engine. It is deferred; stereo width remains available on VR5. Assigning Pan
to a spare button or adding presets requires a separate, visible UI decision.
Do not claim that the five current modes reproduce the pedal's algorithms.

## Switch behavior to implement

* **Sustain:** In Dream, each qualified S6 press alternates between sustaining
  the current tail and returning to the VR1 decay setting. In the other modes,
  S6 sustains only while held, then returns to VR1 decay. The current engine's
  `hold` freezes a tail *and blocks new input*, whereas the manual describes a
  long trail. The audio behavior must be deliberately changed or described as
  a freeze approximation; a control remap alone is insufficient. Keep the
  transition smoothed and confirm whether new notes feed the sustained tail.
* **Bypass:** A press when active moves to bypass. A press while bypassed
  activates the effect immediately. If released before a proposed 250 ms
  threshold, it stays active; if held at least 250 ms, it returns to bypass on
  release. The threshold is our board design choice, not a manual timing value.
  The audio callback must never wait for a gesture timer. Preserve dry signal
  and use short gain ramps at both transitions.
* **Trails:** S8 toggles the stored setting; default is on at every power-up.
  With Trails on, bypass stops new input and lets the existing tail decay.
  With No Trails, the tail must fade out and be cleared so that it cannot
  reappear when re-engaged. The current `trails=false` only mutes wet output
  while the internal state keeps processing, so it needs a regression test.
* **Shapes:** S9–S13 directly select one shape. Current `Step` is smoothed; the
  manual's Square has hard transitions. Keep the existing shape labeled Step
  until an intentional, bounded Square-style behavior has been assessed.

The pedal's preset-save chord, three color-coded memories, boot-time factory
reset, and power-on Trails gesture are **deferred**. No persistent storage or
bootloader changes are part of the first control revision. S14–S16 are kept
free for a later explicit choice rather than assigning undocumented actions.

## Implementation sequence and acceptance

1. Move button gestures and LED state into a small fixed-size control state
   machine. Map S1–S5 to the existing modes, S9–S13 to shapes, and VR6 by mode.
   Keep `UpdateLeds()` in the main loop. Test simultaneous presses, debounce,
   short contact dropouts, startup with a held button, and 250 ms boundaries.
2. Give Sustain, Bypass, and No Trails their defined DSP transitions. Test
   release, retrigger, mode changes during sustain, and bypass while tails are
   active. Verify finite and bounded audio; no allocation, I/O, or blocking in
   the audio callback.
3. Improve Dream and Rain X behavior and compare synthetic renders with the
   current modes. Build with the pinned ARM toolchain and check SDRAM, stack,
   and callback cycle budget on the actual kit. Only then revise the control
   table in the main README and produce a new BIN.
4. Investigate Stretch separately: the codec can remain at 48 kHz while a wet
   processing path runs at a reduced internal rate with proper anti-alias
   filtering and resampling. Measure quality, memory, and CPU before assigning
   VR8 to Stretch. Do not relabel predelay as Stretch or simulate it only by
   changing decay/tone.

First manual board check: start at low monitor level, verify the physical S/VR
labels against `desktop_devkit::Hardware`, then test each mode, X endpoints,
Sustain press/release, bypass tap/hold, Trails on/off, stereo width, and the
D16 overrun indicator. Record board revision, connections, firmware hash,
callback cycles, and audible discontinuities. No hardware latency or CPU result
is claimed by this plan.
