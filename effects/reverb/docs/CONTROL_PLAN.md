# Slöer-Inspired Desktop Controls

**v0.2 implementation and expansion plan, October 20, 2026.** Five original
Ambient Reverb algorithms and the Desktop control layout are implemented in
source; the new image has not yet been run on hardware. It is not a port or a
sonic clone of Walrus Audio firmware.

## Sources and distinction

The [official Slöer manual](https://cdn.shopify.com/s/files/1/0906/8480/files/Sloer_Digital_Manual.pdf?v=1689191307)
(7 pages, SHA-256 `6c773b95e83fdcc2237f7a8005b3b94c99284cb1780defba77ea0d1d3d18962d`)
describes five modes, Decay/Filter/Depth/Mix knobs, a three-way Width switch,
mode-dependent X and Stretch faders, secondary Rate/Pan controls, and Bypass and
Sustain footswitches. The [Thomann product photo](https://www.thomann.de/de/walrus_audio_sloeer_ambient_reverb_black.htm)
confirms that front-panel arrangement.

The [Spin FV-1 datasheet, rev. 2017-08-29](https://archive.org/download/SPN1001-DS-170829/FV-1.pdf)
(original datasheet mirrored by Internet Archive, SHA-256
`80e6b53501e9bc7003ffb16805abd63460fb5b76b816ea5f707cdd71188f516b`)
states under “Crystal oscillator” that a logic-level clock can drive X1 and
the system sample rate follows that clock. It names 32,768 Hz and 48 kHz as
examples with different audio bandwidth. The manual assigns sampling-rate
manipulation to **Stretch**, while X has mode-specific musical roles. These
documents establish the chip capability and pedal control function. They do
**not** establish that the opened Slöer board actually varies FV-1 X1, its
clock range, or the proprietary algorithm. Our Daisy implementation changes a
software wet-clock ratio while the codec remains at 48 kHz.

The pinned `desktop_devkit::Hardware` supplies 16 buttons, matching LEDs,
eight pots, `tog_sw18` (three positions), and `tog_sw17` (two positions). The
official Desktop template uses those toggle APIs. The user's physical labels
“Switch 1” and “Switch 2” must still be matched to the API members on the
actual board; no extra GPIO mapping is introduced.

## v0.2 control contract

| Control | Applied function | LED |
| --- | --- | --- |
| S1–S5 | Cloud, Halo, Scatter, Abyss, Bloom respectively (Dream, Light, Rain, Dark, Rise inspired) | Exactly one of D1–D5 lit |
| S6–S14 | Reserved for original algorithms 6–14 | D6–D14 off |
| S15 | Bypass; tap latches, hold from bypass temporarily enables effect | D15 lit while bypassed |
| S16 | Sustain; latching in Cloud, momentary in other modes | D16 lit while active |
| Three-way `tog_sw18` | Up Wide (100%), center Medium (50%), down Narrow (0% wet width) | No dedicated LED |
| Two-way `tog_sw17` | Released Stereo; pressed Legacy Mono | No dedicated LED |

The two-way switch's physical orientation is **unverified**; “pressed” is the
libDaisy state. In Stereo, the left input is duplicated only when the right
jack is unplugged. In Legacy Mono, a connected L/R pair is half-summed; a
single left input retains its level. One wet engine processes the adapted
signal, and identical output is sent to both channels. VR5 Pan is ignored at
the fully mono endpoint. A 40 ms ramp smooths
the switch transition. Phase-opposed stereo input can cancel in mono.

S15 from active enters bypass. From bypass, pressing it immediately enables
the effect; release before 250 ms leaves it enabled, while a longer hold
returns to bypass on release. The threshold is our design choice. Trails stay
on: bypass stops new wet input after a ramp, while the existing tail decays.
S16 extends the nominal decay toward 45 s without freezing the wet buffer or
blocking new input. It returns to the VR1 decay when released outside Cloud;
Cloud toggles it on each qualified press. Changing modes clears Sustain.
Neither “Sustain” nor “Legacy Mono” claims exact Slöer behavior.

| Pot | v0.2 function |
| --- | --- |
| VR1 | Nominal decay, 1–45 s, exponential |
| VR2 | Filter/tone, 400–14,000 Hz, exponential |
| VR3 | Modulation depth, 0–3 ms |
| VR4 | Dry/wet mix, 0–100% |
| VR5 | Wet Pan, left–center–right |
| VR6 | Mode-dependent X |
| VR7 | Modulation rate, 0.03–1.5 Hz |
| VR8 | Stretch wet-clock ratio, 1–0.25 (48–12 kHz at a 48-kHz codec rate) |

X adds wet modulation in Cloud **while Sustain is active**; it controls
octave-up amount in Halo, tap level and diffusion in Scatter, octave-down
amount in Abyss, and swell time in Bloom. Each mode reads the current physical
VR6 position. No hidden per-mode value or soft takeover is used. The DSP
shape stays Sine in v0.2; there is no shape-selection gesture.

Stretch uses a bounded fractional scheduler for the wet engine, a 64-tap
low-pass FIR before rate reduction, and linear wet-output interpolation. Filter
coefficient banks are designed before audio starts; control updates only
interpolate their coefficients. It
extends wet delay times and lowers wet bandwidth as the ratio falls. Dry audio
and codec timing remain at 48 kHz. The filter performs 64 taps × 2 channels ×
48,000 samples = 6.144 million tap contributions per second, plus control
work and the wet engine. This is an operation count, **not** a measured CPU
percentage. The wet engine's 1,114,112-byte SDRAM reserve is unchanged; the
v0.2 link uses 69,108 bytes DTCM, 130,428 bytes SRAM, and 17,096 bytes DMA RAM.

LEDs show the **applied** button states. D15 is on when bypass is engaged,
including after a temporary-effect release; D16 is on while Sustain is active.
The previous D16 overrun indicator has moved to the `overruns` debugger
counter. LED I²C transfers remain in the main loop; the audio callback never
waits for them. The callback also performs no allocation or file access.

## Expansion and hardware acceptance

S6–S14 provide nine unused, direct algorithm slots. New algorithms need stable
IDs, explicit VR6 behavior, output-level matching, and worst-case RAM/cycle
checks. Select one mode at a time. S15/S16 remain tentative
until Bypass and Sustain are tried on hardware; no bank switch is needed.
Presets, RGB feedback, separate Trails Off, and exact pedal stereo curves are
outside v0.2. The current five native modes are functional references, not
reconstructions of the pedal modes.

First device test: start at low monitor level, verify both physical toggle
positions, then test S1–S5, S15/S16, corresponding LEDs, all eight pots,
stereo/mono routing, phase-opposed stereo input, Stretch endpoints, and mode
changes during a tail. Read `maximumCycles`, `overruns`, and `budgetCycles` in a
debugger during the heaviest Halo/Abyss and VR8 sweeps. Record board revision,
connections, firmware hash, levels, audible clicks, and the measured cycle
maximum. The host tests and ARM build do not prove hardware latency, sound,
or callback headroom.
