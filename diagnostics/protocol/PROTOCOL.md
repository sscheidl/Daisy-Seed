# Daisy Diagnostic Protocol (DDP) — Version 1

Bidirectional text protocol between Seed3 diagnostic firmware and the Windows
application. Transport: **USB CDC (virtual COM port)** through **J7**, the external
USB-C connector on the Desktop Dev Kit (OTG-HS peripheral in FS mode, D29/D30).
Baud rate and modem handshake lines have no meaning for USB CDC.

Reference implementations: [`ddp.h`](ddp.h)/[`ddp.cpp`](ddp.cpp), portable C++17
without libDaisy, and [`protocol.py`](../windows/daisy_diag/protocol.py) on the host.
[`vectors.txt`](vectors.txt) contains shared test vectors.

## Why not the libDaisy logger?

`daisy::Logger<LOGGER_EXTERNAL>` at libDaisy `f715e48` can transmit (`Print`,
`PrintLine`) but has no receive API. `TransmitSync()` waits for the host after the
first packets. The firmware therefore uses `UsbHandle` directly with
`Init(FS_EXTERNAL)`, `SetReceiveCallback(..., FS_EXTERNAL)`, and nonblocking
`TransmitExternal()`. It uses the same CDC class; no custom CDC implementation is needed.

Default USB identity: VID `0x0483`, PID `0x5740`, product **Daisy Seed External**.
Other STM32 devices share this identity, so a port becomes a diagnostic device only
after a successful HELLO handshake.

## Frame format

```text
$<body>*<CRC>\n
```

* `body`: printable ASCII `0x20..0x7E`, excluding `$` and `*`; maximum **400 bytes**.
* `CRC`: CRC-16/CCITT-FALSE over the body bytes: polynomial `0x1021`, initial value
  `0xFFFF`, no reflection, no final XOR. Encode as four uppercase hexadecimal digits.
  Check value: `CRC("123456789") = 29B1`.
* A `\r` before `\n` is accepted. Receivers also accept lowercase CRC digits.

Receiver resynchronization rules:

1. Discard bytes outside a frame until `$` appears.
2. `$` inside a frame discards the incomplete frame and starts a new one.
3. Invalid characters, a missing `*`, invalid/wrong CRC, and a body longer than
   400 bytes discard the frame and increment the error counters.

This handles truncated frames after disconnection, buffered old data when reopening
a port, and noise bytes.

## Message structure

```text
TYPE key=value key=value ...
```

`TYPE` is uppercase. Keys are lowercase; values contain neither spaces nor `=`.
Lists use commas. **Unknown keys are ignored**; the host also ignores unknown
message types. Firmware answers unknown commands with `ERR`. New optional fields
therefore do not require a protocol version change; incompatible changes increment `proto`.

## Connection sequence

```text
Host                                    Seed3
 | -- $HELLO proto=1*... ------------>  | enable streaming, start host timer
 | <- $HELLO proto=1 fw=... ----------  |
 | <- $ST ... (configured rate) ------  |
 | <- $SYS ... (1 Hz) ----------------  |
 | -- $PING n=17*... (2 Hz) ----------> |
 | <- $PONG n=17*... -----------------  |
 | -- $LED m=CHASE*... --------------> |
 | <- $ACK c=LED m=CHASE ... ---------  |
```

* Streaming starts after HELLO and continues while a valid host frame arrives
  within **3 s**. After timeout or BYE, only a new HELLO restarts streaming; PING
  still receives PONG but does not restart it. Time spent in a requested SD test
  does not count as host silence. An arbitrary terminal gets no unsolicited stream.
* BYE receives `ACK c=BYE`. A new HELLO discards pending output from the previous session.
* The host sends PING every 0.5 s. It disconnects after **1.5 s** without a valid
  frame or when the port disappears, then searches every second and repeats HELLO.
  An explicitly requested SD test gets a separate timeout of up to **20 s**.
* J7 powers the kit; unplugging it also restarts the firmware. Reconnection starts
  with HELLO.
* USB sends are nonblocking. While CDC holds one buffer, the next frames collect
  in a second buffer. New frames that do not fit are dropped and counted in `SYS txdrop`.

## Host → firmware

| Command | Fields | Response |
| --- | --- | --- |
| `HELLO` | `proto=1` | `HELLO ...`, then streaming |
| `PING` | `n=<u32>` | `PONG n=<u32>` |
| `GET` | — | Immediate `ST` and `SYS` in an active session |
| `RATE` | `hz=10..100` | `ACK c=RATE hz=<n>` |
| `LED` | `m=OFF\|ON\|MASK\|CHASE\|FOLLOW`, optional `mask=<hex4>`, `br=0..100`, `ms=20..2000` | `ACK c=LED m=... mask=... br=... ms=...` |
| `CVOUT` | `m=OFF\|FIX\|RAMP\|SAW`, optional `c1=0..4095`, `c2=0..4095`, `ms=50..10000` | `ACK c=CVOUT m=... c1=... c2=... ms=...`; `ERR c=CVOUT e=SESSION` without HELLO (firmware 0.3.0) |
| `SDTEST` | — | `SD ...` (read-only test); firmware 0.4.0 switches CV OUT and the test tone off before it blocks |
| `TONE` | `m=OFF\|SINE`, optional `f=20..20000`, `a=0..5000`, `ch=L\|R\|LR` | `ACK c=TONE m=... f=... a=... ch=...`; `ERR c=TONE e=SESSION` without HELLO (firmware 0.4.0, capability `audio`) |
| `SCOPE` | optional `n=256..2048`, `t=0\|1` | `ACK c=SCOPE n=... t=... k=<chunks>` then `k` `SC` frames; `ERR c=SCOPE e=SESSION`, `e=BUSY` while a capture runs, `e=UNSUPPORTED` without audio (firmware 0.4.0) |
| `BOOT` | — | `ACK c=BOOT`, then the firmware restarts into the Daisy bootloader (no timeout); `ERR c=BOOT e=SESSION` without HELLO, `e=BUSY` while Windows owns the card |
| `BYE` | — | `ACK c=BYE`, streaming off |

Errors: `ERR c=<command> e=<code>`, where `UNKNOWN` means unknown command, `ARG`
means a missing/invalid field (including an overlong optional value; the whole
command is rejected), and `PROTO` means unsupported protocol version. Unsigned
numbers contain digits only, without signs or underscores; the host rejects state
values outside the specified ranges. ERR is generated only for complete frames
with a valid CRC.

| LED mode | Meaning |
| --- | --- |
| `OFF` | All 16 LEDs off |
| `ON` | All 16 LEDs on |
| `MASK` | Individual LEDs: bit 0 = D1 … bit 15 = D16 (`mask=8001` selects D1 and D16) |
| `CHASE` | D1 → D16, one step every `ms` (default 120 ms) |
| `FOLLOW` | LED follows its button's held state; default after startup |
| `POTDIM` | All 16 LEDs on, brightness follows VR1 (0..100 %); ST `br` reports the effective value (firmware 0.4.0) |

`br` sets PWM brightness in percent (default 100). LEDs have **no electrical
feedback**: ST field `l` is the commanded state, not proof of actual light output.

| CV OUT mode | Meaning (DAC codes, 12 bit) |
| --- | --- |
| `OFF` | Both outputs code 0; default after startup **and after every session end** (BYE or 3 s timeout) |
| `FIX` | CV OUT 1 = `c1`, CV OUT 2 = `c2` |
| `RAMP` | Triangle 0 → 4095 → 0 with period `ms` (default 1000 ms); CV OUT 2 = 4095 − CV OUT 1 |
| `SAW` | Rising sawtooth 0 → 4095, reset every `ms`; CV OUT 2 = 4095 − CV OUT 1 |

Absent optional fields keep their previous value. Codes are not volts: the
analog scaling of the outputs has not been measured.

## Firmware → host

### HELLO

```text
HELLO proto=1 fw=0.3.0 app=DaisyDiagnostic dev=seed3-desktop boot=BOOT_SRAM bl=v6_1 libdaisy=f715e48 caps=btn,pot,potraw,sw17,sw18,sd,sdread,jack,led,boot,cvin,gate,cvout
```

`cvin`, `gate` and `cvout` (firmware 0.3.0) announce the CV/gate fields in `ST` and
the `CVOUT` command. `audio` (firmware 0.4.0) announces `TONE`, `SCOPE`/`SC`, the
`AU` frames and the LED mode `POTDIM`; `fw=0.4.0` HELLOs end in `…,cvout,audio`.

`bl` is the bootloader version recognized by libDaisy: `v6_1` means v6.1 or newer;
other values are `v6_0`, `LT_v6_0`, and `NONE`.

`boot` in `caps` means the firmware accepts `BOOT`; images built before this
command was added do not advertise it. The firmware sends the ACK, waits up to 50 ms for it to leave, lights
all 16 LEDs and resets through libDaisy's `DAISY_INFINITE_TIMEOUT` mode
([common/bootloader](../../common/bootloader/README.md)). The CDC port then
disappears; the bootloader appears as DFU device `0483:DF11`. Hosts without `boot`
in `caps` do not offer the command. `ACK c=BOOT` confirms the accepted request,
not the DFU entry; only the DFU device appearing shows that. Holding S9 + S16 for 5 s on the kit does the
same without a host and without a reply; the stream simply ends.

### ST — complete state

```text
ST n=1234 t=56789 b=0001 pc=01000000000000000000000000000000 p=0,10000,5000,0,0,0,0,0 r=0,65535,32768,0,0,0,0,0 s17=U s18=C sd=0 jl=0 jr=1 lm=FOLLOW l=0001 br=100
```

| Field | Meaning |
| --- | --- |
| `n` | Sequence number (u32); gaps indicate lost frames |
| `t` | Firmware uptime in milliseconds (u32) |
| `b` | Debounced button mask: bit 0 = S1 … bit 15 = S16 |
| `pc` | 16 press counts, two hex digits each, modulo 256, S1 first; retains short presses between frames |
| `p` | VR1…VR8 normalized values: `0..10000` = `0.0000..1.0000` |
| `r` | VR1…VR8 raw ADC values `0..65535`, mux channels 0…7 |
| `s17` | SW17: `U` or `D`; libDaisy `tog_sw17.Pressed()` maps to `U` |
| `s18` | SW18: `U`, `C`, or `D`; libDaisy `POS_UP`, `POS_CENTER`, `POS_DOWN` |
| `sd` | microSD detection: `1` = card detected |
| `jl`, `jr` | Left/right audio input plug detected (`1`); outputs have no plug detection |
| `lm` | Active LED mode |
| `l` | Commanded LED mask, bit 0 = D1 |
| `br` | LED brightness, `0..100` percent |

Firmware 0.3.0 appends a CV/gate block. It is either complete or absent; hosts
must accept frames without it (older firmware):

```text
ST ... br=100 cv=2500,-5000 cvr=40960,16384 g=10 gc=03FF co=FIX,1000,4095
```

| Field | Meaning |
| --- | --- |
| `cv` | CV IN 1/2 normalized, `-10000..10000` = `-1.0..+1.0` (libDaisy bipolar `AnalogControl`); not volts |
| `cvr` | CV IN 1/2 raw ADC values `0..65535` (ADC channels 1/2, pins D20/D21) |
| `g` | GATE IN 1/2 state, one `0`/`1` digit each, GATE 1 first |
| `gc` | Rising-edge counts, two hex digits each (modulo 256), GATE 1 first |
| `co` | CV OUT mode and the two DAC codes currently written (`0..4095`) |

Gates are sampled once per 1 kHz control tick (pins D0/D18, libDaisy `GateIn`
polarity); pulses shorter than 1 ms can be missed. CV values are sent with the
periodic frames; gate changes also trigger an immediate frame.

Periodic rate is selected by RATE (default **50 Hz**). Digital input changes
trigger additional frames with a minimum 10 ms interval.

### SYS — firmware diagnostics, 1 Hz

```text
SYS up=123 cbhz=1000 txok=6150 txbusy=3 txdrop=0 rxok=250 rxbad=0 host=1 rate=50
```

`cbhz` is measured control updates per second (target 1000, from 48 kHz / 48 samples).
`rxbad` reports discarded receive data/frames. Remaining fields expose uptime,
transmit/receive counters, host-active state, and configured rate.

### SD — SDTEST result

```text
SD st=MOUNTED res=0 fs=FAT32 mb=30436 ent=12
SD st=NONE res=-1
SD st=ERROR res=13
```

| `st` | Meaning |
| --- | --- |
| `NONE` | No card detected; no access attempted |
| `MOUNTED` | Filesystem mounted and root directory read: `ent` entries, capped at 1000 |
| `ERROR` | Access failed; `res` is FatFs FRESULT, e.g. `3` = FR_NOT_READY, `13` = FR_NO_FILESYSTEM |

The test uses only `f_mount`, `f_opendir`, `f_readdir`, `f_closedir`, and unmounting,
over a custom write-protected driver (`STA_PROTECT`). It does not write, format,
or modify the card. The default diagnostic build rejects SD write/erase/lock
commands at SDMMC, HAL, driver, and FatFs levels. The separate card-reader build
allows polled SD block writes only for MSC, and rejects SDTEST whenever ownership
is not LOCAL. See [FIRMWARE.md](../docs/FIRMWARE.md).

Streaming pauses during the test (typically < 0.5 s). Reads have a five-second
budget plus initialization; the host tolerates up to 20 s. Press counts retain
button presses during the pause. Each test reinitializes the card, supporting swaps.

### ACK, ERR, PONG

See the host-to-firmware command table above.

## Audio extension (firmware 0.4.0, capability `audio`)

Firmware with `audio` in `caps` carries a test tone generator, peak meters and an
input snapshot. All values are digital (fraction of full scale or int16 samples);
the analog levels of the codec, jacks and headphone amplifier are not measured.

### TONE — test tone on AUDIO OUT

`TONE m=SINE f=1000 a=1000 ch=LR` starts a sine of `f` Hz with amplitude `a` in
1/10000 of full scale (`a=1000` ≈ −20 dBFS, default; the firmware caps `a` at
**5000** ≈ −6 dBFS) on the left, right or both outputs. Absent optional fields
keep their previous value; `m=OFF` silences the outputs. The tone needs a session
and ends with it (BYE, 3 s timeout). Independently of the main loop, the audio
callback mutes the tone when the main loop has not refreshed it for 1 s, so a
blocked main loop (SD test, card-reader initialisation) or a crashed firmware
cannot leave a tone on; the firmware also switches it off explicitly before SDTEST.

### AU — peak levels, 10 Hz

```text
AU il=10000 ir=0 ol=5000 or=1 tone=SINE,1000,1000,LR
```

| Field | Meaning |
| --- | --- |
| `il`, `ir` | Peak |sample| of AUDIO IN left/right since the previous AU frame, `0..10000` = `0.0..1.0` FS |
| `ol`, `or` | Same for the samples written to AUDIO OUT (the tone, before the codec) |
| `tone` | Active tone: mode, frequency, amplitude, channels |

Sent every 100 ms while a session is active (and with every `GET`). Hosts convert
to dBFS as `20·log10(value/10000)`; `0` is digital silence.

### SCOPE / SC — input snapshot

`SCOPE n=1024 t=1` arms a capture of `n` stereo input frames (256..2048, i.e.
5–43 ms at 48 kHz). With `t=1` the capture starts at the next rising zero crossing
of the left input, or after 200 ms without one; `t=0` starts immediately. The
firmware answers `ACK c=SCOPE n=1024 t=1 k=26` and, once the callback has filled
the buffer, sends `k` chunks:

```text
SC i=0 k=26 n=1024 d=00F0FF1000F1FF0F…
```

`d` holds 40 stereo frames per chunk (the last one fewer), each frame as left
then right int16 in two's-complement hex (`FFCE` = −50). Chunks are sent in order,
a few per main-loop step, interleaved with the normal stream; the host reassembles
`k` chunks with equal `n`. A second `SCOPE` while one capture is pending is
answered with `ERR c=SCOPE e=BUSY`; builds without audio answer `e=UNSUPPORTED`.
A capture that is still open when the session ends is dropped.

Frequency and level derived from a snapshot (zero crossings, peak) are host-side
measurements of the digital input; with a cable from AUDIO OUT to AUDIO IN and the
tone running they form the audio loopback check.

## Versioning

`proto=1` identifies this specification. A host receiving another major version
shows **Incompatible protocol** and sends no control commands.

## Optional card reader extension (firmware/application 0.2.0)

Since 0.3.1, reader status may also carry diagnostic fields: `io` (NONE/READ/WRITE/
SYNC), `fault` (0=none, 1=unavailable, 2=start failed, 3=DMA error, 4=DMA timeout,
5=programming timeout, 6=alignment, 7=card status error since 0.3.2), `he` (HAL error bits captured before abort),
`lba` and `ms` (failed operation elapsed milliseconds). Hosts may ignore them;
the existing ownership fields and action acknowledgement are unchanged. The 0.3.2
host retains all five fields, displays them and includes them in saved JSON reports.
The diagnostic group is complete or absent for compatibility with older firmware.

The writable firmware advertises `sdmsc` in HELLO capabilities. The protected
build does not. The DDP v1 framing, CRC and existing fields remain unchanged.
Older hosts ignore the additional message; newer hosts disable reader controls
when `sdmsc` is absent. USB MSC is a separate interface, not a DDP file tunnel.

Commands require an active HELLO session:

| Command | Meaning |
| --- | --- |
| `READER a=STATUS` | Query authoritative ownership/transport status |
| `READER a=SHARE` | From LOCAL with a detected card and idle BOT transport: initialize and expose LUN 0 |
| `READER a=RETURN` | From EJECTED, INTERRUPTED or ERROR with idle BOT: close/reset SD interface and return to LOCAL |

Success body (also sent with SYS while streaming, once per second):

```text
READER state=SHARED sectors=62332928 busy=0 a=STATUS
```

Required fields: `state` is LOCAL / STARTING / SHARED / EJECTED / INTERRUPTED /
ERROR; `sectors` is a decimal uint32 count of 512-byte physical card sectors;
`busy` is 0 or 1 (BOT not waiting for a new command); `a` is STATUS / SHARE / RETURN.
Direct responses echo the action; periodic status uses STATUS. A periodic status
must not acknowledge a different outstanding action. Error bodies are
`ERR c=READER e=ARG|SESSION|STATE|UNSUPPORTED`; SDTEST during non-local ownership
returns `ERR c=SDTEST e=BUSY`.

No implicit return occurs on BYE, missing PING, application close or serial-port
reconnect. On HELLO the host queries STATUS; it never automatically repeats SHARE.
Requested reader commands tolerate 20 s; expiration means unknown ownership,
not successful eject. While SHARED, the host tolerates a five-second receive gap
because a DMA wait plus card-programming wait can take two seconds; SDK command
response deadlines are separate from these main-loop budgets. Other
normal receive gaps retain the 1.5 s timeout.

SHARED exposes a removable read/write LUN. A successful SCSI START STOP UNIT
(LoEj=1, Start=0) synchronizes the card and sets EJECTED. Windows still owns any
host-side caches until it issues its eject; SYNCHRONIZE CACHE alone does not
return ownership. PREVENT MEDIUM REMOVAL blocks eject until allowed again.
Bus/BOT reset, removal or SD I/O failure quarantines the medium (INTERRUPTED or
ERROR), not cleanly ejected. RETURN in these states is explicit acknowledgement
of interrupted access; the application asks for confirmation and does not claim
to recover data. Newly shared media report UNIT ATTENTION / MEDIUM CHANGED.

See [reader instructions](../docs/SD_CARD_READER.md) and
[implementation/review handover](../docs/SD_CARD_READER_REVIEW.md).
