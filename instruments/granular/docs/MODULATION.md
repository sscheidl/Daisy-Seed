# Nebulae + Mod on Daisy (separate build)

**Image:** `build/mod/NebulaeMod-v0.2.2.bin` (`make PROJECT=granular-mod`, or
`make PROJECT=granular MOD=1`). Firmware marker: `NEBULAE-MOD-VERSION=0.2.2`; IDENTIFY
reports the key `nebulae` and the version `0.2.2`, the same version as image and marker.
The numbers and hashes below were recorded for 0.2.1; 0.2.2 changes both images on purpose
(real-time repair, [changelog](../CHANGELOG.md)) and keeps the rule that the MOD build
with an empty matrix sounds bit-identical to the regular build.
**Status:** software tested, hardware test pending (**NOT MEASURED**). Checklist:
[MOD_HARDWARE_TEST.md](MOD_HARDWARE_TEST.md).

This is an **additional, separate build** of Nebulae with the
[Mod on Daisy](../../../modules/mod-on-daisy/) modulation matrix, following the
[Looper](../../looper/docs/MODULATION.md) pattern. The regular firmware stays as it is:

* On 0.2.1, `make PROJECT=granular` compiled exactly the code without the matrix; its
  image was **byte-identical** to `main`: 183,740 bytes, SHA-256
  `cbc4c37686be32eca39825459f95c676af7a7010c3c719faba1cf0def4e381ff`, the value recorded
  in [SD_CARD_INTEGRATION_2026-11-03.md](../../../docs/SD_CARD_INTEGRATION_2026-11-03.md).
  It contains no matrix symbol.
* All matrix code sits behind `NEBULAE_MOD`. Only `MOD=1` sets it, together with
  `MOD_ON_DAISY_EXTENSIONS`, and that build uses its own `build/mod/` directory.
* With an empty matrix the MOD build sounds bit-identical to the regular build. A golden
  test runs the production callback through file playback, Live capture, overdub,
  circular, every pot and secondary, CC20-27, notes, bend, freeze, gate grains, SAMPLE and
  KEYBOARD, plus everything only the matrix reads. Both builds must print the same FNV-1a
  hash. One active connection must change it, so the test cannot pass by accident.

The editors (browser, JUCE standalone, VST3) read the columns from the device
(`GET_TARGET`), so they work with this build without a change. The protocol framing is
unchanged.

## Module boundaries

| Part | Owner | Version source |
| --- | --- | --- |
| Instrument DSP | `src/engine.*`, `src/reference_dsp.*` | `src/version.h` (`nebulae`) |
| Column table | `src/modulation.h` (no libDaisy, also read by `mod_sim`) | `src/version.h` |
| Execution-context glue | `src/mod_bridge.h` (no libDaisy, host-tested) | `src/version.h` |
| Matrix | `modules/mod-on-daisy`, unchanged | `mod_on_daisy/types.h` (`mod-on-daisy`) |
| Composition | `src/main.cpp`, `Makefile`, `components/registry.json` (`granular-mod`) | product manifest `COMPONENTS.json` |

The engine knows nothing about the matrix: it takes eight normalized offsets
(`SetControlOffsets`). It has no FX slot, so there is nothing to swap in this product.

## Columns

Nebulae has one shared stereo voice: every column is global, none is latched, and all 64
cells of the grid are allowed. Poly AT therefore acts on every column through the most
recent held note (module rule for global targets). The columns are the eight primaries in
CC20-27 order, so the column number is the `neb::Param` index.

| Column | Target | Pot / CC | Depth 100 means | Span text |
| --- | --- | --- | --- | --- |
| 0 | **Start** | VR1 / CC20 | full range | `100%` |
| 1 | **Speed** | VR2 / CC21 | half the knob: from the centre, the whole -4...+4 range | `+-50%` |
| 2 | **Size** | VR3 / CC22 | full range | `100%` |
| 3 | **Density** | VR4 / CC23 | full range | `100%` |
| 4 | **Pitch** | VR5 / CC24 | half the knob: from the centre, the whole -3...+2 octave range | `+-50%` |
| 5 | **Overlap** | VR6 / CC25 | full range | `100%` |
| 6 | **Blend** | VR7 / CC26 | full range | `100%` |
| 7 | **Window** | VR8 / CC27 | full range | `100%` |

Spans are in knob position, not in octaves or multiples: the Speed and Pitch knobs are
remapped around their detent, so the same offset is a different musical interval above
and below the centre. The secondaries (SOURCE page) are not columns.

### How modulation combines with pots and MIDI CCs

* **Base:** the existing primary register `ui.params.primary[]`, whatever moved it last:
  the pot, a CC, or an overdub that resets Speed and Pitch to unity. It is never
  overwritten by the matrix, so the editors' white line shows the unmodulated base.
* **Value = clamp(base + offset, 0, 1)**, computed in `Engine::ControlTarget()` and fed
  to the existing 20 ms smoother (100 ms for Window). The laws follow as before. With no
  active connection the offset is exactly +0.0 and the value is the base bit for bit.
  A test keeps this equal to the module's `Engine::Apply`.
* **Pickup** compares the pot with the register, so it behaves as in the regular build: a
  pot that passes the modulated value without reaching the base does not take over.
* **Snap bands** live in `LawSpeed`, `LawPitch` and `LawBlendLive` behind the offset. A
  small offset around a centre stays inside the band and is inaudible; endpoints stay exact.
* **Notes** multiply after the Pitch law, unchanged. Last-note priority, velocity gain,
  bend and release are the instrument's; the matrix only observes notes.
* **Record holds:** START, SIZE and SPEED hold during every normal capture, including
  overdub, because the hold sits on the smoother. An offset cannot move them while they are
  held; they follow again when the capture ends. Circular recording does not hold them.
* **Fast modulation of Start or Size** changes the read window; how that sounds is OPEN
  until the listening test.

## Rates

| Stage | Rate | Why |
| --- | --- | --- |
| Matrix tick (`Engine::Tick(48)`) | **1 kHz** on average | TIM5 control timer, counted in milliseconds |
| Offsets into the DSP | **187.5 Hz** | read once per 256-frame audio callback |
| Smoother after that | 20 ms (Window 100 ms) | the existing parameter smoothing |

So the DSP sees a sampled and smoothed copy of the 1 kHz matrix. A one-pole of 20 ms has
its corner near 8 Hz (Window 1.6 Hz): the fastest LFOs (20 Hz) arrive attenuated. This is
the price of applying offsets before the existing smoothing, and it also means offsets
cannot click. IDENTIFY reports the tick rate, 1000 Hz.

**Why the tick counts milliseconds.** The audio callback runs at interrupt priority 0
and TIM5 at 0x0F, the lowest. For the duration of each callback the timer cannot run and
its interrupts merge, so one tick per interrupt would slow every LFO as the audio load
rises. `TickClock` instead owes one tick per elapsed millisecond (`System::GetNow()`) and
catches up. It drops time only beyond 16 ms, three audio blocks (`mod_clock.Dropped()` in
a debugger).

## Ownership

The Looper ticks inside its 48-frame audio callback. Nebulae cannot: its callback is
256 frames, and the 1 kHz path is the control timer, which audio can interrupt mid-`Tick`.
`Engine::Apply` from the callback could read a half-built sum, so one owner per state:

| State | Owner | Hand-over |
| --- | --- | --- |
| `mod::Engine` (Tick, CV/gate, base report) | control timer `PollControls` | |
| Offsets | timer writes, audio reads | `mod::TripleBuffer` of 8 floats, newest complete frame |
| `ui.params` (bases) | audio callback | `mod::TripleBuffer` to the timer, monitor only |
| Controller, patch, compile, replies | main loop | module's own triple buffer and atomics |
| `MidiTap` | USB interrupt writes, main drains | bounded SPSC queues |

The audio callback takes one frame and adds nothing else: no allocation, lock, blocking
call or USB send. `hw.gate_in[].State()` reads the pin and leaves the edge memory of
`Trig()` alone, so the gate 1 grain trigger keeps its pulses while the matrix watches
the same input.

## MIDI over J7

Nebulae reads raw USB-MIDI bytes in the USB interrupt. Four independent consumers see the
same bytes and none consumes them: the note and CC parser, BL (bootloader), NB (Nebulae
STATUS/SHARE) and `MidiTap` (MD and the live layer). Nothing in the regular path changed.

| Message | Regular build | MOD build |
| --- | --- | --- |
| Notes, bend, CC20-27, CC120/121/123 | instrument | unchanged |
| Bootloader SysEx `F0 7D 42 4C` | reply, then bootloader | unchanged |
| NB SysEx `F0 7D 4E 42` | STATUS, SHARE | unchanged |
| MD SysEx `F0 7D 4D 44` | ignored | consumed and answered |
| CC1, channel and poly aftertouch | ignored | matrix sources |
| Notes, CC120/123, clock, start | notes as above; rest ignored | also observed by the matrix |

The live queue holds 128 messages and the frame queue 4 frames, more than the editor's
one open request. When full, a message is dropped and the matrix releases its notes (a
Note Off may be among the lost). The editor retries a lost request after 250 ms. CC1 and
aftertouch keep their last value when USB disconnects.

Replies leave from the main loop, after NB and BL replies, at most one per millisecond. A
sample load can delay them by one 4096-byte chunk (NOT MEASURED). They stop before
PREPARE_SHARE, so no monitor frame competes with the host's note offs.

## SD sharing and patches

The SD management personality is unchanged and has no matrix: HELLO still says
`app=NebulaeOnDaisy`, so the panel treats the MOD image as Nebulae, and the `Nebulae on
Daisy SD` USB identity stays. Sharing, BUSY, Live-loop confirmation, eject, interruption
quarantine and bank/slot restore are the same code as in the regular build.

**The patch is volatile.** There is no flash storage (module `STORAGE.md`, D8). Entering
SD management restarts the kit, which empties the matrix and, after confirmation, discards
a Live loop. So save the patch before sharing and load it afterwards:

1. In the editor: **Save file ...** writes a `.modpatch`.
2. Share, copy samples, eject, **Return to Nebulae** in the panel.
3. In the editor: it reconnects by itself; **Open file ...** sends the patch back.

Since the Daisy Control Panel 0.5.0 (`editors/mod-on-daisy`, branch `claude/dcp-sd-page`)
one app does all of this: its SD card page saves the patch before the share and sends it
back after the return ([docs/dcp/SD_SHARE_PORT.md](../../../docs/dcp/SD_SHARE_PORT.md) § 8).
The launcher below stays for the Nebulae MOD package of PR #102.

Only one client can hold J7 on Windows. The test package's
[entry point](../tools/Nebulae-Mod-Test.ps1) offers **Modulation** and **SD card**, closes
the active app and waits for it to exit before opening the other. It holds no MIDI, CDC
or card logic and never starts sharing. A DAW or browser tab holding the port must be
closed by hand.

## Changed sources

| File | Change |
| --- | --- |
| `src/modulation.h` | new: column table, `MakeModInfo()`, `ReportBases()` |
| `src/mod_bridge.h` | new: `MidiTap`, `TickClock` |
| `src/engine.h`, `src/engine.cpp` | `ControlTarget()`; `SetControlOffsets()` and `Smoothed()` in the MOD build only |
| `src/main.cpp` | `NEBULAE_MOD` only: module, tick in `PollControls`, offsets and bases in `Audio`, `MidiTap` in `ReceiveUsb`, `ServiceModulation()` in the main loop, the marker. `ArmRestore()` is a plain extraction so the restart test runs the production code; the regular image stays byte-identical |
| `Makefile` | `MOD=1`: target `NebulaeMod-v...`, `build/mod/`; `make test` always runs both builds |
| `tools/` | test-package entry point |

## Tests (host)

| Test | Checks |
| --- | --- |
| `tests/test_mod_bridge.cpp` | column table; offset equals `Engine::Apply`; empty matrix bit for bit; tap parsing with running status, real time inside messages, foreign SysEx, longest frame, overflow; tick clock |
| `tests/test_mod_thread.cpp` | the three hand-overs on separate threads; also under TSan in `make test-thread` (Linux) |
| `tests/test_modulation.cpp` | production `main.cpp` on real FAT32 and exFAT images: IDENTIFY, GET_TARGET, GET_ALLOWED; routes and clamping; LFO rate with merged timer interrupts; base monitor; pickup; snap bands; note priority; record holds; gates and CV; NB, MD and BL in one byte stream; USB disconnect; BUSY, confirmation, SHARE, restart, bank/slot and PCM hash restored, matrix empty, saved patch reloaded |
| `tests/test_callback.cpp` | the whole production test in both builds |
| `tests/test_golden.cpp` | the golden performance above, with the negative control |
| `tests/test_admin.cpp` | shared SD management: ownership, eject, interruption quarantine |
| `editors/mod-on-daisy` | core test and browser e2e against the Nebulae column table; patch save and reload |
| `diagnostics/tests/python/test_nebulae.py` | monitor frames on the port do not disturb the panel |

## Resources (MEASURED 2026-11-06, local ARM build, GCC 13.2.1)

| | Regular | MOD=1 |
| --- | ---: | ---: |
| BIN | 183,740 B | 211,360 B |
| DTCM static | 97,900 B | 101,676 B (reserve 29,396 B; the check requires 16 KiB) |
| SRAM image and data | 184,768 B | 212,384 B of 491,520 B |
| WAV file capacity | 41,695,328 B | 41,695,296 B |

CPU load of the matrix on the board: **NOT MEASURED**. The tick runs below audio, so it
cannot raise `callback_peak_cycles` directly; the panel's STATUS line shows peak and
overruns, and D16 latches an overrun.
