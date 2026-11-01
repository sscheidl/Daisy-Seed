# SQ protocol v1.0 (Acid Seq)

**As of:** 27 October 2026 · **Status:** contract from phase 0, implemented in phase 1
(device side `include/step_seq/device.h`, codec `sq_protocol.h`, editor `SqSession.h` and
`web/sq_protocol.js`, `tools/sq_probe.py`).
Basis: [work order](../../../docs/acid-seq/ARBEITSAUFTRAG.md) 4.9 (R09, R10), D1.

The SQ protocol configures and observes Acid Seq and its individual firmwares
(`step-seq`, `acid-303`, `drive-fx`, `delay-fx`) via SysEx. It is deliberately separate
from the Mod on Daisy protocol `7D 4D 44` ("MD"), which remains unchanged. A shared
parameter contract applies to all components. There is no separate protocol
for Drive or Delay.

* **Reference:** [`tools/sq_codec.py`](../tools/sq_codec.py) is the reference codec.
  It generates the shared test vectors
  [`contracts/sq_vectors.json`](../contracts/sq_vectors.json), which the firmware (C++),
  the editor (C++ and JavaScript) and `sq_probe.py` check.
* **Parameters:** IDs, ranges and CC numbers are in
  [`contracts/parameters.json`](../contracts/parameters.json), explained in
  [PARAMETERS.md](PARAMETERS.md).
* **Transport:** USB MIDI. SysEx over DIN does not exist in this version (4.4).

## 1. Distinction from other messages

| Header | Meaning | Handling in an SQ firmware |
| --- | --- | --- |
| `F0 7D 53 51 …` ("SQ") | this protocol | § 3 |
| `F0 7D 42 4C …` ("BL") | bootloader request ([common/bootloader](../../../common/bootloader/README.md)) | checked before the SQ parser. Before entering, the firmware releases gates and notes (4.1) |
| `F0 7D 4D 44 …` ("MD") | Mod on Daisy | does not belong to SQ. A firmware without a Mod core discards it silently; an SQ 1.1 firmware with capability bit 6 (Streichwurst) answers it with its Mod on Daisy core (§ 8) |
| all others | foreign | discarded, not answered |

## 2. Frame format

```text
F0 7D 53 51 <ver> <type> <rid> <len> <payload: len bytes> <crc_hi> <crc_lo> F7
   │  └─┬─┘   │      │      │     │                          └─┬──────────┘
   │   "SQ"   │      │      │     │                            CRC-8 in two 7-bit bytes
   │          │      │      │     └ payload length 0…109
   │          │      │      └ request ID 0…127, assigned by the panel, rolling
   │          │      └ message type: request 0x01–0x3F, response = request | 0x40
   │          └ protocol major version, here 1
   └ SysEx manufacturer ID "non-commercial / development"
```

| Rule | Value | Reason |
| --- | --- | --- |
| Frame length | **≤ 120 bytes** including `F0` and `F7`, payload ≤ 109 bytes | The pinned libDaisy parser stores only 128 bytes between `F0` and `F7` and reports longer messages truncated, but as complete |
| Response bit | Bit 6 of `<type>` (`0x40`) | Responses, `NAK` (`0x7F`) and monitor frames (`0x7E`) carry it. They are **never answered** (echo protection, like MD with Studio One input monitoring) |
| Checksum | CRC-8, polynomial `0x07`, initial value `0x00`, without reflection and final XOR (CRC-8/SMBUS) over `<ver>` up to the last payload byte. Check value for ASCII `123456789`: `0xF4`. Transmitted as `crc_hi = crc >> 7`, `crc_lo = crc & 0x7F` | CRC-8 detects burst errors better than the MD sum. Two bytes, because SysEx data has 7 bits |
| Request ID | 0…127 | Retries repeat the ID (§ 4) |

**Interspersed real-time bytes** (`F8`–`FF`, e.g. the MIDI clock of a running DAW)
may appear in the middle of a SysEx according to the MIDI specification. The pinned
libDaisy parser (`MidiParser::Parse`) outputs them as separate events and continues
collecting the SysEx, as does `bootloader::SysExMatcher`. They therefore never reach
the SQ frame. The frame stays valid, the clock acts normally. `sq_codec.assemble` models
this, and a test vector checks it.

> **Deviation from work order 4.9 (justified):** there, frames with
> interspersed real-time bytes are to be discarded. With a running DAW clock, that would
> cost almost every panel frame (one clock every 20.8 ms at 120 BPM) and make the panel
> unusable during playback. Therefore, frames with other status bytes are discarded
> and counted. Both cases are tested.

**Acceptance of a frame** in this order (this is how `sq_codec.classify` checks it
for the frame assembled by the parser):

1. Header is not `F0 7D 53 51` → not an SQ frame (§ 1).
2. Last byte is not `F7` → `NAK LENGTH`.
3. A status byte (`0x80`–`0xF7`) between `F0` and `F7` → discarded, counter
   `status_in_frame`, `NAK CRC`.
4. Frame longer than 120 bytes → discarded, counter `too_long`, `NAK LENGTH`.
5. `<type>` has the response bit → discarded, counter `echo`, **no response**.
6. `<len>` does not match the frame length → `NAK LENGTH`.
7. `<ver>` ≠ 1 → `NAK VERSION`.
8. CRC wrong → `NAK CRC`.
9. Same request ID and same CRC as the last executed request →
   repetition: the cached response is sent again, nothing is
   executed a second time. Counter `duplicate`.
10. Unknown type → `NAK UNKNOWN_TYPE`. Otherwise the request is executed.

A `NAK` has the type `0x7F`, the request ID of the request and the payload
`u7 request type · u7 error code`. For errors in steps 2–4 and 6, the request ID is
the byte at its position, or 0 if the frame is too short for it.

| Error code | Name | Meaning |
| --- | --- | --- |
| 1 | `LENGTH` | Length, missing `F7` or frame over 120 bytes |
| 2 | `CRC` | Checksum wrong or status byte in the frame |
| 3 | `UNKNOWN_TYPE` | Type not defined |
| 4 | `BAD_ARGUMENT` | Value out of range, unknown component or parameter ID |
| 5 | `BUSY` | Hand-over queue full, retry later |
| 6 | `VERSION` | Protocol major version not supported |
| 7 | `UID_MISMATCH` | Calibration or enable for a different kit |
| 8 | `NOT_CALIBRATED` | CV enable without valid calibration of both outputs |
| 9 | `NOT_ALLOWED` | not allowed in the current state (e.g. start without enable) |

**Payload encoding** (all bytes 7 bit, multi-byte values LSB first):

| Type | Bytes | Encoding |
| --- | --- | --- |
| `u7` | 1 | direct |
| `u14` | 2 | 7 bits each |
| `u32` | 5 | 32 bits in 5 × 7 bits, the last byte ≤ `0x0F` |
| `i32` | 5 | like `u32`, two's complement |
| `str` | 1 + n | length `u7`, then printable 7-bit ASCII. The panel shows umlauts from its own texts |
| `step` | 3 | `u7` note 0…59 (octave × 12 + semitone) · `u7` gate length 5…100 % · `u7` bits (0 gate on, 1 accent, 2 slide) |

**Parameter values** are `i32` in thousandths of the unit (`value × 1000`).
Example: 120 BPM = `120000`, −1.5 dB = `−1500`, enum index 2 = `2000`.

## 3. Messages

### 3.1 Identity and components

| Type | Name | Request | Response (`type \| 0x40`) |
| --- | --- | --- | --- |
| `0x01` | `IDENTIFY` | — | `u7` major, `u7` minor version of the protocol · `str` product (≤ 16, e.g. `step-seq`, `acid-seq`) · `str` product version (≤ 12) · `u32` boot ID · 3 × `u32` STM32 UID (word 0, 1, 2) · `u14` capabilities · `u7` number of components |
| `0x02` | `GET_COMPONENT` | `u7` index | `u7` index · `u7` count · `str` component ID (≤ 16, from 1.1 ≤ 24, as in the register) · `u7` kind (0 sequencer, 1 instrument, 2 effect, 3 DSP, from 1.1: 4 product) · `str` version (≤ 12) · `u7` schema version of the parameters · `u7` number of parameters |
| `0x03` | `GET_PARAM_DESC` | `u7` component · `u7` parameter index | `u7` component · `u7` index · `u7` count · `u14` parameter ID · `u7` type · `u7` unit · `u7` flags · `i32` min · `i32` max · `i32` default · `u7` CC (127 = none) · `str` key (≤ 16) |
| `0x04` | `GET_ENUM_LABEL` | `u7` component · `u14` parameter ID · `u7` value | the same fields · `str` key of the value (≤ 16) |

* **Boot ID:** random per start-up. If it changes, the device has restarted, and
  the panel reads the state again.
* **Capabilities:** bit 0 CV outputs, bit 1 DIN MIDI, bit 2 internal voice, bit 3
  pattern, bit 4 monitor, bit 5 calibration, from 1.1 bit 6 Mod on Daisy on the same port (§ 8).
* **Fragments:** lists are queried via index and count (components,
  descriptors, `GET_PARAMS`). Each frame carries the index and the total count so that the
  panel detects gaps.
* Type, unit and flags follow the lists `types`, `units` and `flags` in
  `parameters.json` (index = code; flags: bit 0 read-only, bit 1 logarithmic).

### 3.2 Parameters

| Type | Name | Request | Response |
| --- | --- | --- | --- |
| `0x05` | `GET_PARAMS` | `u7` component · `u7` first index | `u7` component · `u7` first index · `u7` total count · `u7` n · n × (`u14` parameter ID · `i32` value), at most 14 per frame |
| `0x06` | `SET_PARAM` | `u7` component · `u14` parameter ID · `i32` value | `u7` status (0) · `u14` parameter ID · `i32` accepted value (clamped and quantised) · `u14` accepted revision |

Parameters are applied at the block boundary. A change on the kit (pot, switch)
appears in the monitor. For pots: the last change wins. A pot only writes
once it moves beyond its hysteresis.

### 3.3 Pattern

| Type | Name | Request | Response |
| --- | --- | --- | --- |
| `0x10` | `GET_PATTERN` | — | `u14` accepted revision · `u14` applied revision · 16 × `step` (52 bytes) |
| `0x11` | `SET_PATTERN` | 16 × `step` (48 bytes) | `u7` status · `u14` accepted revision |
| `0x12` | `SET_STEP` | `u7` step 0…15 · `step` | `u7` status · `u14` accepted revision |

* A pattern fits into one frame and is applied **atomically**. `main` places it in
  an SPSC queue or a triple buffer; the callback applies it **at the next
  step boundary** (3.2 of the work order).
* **Revisions:** every accepted change (pattern, step, parameter) increments the
  accepted revision (14 bits, rolling). The applied revision reports what the
  callback actually plays. The panel thus knows when its change is audible.
* Invalid steps (note > 59, gate length outside 5…100, unknown bits) reject the
  whole frame (`NAK BAD_ARGUMENT`).

### 3.4 Transport and state

| Type | Name | Request | Response |
| --- | --- | --- | --- |
| `0x20` | `TRANSPORT` | `u7` command (0 Stop, 1 Start) | `u7` status · `u7` state |
| `0x21` | `GET_STATE` | — | `u7` state (0 STOP, 1 WARTET (waiting), 2 LÄUFT (running), 3 PAUSE) · `u7` mode · `u7` SW17 (0 Stop, 1 Run) · `u7` SW18 (0 up, 1 centre, 2 down) · `u7` active clock source (0 internal, 1 Gate IN 1, 2 MIDI USB, 3 MIDI DIN) · `u7` current step (0…15, 127 none) · `i32` tempo in milli-BPM · `u14` accepted · `u14` applied revision · `u7` CV enabled (0/1) · `u7` USB connected (0/1) |

Panel start and stop only take effect when enabled (SW17 on Run, see 4.3 of the work order),
otherwise `NAK NOT_ALLOWED`.

### 3.5 Calibration and CV enable

| Type | Name | Request | Response |
| --- | --- | --- | --- |
| `0x28` | `GET_CAL` | `u7` output (0 CV OUT 1, 1 CV OUT 2) | `u7` output · `u7` valid · `i32` offset in milli-code at 0 V · `i32` scale in milli-code per volt · `u14` gate-low code · `u14` gate-high code · `u14` clamp min · `u14` clamp max |
| `0x29` | `SET_CAL` | 3 × `u32` UID · `u7` output · the same six fields | `u7` status |
| `0x2A` | `CV_ENABLE` | 3 × `u32` UID · `u7` enabled (0/1) | `u7` status · `u7` enabled |

* **Code** = offset + volts × scale, rounded and limited to the clamp. A
  negative scale describes an inverting output stage.
* **Limits:** codes 0…4095. |scale| 100 000…2 000 000 milli-code/V. Clamp
  min < max. Gate low and gate high within the clamp and different. Other values
  are rejected (`NAK BAD_ARGUMENT`).
* **UID:** if it does not match the kit, `NAK UID_MISMATCH`. The panel and `sq_probe.py`
  store calibration data per UID and send it only to that kit.
* **Separate enable:** `SET_CAL` revokes an existing enable. `CV_ENABLE`
  requires valid data for both outputs (`NAK NOT_CALIBRATED`). The enable is
  never stored and never restored automatically. If it is withdrawn while the gate is open,
  CV OUT 2 immediately goes to the gate-low code.
* Calibration data lives only in RAM (no flash without the user's approval).

### 3.6 Monitor and diagnostics

| Type | Name | Request | Response |
| --- | --- | --- | --- |
| `0x30` | `MONITOR` | `u7` on (0/1) | `u7` on |
| `0x31` | `GET_STATS` | — | `u7` n · n × `u32` counters (§ 5) |
| `0x32` | `RESET_STATS` | — | `u7` status |

* The monitor sends unsolicited frames `0x7E` with a `u7` event code and data. The
  request ID counts the monitor frames (rolling) so that the panel detects losses.

| Code | Event | Data |
| --- | --- | --- |
| 1 | Step change | `u7` step · `u7` gate on |
| 2 | State | like the first five fields of `GET_STATE` |
| 3 | Tempo | `i32` milli-BPM |
| 4 | Revision | `u14` accepted · `u14` applied |
| 5 | Parameter | `u7` component · `u14` parameter ID · `i32` value (pot or CC change on the device) |
| 6 | Counters changed | — (the panel reads `GET_STATS`) |
| 7 | Step changed on the kit | `u7` step · `step` (pot or button; one frame per changed step) |

* **At most 50 frames per second.** Events of the same kind are coalesced;
  the most recent wins. Parameters are coalesced per component and parameter:
  if several parameters change before the next frame, each gets its own
  frame with its latest value. Step changes have priority.
* The monitor ends **3 s after the last `MONITOR` request**. The panel renews it
  every second.
* Apart from monitor frames, the device never sends unsolicited.

## 4. Reliability (panel and `sq_probe.py`)

* Timeout **200 ms**, at most **3 retries** with the same request ID and
  the same bytes. After that the request counts as lost (counter in the panel).
* The device handles retries idempotently (§ 2, step 9).
* Responses with an unexpected request ID or unexpected type are discarded and
  counted.
* After every connect, the panel first reads the device state. **The device takes
  precedence** (D13). Data is only sent from the panel to the device on explicit action.

## 5. Counters (`GET_STATS`)

| No. | Counter |
| --- | --- |
| 0 | `bad_length` |
| 1 | `bad_crc` |
| 2 | `bad_version` |
| 3 | `unknown_type` |
| 4 | `too_long` |
| 5 | `status_in_frame` (frame with a status byte between `F0` and `F7`) |
| 6 | `duplicate` |
| 7 | `echo` (frame with the response bit) |
| 8 | `busy` |
| 9 | `monitor_dropped` |
| 10 | `note_on_dropped` (queue full, 4.4) |
| 11 | `midi_queue_overflow` |
| 12 | `clock_ignored` (clock from the port not selected) |
| 13 | `callback_overruns` |
| 14 | `callback_peak_cycles` |
| 15 | `queue_high_water` |
| 16 | `stack_free_bytes` (never used stack reserve, stack probe) |

Further counters are appended at the end. A client reads `n` and ignores
unknown ones.

## 6. Test vectors

`contracts/sq_vectors.json` contains valid frames (with type, request ID and
payload) and invalid frames with the expected result of the acceptance:
flipped bit, wrong length, status byte in the frame, version 2, frame over 120 bytes,
unknown type, MD and bootloader frames. A vector with the field `stream` contains
interspersed real-time bytes (`F8 FA F8`). After `assemble` it must yield the valid frame.
Every implementation must classify all
vectors in the same way. The Python contract test
(`tests/test_acid_seq_contracts.py`) checks the vectors against the reference codec.

## 7. Changes

A change to this protocol happens only together with the firmware, `sq_codec.py`
or `sq_probe.py`, both editors and their tests. New messages and
appended fields increase the minor version, incompatible changes the
major version.

## 8. SQ 1.1 (Streichwurst, 01.11.2026)

Compatible extension for firmware products that connect the instrument, effects and the
Mod on Daisy matrix on **one** USB port. Each device reports the minor version
itself in IDENTIFY (`sq::Device::protocol_minor`); the Acid Seq firmwares report
1.0 unchanged and send byte-identical frames.

| Extension | Meaning |
| --- | --- |
| IDENTIFY capability bit 6 (`CapModMatrix`) | The device also answers Mod on Daisy v1 (`F0 7D 4D 44`, [PROTOCOL.md](../../../modules/mod-on-daisy/docs/PROTOCOL.md)) on the same port. The editor starts its MD session only after the SQ state has been read, and reads first there as well. |
| GET_COMPONENT kind 4 (`KindProduct`) | Parameters of the firmware product itself (mix, MIDI channel, state on the kit), separate from the instrument and effects |
| Component ID up to 24 characters | IDs such as `streichwurst-instrument` match the component register |
| GET_PATTERN only with bit 3 | Panel and probe read the pattern only on devices with a sequencer; GET_STATE also returns the accepted and applied revision there |

A 1.0 editor still sees a 1.1 device as an SQ device with parameter pages; it only lacks
the matrix. A 1.1 editor operates 1.0 devices as before. No message type, no
error code and no field of existing frames was reinterpreted. Parameters, ranges and
CCs of Streichwurst are in
[instruments/streichwurst/contracts/parameters.json](../../../instruments/streichwurst/contracts/parameters.json);
`tools/gen_params.py` generates the firmware table and the editor labels from it.
