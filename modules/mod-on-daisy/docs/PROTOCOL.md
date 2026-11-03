# Mod on Daisy — MIDI protocol v1 (specification)

**As of:** 23 October 2026 · **Status:** implemented in `include/mod_on_daisy/protocol.h`
(codec) and `controller.h` (commands), checked on the host. Firmware, host tests and
the JUCE editor use the same codec; `tools/mod_probe.py` mirrors it in Python. Transport: **USB MIDI** via J7 ("Daisy Seed
External", D6). Background: [DECISIONS.md](DECISIONS.md) D9 and E9,
[ARCHITECTURE.md](ARCHITECTURE.md) § 3–4.

## 1. Layers

| Layer | Messages | Effect | Editor needed? |
| --- | --- | --- | --- |
| **Live** | CC1, Channel Pressure, Poly Key Pressure, Note On/Off, CC120/123, MIDI clock `F8`, Start `FA` | values of sources 6–8, pressure table, retrigger, LFO/random sync | no |
| **Configuration** | SysEx `F0 7D 4D 44 …` | grid, source parameters, descriptions, diagnostics, later presets | yes |

* The live layer is **omni**, i.e. valid on all channels (v0.1).
* CC1 and aftertouch are **consumed** by Mod on Daisy.
* Notes are **observed and passed on**.
* Foreign SysEx frames and SysEx with a different project identifier are passed on to
  the instrument by `HandleMidi()` returning `false`.
* `F0 7D 42 4C …` ("BL") is the bootloader request of all firmwares in this repository
  ([common/bootloader](../../../common/bootloader/README.md)), not an MD frame. The
  firmware checks it before `HandleMidi()`; both editors send it via **Firmware update …**.
* The existing instrument CCs (Physical Modeling CC20–33, telemetry channel 16
  CC100–119; Nebulae CC20–27) remain untouched.

## 2. Frame format

```text
F0 7D 4D 44 <ver> <dev> <cmd> <seq> <len> <payload: len bytes> <chk> F7
   │  └─┬─┘   │     │     │     │     │                            └ checksum
   │   "MD"   │     │     │     │     └ payload length, 0…109
   │          │     │     │     └ 0…127, assigned by the editor, rolling
   │          │     │     └ request 0x01–0x3F; reply = request | 0x40; 0x7F = NAK
   │          │     └ device ID 0…15; 0x7F = all (IDENTIFY only)
   │          └ protocol major version, here 1
   └ SysEx manufacturer ID "non-commercial / development" (D9)
```

| Rule | Value | Reason |
| --- | --- | --- |
| Frame length | **≤ 120 bytes** including `F0` and `F7` | libDaisy stores at most 128 bytes between `F0` and `F7` and reports longer messages truncated but as complete (`SYSEX_BUFFER_LEN`, E9) |
| Project identifier | `4D 44` ("MD") | `0x7D` is shared by all non-commercial projects |
| Checksum | `chk = (128 − (Σ bytes from <ver> to end of payload) mod 128) mod 128` (Roland style) | detects byte errors; the length field detects truncated or shifted frames |
| Device ID | default 0. In v0.1 fixed in the firmware, stored later (STORAGE § 3) | several kits on one computer |

**Acceptance of a frame** in this order:

1. Header `7D 4D 44` does not match → `false`, to the instrument.
2. `<dev>` is neither the own ID nor `0x7F` → consumed, no reply.
2a. `<cmd>` has the reply bit (`0x40`–`0x7F`: reply, `NAK`, monitor frame) → consumed,
   **never answered**. Reason (hardware test 24.10.2026): with input monitoring, Studio
   One sends everything that comes from the Daisy back to the Daisy. A NAK for every
   returned frame resulted in endless NAK loops (measured at around 500 frames/s), which
   crowded out the replies to the editor. Part of `MOD_ON_DAISY_EXTENSIONS`
   (Physical Modeling from `0.1.0-mod4`, Toy Piano from v0.4, Toy Piano Ambient); can be
   switched individually with `MOD_ON_DAISY_IGNORE_REPLIES=1`. Toy Piano v0.3 and the
   measurement firmware do not have this rule.
3. `<ver> ≠ 1` → `NAK 6`.
4. Length or checksum wrong → `NAK 1` or `NAK 2`.
5. Otherwise execute.

**Payload encoding** (all bytes 7-bit):

| Type | Bytes | Encoding |
| --- | --- | --- |
| `u7` | 1 | direct |
| `u14` | 2 | LSB first, 7 bits each |
| `u32` | 5 | 32 bits in 5 × 7 bits, LSB first |
| `str` | 1 + n | length `u7`, followed by 7-bit ASCII, without null ("±" is transmitted as `+-`) |
| `packed(n)` | n + ⌈n/7⌉ | 8-in-7: every 7 data bytes are preceded by one MSB byte and 7 × 7 bits |
| `mask64` | 10 | 64 bits in 10 × 7 bits, LSB first; bit `8·s + t` = cell (source s, target t) |
| `depth8` | 1 byte before packing | `int8_t` −100…+100 in two's complement |

## 3. Commands

### 3.1 Identification and target description

| Cmd | Name | Request | Reply (`cmd \| 0x40`) |
| --- | --- | --- | --- |
| `0x01` | `IDENTIFY` | — | `u7` major, `u7` minor version of the protocol · `u7 u7 u7` module version · `str` instrument key (≤ 24) · `str` instrument version (≤ 12) · `u7` kMaxActive · `u14` effective tick rate in Hz · `u7` preset slots (v0.1: 0) · `u32` schema hash · `u7` capabilities (bit 0 storage, bit 1 monitor, bit 2 diagnostics, bit 3 poly AT global = last note, bit 4 base values in the monitor, bit 5 extended monitor, bit 6 LFO cross-modulation; § 3.6) · optionally appended `u7` capabilities 2 (bit 0 instrument mode; § 3.6) |
| `0x02` | `GET_TARGET` | `u7` column 0…7 | `u7` column · `u7` flags (bit 0 present, bit 1 `per_voice`, bit 2 `latched`) · `u14` span · 16383 · `str` name (≤ 12) · `str` span text (≤ 8) |
| `0x03` | `GET_ALLOWED` | — | `mask64` allowed cells (ARCHITECTURE § 3.2) |
| `0x04` | `GET_MODE` | `u7` mode 0…count−1 | `u7` mode · `u7` count · `str` name (≤ 24) · `str` operating hint (≤ 24). Only with capabilities 2 bit 0, otherwise `NAK 3` (§ 3.6) |

The **schema hash** is a CRC-32 over the instrument key, all eight target descriptions and
the poly AT rule. If the editor already knows the hash, it skips `GET_TARGET`.

### 3.2 Grid and source parameters

Every reply to a state-changing command contains `u7` status (0 = accepted),
`u7` active cells and `u32` **CRC-32 of the whole patch after the change**.

| Cmd | Name | Request | Reply |
| --- | --- | --- | --- |
| `0x10` | `GET_MATRIX` | — | `packed(64)` depths · `mask64` enabled · `u32` patch CRC |
| `0x11` | `SET_MATRIX` | `packed(64)` depths · `mask64` enabled | status · active · patch CRC |
| `0x12` | `SET_CELL` | `u7` source · `u7` target · `u7` magnitude 0…100 · `u7` bits (0 negative, 1 enabled) | status · active · patch CRC |
| `0x13` | `CLEAR_MATRIX` | — | status · active · patch CRC |
| `0x18` | `GET_SOURCE` | `u7` source (0 LFO 1, 1 LFO 2, 2 Random) | `u7` source · `u14` rate · `u7` shape or mode · `u7` sync · `u7` gate · `u7` division · `u7` retrigger (Random: 0); with capability bit 6, for LFO 1/2 additionally `u14` fm + 100 (§ 3.6) |
| `0x19` | `SET_SOURCE` | `u7` source · `u7` parameter (0 rate, 1 shape/mode, 2 sync, 3 gate, 4 division, 5 retrigger, 6 fm + 100 only with capability bit 6) · `u14` value | status · active · patch CRC |
| `0x1A` | `GET_NAME` | — | `str` patch name (≤ 16) |
| `0x1B` | `SET_NAME` | `str` name | status · active · patch CRC |

**Frame sizes**: `GET_MATRIX` reply = 9 header + 74 + 10 + 5 + 2 = **100 bytes**;
`SET_MATRIX` = **95 bytes**. The whole grid is therefore transferred in one frame and
applied **atomically**. An invalid grid (more than 8 active cells, a disallowed cell
active, depth outside ±100) is rejected as a whole.

**Rejections for `SET_CELL`**:

* A 9th active cell → `NAK 7`; the patch stays unchanged.
* A disallowed cell → `NAK 8`.
* A disabled cell may receive a depth at any time. It only counts once it is
  enabled.

### 3.3 Diagnostics and monitoring

| Cmd | Name | Request | Reply |
| --- | --- | --- | --- |
| `0x30` | `MONITOR` | `u7` rate Hz (0 = off, ≤ 25) | `u7` rate (reply code `0x70`); after that unsolicited frames **`0x7E`** with 8 × `u14` source values, 8 × `u14` global offsets and 2 × `u14` CV raw values before calibration and dead zone (offset binary, 8192 = 0, ±8191 = ±1), 36 bytes. With capability bit 4, 8 × `u14` base values of the columns follow (pot/CC position or fixed value before modulation, 0…1; −1 = not reported), then 52 bytes. With bit 5, 8 × `u14` minimum and 8 × `u14` maximum per source in the monitor window, `u7` clock state, `u7` aftertouch type, `u7` note, `u7` value, `u7` age also follow: 89 bytes. With capabilities 2 bit 0, `u7` active mode and `u7` origin follow: 91 bytes (§ 3.6) |
| `0x31` | `GET_STATS` | — | 10 × `u32` (50 bytes): ticks · matrix cycles min/mean/max · callback load mean/max (in 0.01 %) · overruns · lost MIDI events · main loop peak values in cycles for the `HandleMidi` pass and `Service` (measurement firmware, TEST_PLAN § 3) |
| `0x32` | `RESET_STATS` | — | — |
| `0x33` | `SET_SCENARIO` | `u7` scenario no. | status (measurement firmware `bench/` only; in the instrument `NAK 9`) |

* Monitoring ends **3 s after the last `MONITOR` request**. The editor renews it every
  second. A closed editor therefore leaves no data stream behind.
* The main loop reads the values from a snapshot that the audio IRQ publishes via an
  SPSC queue.
* Apart from monitor frames, the device **never sends unsolicited**.
* Base values and the extended monitor are **backwards-compatible extensions**: they are
  appended at the end; a client that reads 36 bytes notices nothing. A client recognizes
  them by the frame length and by the capability bits. Details in § 3.6.
* The monitor frames have the code `0x7E` because `0x70` is already the reply to `MONITOR`
  (`0x30 | 0x40`). The browser editor test found this collision. Code `0x7E` belongs to
  the request `0x3E`, which does not exist and is always answered with `NAK`.

### 3.4 Device and presets (specified, not implemented in v0.1)

| Cmd | Name | Request | Reply |
| --- | --- | --- | --- |
| `0x20` | `STORE` | `u7` slot 0…7 | status · `u32` CRC (only after completion; see STORAGE § 5) |
| `0x21` | `RECALL` | `u7` slot | status (0 ok, 1 empty, 2 CRC invalid, 3 foreign instrument/schema) · active · patch CRC |
| `0x22` | `GET_SLOT_INFO` | `u7` slot | status · `str` name · `u32` CRC · `u32` sequence |
| `0x23` | `SET_POWER_ON` | `u7` slot (0x7F = default patch) | status |
| `0x24` | `GET_CALIBRATION` | — | CV offsets 2 × `u14` · device ID |
| `0x25` | `SET_CALIBRATION` | like the reply | status (v0.1: RAM only, without storing) |

In v0.1 the firmware answers `0x20`–`0x23` with `NAK 9`, and the editor greys out the
storage functions (IDENTIFY, bit 0). In phase 2, `0x24`/`0x25` serve to measure the
CV zero points and act only in RAM.

### 3.5 Errors

`0x7F NAK`: `u7` rejected command, `u7` code.

| Code | Meaning |
| --- | --- |
| 1 | checksum wrong |
| 2 | length wrong (length field ≠ actual length, frame > 120 bytes) |
| 3 | command unknown |
| 4 | value out of range |
| 5 | busy |
| 6 | protocol version not supported |
| 7 | limit of active connections reached |
| 8 | cell not allowed (greyed out) |
| 9 | not implemented |
| 10 | storage error |
| 11 | schema does not match |

### 3.6 Extensions (capabilities bit 4–6, as of 24.10.2026)

The extensions only exist when building with `MOD_ON_DAISY_EXTENSIONS=1` and are
enabled per instrument at run time (`Module::EnableExtensions({monitor, lfo_fm})`).
Without the switch, the code is byte-identical to the previous state. Currently they are
enabled only in the Physical Modeling build `MOD=1` (`0.1.0-mod3`, capabilities `0x7A`).
Toy Piano v0.3 and the measurement firmware still report `0x0A` and send 36-byte frames.

**Extended monitor (bit 4 + bit 5).** The monitor frame `0x7E` grows to 89 bytes:

| Byte | Content |
| --- | --- |
| 0–35 | 8 source values, 8 global offsets, 2 CV raw values (as before) |
| 36–51 | 8 base values of the columns, 0…1, −1 = not reported (`Engine::ReportBase`) |
| 52–67 | 8 × `u14` minimum per source over the monitor window |
| 68–83 | 8 × `u14` maximum per source over the monitor window |
| 84 | clock state: bit 0 MIDI clock running, bit 1 gate 1, bit 2 gate 2 |
| 85 | aftertouch type of the most recently received message: 0 none, 1 channel, 2 poly |
| 86 | note of the last poly AT message |
| 87 | pressure value: channel AT current; poly AT of the note from byte 86, 0 as soon as it is released (also after All Notes Off) |
| 88 | age of the aftertouch message in 20 ms, 127 = 2.54 s or older |

* **Window:** minimum and maximum apply since the previous frame (50 ms at 20 Hz) and
  are tracked per tick in the audio IRQ. After each frame the main loop advances an
  atomic window number; the audio IRQ resets its own accumulators at the next tick.
  There is thus no unprotected write access by the main loop to shared values. A source
  without an active connection is not computed; its window shows the idle value.
* **Clock state:** a clock counts as running as long as the last pulse was less than
  3 periods (MIDI clock, at least 0.25 s) or 2.5 periods (gate, at least 50 ms) ago, at
  most 4 s. This way the editor distinguishes "no clock" from "no monitor data".
* **Aftertouch type:** determined by the most recently received message, not by the
  larger value. A message with value 0 also sets the type. Capture runs regardless of
  whether a matrix connection is active. The sources channel AT (row 6) and poly AT
  (row 7) stay technically separate; the poly AT row still follows rule N1 (most
  recently played note).

**LFO cross-modulation (bit 6).** Each LFO gets a parameter `fm` from −100 to
+100: the respective other LFO changes its rate by up to ±2 octaves
(value of the other LFO from the previous tick × fm/50 octaves). Effective only when
free-running; with MIDI clock or gate the clock determines the rate, and the value stays
stored.

* `SET_SOURCE` with parameter `6` (`fm`), value `fm + 100` (0…200). Without bit 6 → `NAK 4`.
* `GET_SOURCE` appends `u14` `fm + 100` for LFO 1 and 2 (payload 10 instead of 8 bytes).
* The patch then has **format 2** with 111 instead of 109 bytes (STORAGE § 2). The patch
  CRC covers the format byte: default patch format 2 `C422B385`, test vector `9D6E020E`
  (format 1: `61C264C4`, `E2340CC6`).

**Instrument mode (capabilities 2, bit 0; as of the evening of 24.10.2026).** An
instrument with switchable modes (Physical Modeling: six models via SW17/SW18 or CC28)
reports which mode is currently sounding. Enabled with `Extensions::modes`, only together
with the extended monitor; currently only in the Physical Modeling build `MOD=1`
(`0.1.0-mod4`).

* The seven capability bits of IDENTIFY are taken. IDENTIFY therefore appends a second
  byte **capabilities 2**. A client that stops reading after the capabilities notices
  nothing; if the byte is missing, 0 applies.
* `GET_MODE` (`0x04`) returns the name and operating hint of a mode, e.g. "Western Chords"
  and "SW17 gedrückt · SW18 Mitte" (firmware strings, quoted verbatim). The editor queries
  all modes once when connecting.
* The monitor frame grows by 2 bytes to **91 bytes**:

| Byte | Content |
| --- | --- |
| 89 | active mode 0…count−1; 127 = not yet known |
| 90 | origin of the last switch: 0 controls on the device, 1 MIDI, 127 unknown |

* The mode reported is the one that is **sounding** (for Physical Modeling after the
  crossfade), not the one requested.

Whoever changes a protocol field changes the firmware, `mod_probe.py`, both editors, the
simulators and their tests together.

## 4. Reliable transfer

1. **One outstanding request**: the editor sends the next request only after a reply
   or a timeout (250 ms). After that it retries at most three times with
   **the same** `seq`. If the reply still does not arrive, the connection counts as
   disconnected.
2. **Retries without double effect**:
   * For state-changing commands (`SET_*`, `CLEAR_MATRIX`, `SET_SCENARIO`) the device
     remembers the last `seq` and the last reply.
   * If a byte-identical frame with the same `seq` arrives again, it sends the stored
     reply without executing again.
   * Any other change of the patch clears this memory, for example a key combination.
   * Read commands are always answered fresh.
   * Clients start with a random `seq` so that a newly started client does not
     accidentally receive an old reply.
3. **Confirmation with CRC**: every change is answered with the patch CRC. The editor
   computes the same CRC from its model, with the same `patch.h`. If the two differ, a
   **resynchronization** with `GET_MATRIX`, `GET_SOURCE` × 3 and `GET_NAME` follows.
4. **Atomic application** of whole grids via `SET_MATRIX`, for example when loading a
   file in the editor.
5. **Throttled dragging**: while a depth is being dragged, the editor sends `SET_CELL`
   at most every 20 ms. If a reply is outstanding, it sends the latest value.
6. **Bounded load in the device**: reply queue with 2 frames (further ones are
   discarded and retried by the editor), at most one frame per `Service()`, at most one
   reply per millisecond, sending only with a configured USB host (like Toy Piano).

## 5. Sequence of a session

```text
Editor                                         Device
IDENTIFY (dev 0x7F) ─────────────────────────► every device replies with its ID
[hash unknown] GET_TARGET × 8, GET_ALLOWED ──► column headers, greyed-out cells
GET_MATRIX, GET_SOURCE × 3, GET_NAME ────────► state; compare CRC
… select a cell, drag the depth …
SET_CELL (≤ 50/s) ───────────────────────────► audible immediately; reply with CRC
… load a file …
SET_MATRIX, SET_SOURCE × 6, SET_NAME ────────► grid at once, parameters afterwards
MONITOR 20 (renewed every second) ───────────► live values for the row/cell display
```

A complete synchronization needs 15 requests. Over USB it takes
`ESTIMATE` **less than 0.1 s**.

## 6. Open points

* `OPEN`: how Windows processes the SysEx send stream of the pinned libDaisy, which
  sends `F7` as a separate packet (E9). This is the **first hardware test in phase 2**
  (TEST_PLAN § 4). If necessary, a separate framing in the adapter follows.
* `OPEN`: simultaneous port use by the editor and Studio One (E10). Phase 3.
* Later: 14-bit modwheel (CC1/CC33), MPE, NRPN as a fallback for DAWs that filter
  SysEx.
