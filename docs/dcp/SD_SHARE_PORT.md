# SD share port: Python reference to C++ core (Daisy Control Panel 0.5.0)

Phase 1 of [SD_PAGE_WORK_ORDER.md](SD_PAGE_WORK_ORDER.md). This document pins down what the
C++ core `editors/mod-on-daisy/src/core/SdShare*.h` has to do, taken from the Python reference
Daisy Diagnostic 0.6.0 (`diagnostics/windows/daisy_diag/nebulae.py`, `looper.py`,
`session.py`, `protocol.py`, `gui/main_window.py`) and from the protocol documents
([Nebulae](../../instruments/granular/docs/CONTROL_PROTOCOL.md),
[Looper](../../instruments/looper/docs/SD_MANAGEMENT.md),
[DDP](../../diagnostics/protocol/PROTOCOL.md), [Nebulae profile](../../diagnostics/docs/NEBULAE_PROFILE.md),
[Looper profile](../../diagnostics/docs/LOOPER_PROFILE.md)). Where the Python code and the
documents differ, section 9 says so. Card safety rules are marked **CS**; the C++ port keeps
every one of them exactly.

Status 2026-11-08: specification. Hardware NOT MEASURED (the Python reference itself is
simulation-tested only).

## 1. Files and layering

| C++ file (`editors/mod-on-daisy/src/`) | Ports | Depends on |
| --- | --- | --- |
| `core/SdShareProtocol.h` | `nebulae.py` codec, `looper.py` codec, `DeviceProfile`, texts | nothing (header-only, no JUCE) |
| `core/Ddp.h` | `protocol.py`: CRC-16, framing, `FrameParser`, `parse_message`, HELLO/READER/NEB/LOOPER/ST(`sd`) | nothing |
| `core/DdpSession.h` | `session.py` `DeviceSession`, reduced to what the SD share needs (HELLO, PING, ST, SYS ignored, READER, NEB, LOOPER, ERR, BYE) | `Ddp.h` |
| `core/SdShare.h` | `nebulae.py` `NebulaeWorkflow` as `SdShareWorkflow` | `SdShareProtocol.h`, `DdpSession.h` |
| `core/SdShareSim.h` | `nebulae_sim.py`, `looper_sim.py`, the reader/HELLO/PING/BYE/READER part of `simulator.py` (`SimulatedDevice`, `SimulatedCable`, `SimulatedLink`) | the three above |
| `core/SerialTransport.h` | phase 3: platform-free I/O thread with bounded queues around a backend (`Open/Read/Write/Cancel/Close`) | nothing |
| `ui/SerialPortWin.h` | phase 3: Win32 backend (overlapped `ReadFile`/`WriteFile`) and SetupAPI enumeration with VID/PID | `SerialTransport.h`, Windows SDK only |
| `ui/SdSharePanel.h` | phase 4: the SD card page | JUCE, `SdShare.h` |

Clock: every core class takes `uint32_t now_ms` like `Session`, `SqSession` and `BootRequest`
(the Python reference uses float seconds). Transport and ports are abstract interfaces so
tests inject fakes; the Win32 code lives only in `ui/` and compiles only on Windows.

## 2. Messages and bytes

### 2.1 Performance personality: control SysEx over USB-MIDI

Prefix `F0 7D` + two owner bytes, version `01`, command, payload, `F7`. Reply command =
request command `| 0x40`.

| Owner | Nebulae (`NB`) | Looper (`LP`) |
| --- | --- | --- |
| Prefix | `F0 7D 4E 42` | `F0 7D 4C 50` |
| STATUS request | `F0 7D 4E 42 01 01 F7` | `F0 7D 4C 50 01 01 F7` |
| STATUS reply | `F0 7D 4E 42 01 41 <19 bytes> F7` | `F0 7D 4C 50 01 41 <13 bytes> F7` |
| SHARE request | `F0 7D 4E 42 01 02 <confirm 0/1> <t0 t1 t2 t3> F7` | `F0 7D 4C 50 01 02 <confirm> <t0..t3> F7` |
| SHARE reply | `F0 7D 4E 42 01 42 <result> <t0..t3> F7` | `F0 7D 4C 50 01 42 <result> <t0..t3> F7` |
| ERROR reply | `F0 7D 4E 42 01 7F <cmd> <code> F7` | `F0 7D 4C 50 01 7F <cmd> <code> F7` |

Token: four host-chosen 7-bit bytes; a reply whose token differs from the open request is
stale or foreign and is ignored (**CS**: a share reply is only accepted for the request the
host made). `ShareRequest` rejects a token byte > `0x7F`.

STATUS reply payload, Nebulae (19 bytes):

| Byte | Field |
| --- | --- |
| 0–2 | firmware major, minor, patch |
| 3 | personality, must be `0` (otherwise the message is rejected: "STATUS from an unknown personality") |
| 4 | flags: bit0 card, bit1 live loop in RAM, bit2 recording/finalizing, bit3 sample loading, bit4 overrun latched, bit5 bank scan |
| 5 | selected bank (0 none) |
| 6 | number of banks |
| 7, 8 | playing bank, playing slot (0 none) |
| 9–11 | bank mask, 16 bit in 7-bit groups: `b0 | b1 << 7 | (b2 & 3) << 14` |
| 12–14 | slot mask, same encoding |
| 15 | last load error: 0 none, 1 missing file, 2 invalid WAV, 3 too large, 4 SD I/O, 5 no card |
| 16–17 | overruns, 14 bit: `b16 | b17 << 7` |
| 18 | callback peak percent |

`busy` = recording or loading.

STATUS reply payload, Looper (13 bytes):

| Byte | Field |
| --- | --- |
| 0–2 | firmware |
| 3 | personality, must be `0` |
| 4 | flags: bit0 card, bit1 loop, bit2 unsaved, bit3 saving, bit4 recording, bit5 importing, bit6 overrun |
| 5 | bit0 last save failed |
| 6 | fault: 0 none, 1 card not ready, 2 write failed, 3 looper busy, 4 no WAV files, 5 file rejected |
| 7–9 | loop ms, 21 bit: `b7 | b8 << 7 | b9 << 14` |
| 10–11 | overruns, 14 bit |
| 12 | callback peak percent |

`busy` = recording or importing.

SHARE results: `0` ACCEPTED, `1` CONFIRM_REQUIRED, `2` BUSY, `3` NO_CARD, `4` BAD_REQUEST,
`5` SAVING (Looper only; for Nebulae code 5 is reported as `CODE5` and treated as a refusal).
ERROR codes: `1` VERSION, `2` UNKNOWN, `3` LENGTH.

Rejection rules of the parser (one function per profile, as in Python): length < 8, wrong
prefix, last byte not `F7`, any body byte > `0x7F`, version ≠ 1, STATUS length ≠ 19/13,
personality ≠ 0, SHARE length ≠ 5, ERROR length ≠ 2, unknown command. A message rejected by
both profiles is foreign (Mod on Daisy monitor frames, SQ, bootloader) and is skipped without
an error.

### 2.2 SD management personality: DDP v1 over USB CDC

Frame: `$` body `*` CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) as four upper-case hex
digits, `\n` (a `\r\n` is accepted on receive). Body: 1–400 bytes, printable ASCII `0x20..0x7E`
without `$` and `*`. The receiver resynchronises on every `$`.

| Direction | Body | Note |
| --- | --- | --- |
| host → device | `HELLO proto=1` | handshake; repeated every 300 ms until a reply, 1 s deadline |
| device → host | `HELLO proto=1 fw=<v> app=NebulaeOnDaisy\|LooperOnDaisy dev=... caps=...,sdmsc,nebmgmt\|loopmgmt [sim=1]` | `app` selects the profile; `sdmsc` enables READER |
| host → device | `PING n=<k>` | every 500 ms while connected |
| device → host | `PONG n=<k>` | |
| device → host | `ST ... sd=<0/1> ...` | only `sd` is used (card present) |
| host → device | `READER a=STATUS\|SHARE\|RETURN` | |
| device → host | `READER state=<LOCAL\|STARTING\|SHARED\|EJECTED\|INTERRUPTED\|ERROR> sectors=<n> busy=<0/1> a=<STATUS\|SHARE\|RETURN> [io= fault= he= lba= ms=]` | once per second with SYS, and as the answer to a command |
| host → device | `NEB a=STATUS` / `NEB a=PERFORM` | Nebulae only |
| device → host | `NEB mode=SDMGMT fw=<v> card=<0/1> ret=<0/1> bank=<n> slot=<n> a=<STATUS\|PERFORM>` | |
| host → device | `LOOPER a=STATUS` / `LOOPER a=PERFORM` | Looper only |
| device → host | `LOOPER mode=SDMGMT fw=<v> card=<0/1> ret=<0/1> a=<STATUS\|PERFORM>` | |
| device → host | `ERR c=<cmd> e=<ARG\|SESSION\|STATE\|UNSUPPORTED\|UNKNOWN\|PROTO>` | `c=READER` clears the pending reader command and sets the reader state to unknown |
| host → device | `BYE` | only on an explicit disconnect; never returns the card |

Exact vectors: `editors/mod-on-daisy/tests/sd_share_vectors.txt`, written by
`tests/sd_vectors.py` from the Python reference and read by `tests/test_sd_share.cpp`
(phase 2 byte cross-check: STATUS, SHARE, CONFIRM, RETURN, PERFORM for both instruments plus
HELLO, PING, BYE, `READER a=SHARE`).

## 3. States

### 3.1 Workflow phases (`SdShareWorkflow::Phase`, texts as shown in the heading)

| Phase | Heading text | Means |
| --- | --- | --- |
| NotFound | No Nebulae or Looper found | no MIDI link with a STATUS answer, no CDC session |
| Performance | Performance (USB-MIDI) | STATUS answered on the MIDI link; polled every 1 s |
| Confirm | Confirmation required | SHARE answered CONFIRM_REQUIRED; waits for the user |
| Requesting | Requesting SD management … | SHARE sent, reply awaited (2 s) |
| Saving | Saving the loop before sharing … | Looper answered SAVING; SHARE repeated every 0.5 s (180 s) |
| Restarting | Restarting into SD management … | SHARE ACCEPTED; MIDI link closed; waits for CDC (20 s) |
| Management | SD management (USB CDC + mass storage) | CDC session connected to a profile `app` |
| Returning | Returning to performance … | PERFORM sent; waits for STATUS on MIDI (20 s) |

### 3.2 Reader states (firmware truth, `DdpSession::Reader()`)

LOCAL, STARTING, SHARED, EJECTED, INTERRUPTED, ERROR, plus **unknown** (`std::optional`
empty: no READER received yet, or a reader command timed out, or `ERR c=READER`).

### 3.3 Derived GUI predicates (same names as the Python properties)

| Predicate | Definition |
| --- | --- |
| `ManagementConnected()` | session connected and HELLO `app` equals a profile's `app` |
| `ReaderState()` | reader state of the session while management connected, else unknown |
| `CanShare()` | phase ∈ {Performance, Confirm} and link open and status present and `status.card` and not `status.busy` |
| `NeedsConfirmation()` | phase == Confirm |
| `CanShareInManagement()` | management connected and reader == LOCAL and ST seen with `sd=1` and no reader command pending |
| `Ejected()` | reader == EJECTED only (**CS**: never inferred) |
| `CanReturn()` | management connected and no reader command pending and phase ≠ Returning and reader ∈ {EJECTED, LOCAL} and not `reader.busy` |
| `CanAcknowledge()` | management connected and no reader command pending and reader ∈ {INTERRUPTED, ERROR} |

## 4. Events and transitions (Nebulae and Looper side by side)

Differences between the instruments are only: the SysEx prefix, the STATUS layout, the
`SAVING` result, the HELLO `app`, the capability, the status message (`NEB`/`LOOPER`), the
PERFORM command and the texts. Everything else is one state machine.

| # | From | Event | To | Actions | Nebulae | Looper |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | NotFound (no link) | scan tick (1 s) finds a MIDI port not backed off | NotFound (probing) | open link, send STATUS of **every** profile, `probing = true` | both STATUS requests go out | same |
| T2 | probing | STATUS reply of profile P within 1 s | Performance | `profile = P`, `status`, log "P <fw> found on <port>" | `NB` reply | `LP` reply |
| T3 | probing | 1 s without STATUS | NotFound | close link, back off the port 5 s | | |
| T4 | Performance/Confirm/Saving | 1 s since the last STATUS sent | same | send STATUS of the current profile | | |
| T5 | Performance/Confirm/Saving | 3 s without any reply | NotFound | `_lost`: close link, `status = none`, message "P stopped answering on USB-MIDI" | | |
| T6 | Performance/Confirm/Requesting/Saving | MIDI send or poll fails | NotFound | `_lost` with "MIDI connection lost (…)" / "MIDI send failed (…)" | | |
| T7 | Performance | `RequestShare(confirm=false)` and `CanShare()` | Requesting | new token, send SHARE(confirm=0), deadline 2 s, message "Waiting for the firmware to accept SD management …" | | |
| T8 | Confirm | `RequestShare(confirm=true)` | Requesting | new token, SHARE(confirm=1) | "Discard Live loop and share" | "Share without the unsaved changes" |
| T9 | Confirm | `RequestShare(confirm=false)` | Confirm | refused (`false`) (**CS**: only an explicit confirmation continues) | | |
| T10 | Requesting/Saving | SHARE reply, token matches, ACCEPTED | Restarting | `share_after_restart = true`, deadline 20 s, close link, message "P stops audio and restarts into SD management; Windows will install/open the drive …", log | | |
| T11 | Requesting/Saving | SHARE reply SAVING | Saving | first time: `save_started = now`, log; `resend_at = now + 0.5 s`, deadline `save_started + 180 s`, message texts.saving | never (code 5 unknown → refusal) | yes |
| T12 | Requesting/Saving | SHARE reply CONFIRM_REQUIRED | Confirm | message texts.confirm | Live loop in RAM | last save failed and the loop changed |
| T13 | Requesting/Saving | SHARE reply BUSY / NO_CARD / BAD_REQUEST / other | Performance | error texts.busy / "No microSD card in the slot." / "Request refused (<result>)." ; log | | |
| T14 | Requesting/Saving | SHARE reply with another token, or in another phase | same | ignored | | |
| T15 | Requesting/Saving | ERROR reply | Performance | error "Firmware rejected command 0x<cmd>: <code>" | | |
| T16 | Requesting | 2 s deadline | Performance | error "No answer to the share request; nothing was changed." | | |
| T17 | Saving | `resend_at` reached | Saving | SHARE with the **same** token and confirm flag | | yes |
| T18 | Saving | 180 s deadline | Performance | error "The loop was not saved within 180 s; nothing was shared. Check the card and try again." | | yes |
| T19 | Confirm/Saving | `CancelConfirmation()` | Performance | deadline and resend cleared, message "Cancelled: P keeps playing; nothing was shared." | | |
| T20 | Restarting | CDC session connects with HELLO `app` of a profile | Management | `profile` from `app`, close link, deadline cleared, log "P: SD management connected over USB CDC" | `NebulaeOnDaisy` | `LooperOnDaisy` |
| T21 | Restarting | STATUS reply on MIDI (kit came back in performance, R1) | Performance | error "P restarted but did not enter SD management (boot intent not kept). Nothing was shared.", `share_after_restart = false` | | |
| T22 | Restarting | 20 s deadline | NotFound | error "P did not reappear in SD management within 20 s. Check the USB cable; power-cycling always starts the performance mode.", `share_after_restart = false` | | |
| T23 | NotFound/Performance | CDC session connects with a profile `app` | Management | as T20, log "P found in SD management mode"; the MIDI link is closed | | |
| T24 | Management | `share_after_restart` and reader == LOCAL and `CanShareInManagement()` | Management | **exactly once**: `share_after_restart = false`, `READER a=SHARE`, message "Opening the card for Windows …" (**CS**: the only automatic SHARE, and only for a share the user requested before the restart) | | |
| T25 | Management | `RequestShare()` and `CanShareInManagement()` | Management | `READER a=SHARE` | | |
| T26 | Management | reader state shown | Management | message by state: unknown "Waiting for the card-reader status …", LOCAL texts.local, STARTING "Preparing the card for Windows …", SHARED texts.shared, EJECTED texts.ejected, INTERRUPTED "Access was interrupted (USB reset, card removed or I/O error). Windows may have lost pending changes. The card stays quarantined until you acknowledge.", ERROR "Card I/O failed. Windows may have lost pending changes. The card stays quarantined until you acknowledge." | | |
| T27 | Management, reader EJECTED | `ReturnToPerformance()` and `CanReturn()` | Management | `READER a=RETURN`, `perform_after_return = true`, message "Card returned by the firmware; restarting into performance …" | | |
| T28 | Management, reader LOCAL | `ReturnToPerformance()` and `CanReturn()` | Returning | send PERFORM, deadline 20 s, message texts.returning, log | `NEB a=PERFORM` | `LOOPER a=PERFORM` |
| T29 | Management | `perform_after_return` and reader == LOCAL and no reader command pending | Returning | as T28 (`perform_after_return = false`) | | |
| T30 | Management, reader INTERRUPTED/ERROR | `AcknowledgeInterruption()` and `CanAcknowledge()` | Management | `READER a=RETURN` (**CS**: explicit acknowledgement; no data recovery, not a clean eject) | | |
| T31 | Management | CDC session lost | Management | message "SD management connection lost; card ownership is unknown." (**CS**: no return, no reshare; the session keeps scanning and reconnects with `READER a=STATUS` only) | | |
| T32 | Returning | STATUS reply on MIDI | Performance | message texts.back, log "P is back in performance" | banks rescanned | loop.wav reloaded |
| T33 | Returning | 20 s deadline | NotFound | error "P did not reappear on USB-MIDI within 20 s." | | |
| T34 | any with a link | `Disconnect` by the user / app close | — | the MIDI link is closed; the CDC session sends BYE; **nothing** is sent to the card reader (**CS**) | | |

Notes:

* In phases Performance, Confirm, Saving, Requesting the matrix session may run on the same
  MIDI port; the workflow only consumes `NB`/`LP` replies (section 7).
* Closing the MIDI link in T10 means in the control panel: the matrix session is disconnected
  and the port released (the kit restarts anyway); see section 8.
* The scan in T1 runs while the phase is NotFound, Restarting (to notice R1) and Returning,
  never in Requesting (the link is still open) and never in Management.

## 5. Timeouts

| Name | Value | Where |
| --- | --- | --- |
| `kStatusIntervalMs` | 1000 | STATUS poll in performance |
| `kStatusTimeoutMs` | 3000 | no reply in performance → lost |
| `kProbeTimeoutMs` | 1000 | a port that does not answer STATUS is not an instrument |
| `kShareTimeoutMs` | 2000 | SHARE reply |
| `kRestartTimeoutMs` | 20000 | Restarting and Returning |
| `kScanIntervalMs` | 1000 | MIDI port scan |
| `kSaveRetryMs` | 500 | Looper: repeat SHARE while SAVING |
| `kSaveTimeoutMs` | 180000 | Looper: give up saving |
| `kFailedPortBackoffMs` | 5000 | MIDI and CDC: a port that failed or did not answer |
| DDP `kHandshakeTimeoutMs` | 1000 | HELLO without reply → port backed off |
| DDP `kHelloRetryMs` | 300 | HELLO repeated |
| DDP `kRxTimeoutMs` | 1500 | nothing received → link dropped (state Error) |
| DDP `kReaderIoRxTimeoutMs` | 5000 | the same while the reader is SHARED (Windows I/O blocks the main loop) |
| DDP `kPingIntervalMs` | 500 | PING |
| DDP `kReaderCommandTimeoutMs` | 20000 | a READER command without its answer → reader **unknown** (**CS**: never implies eject or ownership) |
| DDP `kScanIntervalMs` | 1000 | CDC port scan |
| Simulator `kRestartMs` | 800 | reset + bootloader skip + enumeration |
| Simulator `kSaveMs` | 600 | Looper save |
| Simulator device host timeout | 3000 | the firmware ends the stream without host traffic |

Every timeout is driven by the injected clock and tested with a fake clock.

## 6. User texts and button rules of the SD share tab (ported 1:1, English)

### 6.1 Instrument texts (`Profile::texts`)

| Key | Nebulae | Looper |
| --- | --- | --- |
| busy | Nebulae is recording or loading a sample. Stop it and try again. | The looper is recording, overdubbing, resampling or importing a WAV file. Stop it and try again. |
| confirm | A recorded Live loop exists only in RAM and will be LOST by the restart. Confirm to continue, or cancel and keep playing. | The loop could not be saved to the card (the last write failed). Sharing restarts the looper and LOSES the unsaved changes; looper/loop.wav keeps the last saved loop. Confirm to continue, or cancel and keep playing. |
| back | Back in performance. Banks were rescanned from the card. | Back in performance. The loop was reloaded from looper/loop.wav. |
| returning | Nebulae restarts into performance and rescans the card … | The looper restarts into performance and reloads looper/loop.wav … |
| local | Card is with Nebulae (not shared). Share it with Windows or return to performance. | Card is with the looper (not shared). Share it with Windows or return to performance. |
| shared | Windows owns the card. Copy, rename or delete samples, then use “Safely Remove Hardware” / Eject in Windows. | Windows owns the card. Copy WAV files into looper/loops or back up looper/loop.wav, then use “Safely Remove Hardware” / Eject in Windows. |
| ejected | Windows ejected the card and all writes completed. You can return to Nebulae. | Windows ejected the card and all writes completed. You can return to the looper. |
| saving | Saving before the restart … | Saving the current loop to looper/loop.wav before the restart … |

### 6.2 Page layout and rules (from `_build_nebulae_group` / `_refresh_nebulae`)

| Element | Rule |
| --- | --- |
| Heading | phase text (section 3.1), with " – SIMULATION" for a simulated device |
| Instrument | "Looper" / "Nebulae on Daisy" when found (status or management), else "–" |
| Firmware | `status.firmware` or HELLO `fw`, else "–" |
| Connection | "USB-MIDI <port>" in performance; "USB CDC <port> + mass storage" in management; else "–" |
| SD card | performance: "card inserted" / "no card"; management: "card reader: <state or unknown>" |
| Bank / sample (Nebulae) | "bank <b> of <n>[, playing bank <pb> slot <ps>][; last load: <error text>]"; management: "restores bank <b> slot <s>" from NEB or "–" |
| Loop (Looper) | "no loop" or "<s>.<d> s, saved in looper/loop.wav" / "not saved yet" / "saving to looper/loop.wav …" (or "erased, …" without a loop); "; recording" / "; importing a WAV file" when busy; "; last card write failed (<fault text>)" when `save_failed`; management: "reloaded from looper/loop.wav after the return" |
| Audio deadline | "peak <p> % of the block, <n> overruns (firmware counters, not a bench measurement)" + " – OVERRUN LED latched"; management: "audio stopped (SD management)" |
| Message | `workflow.message` |
| Error (red) | `workflow.error` |
| Warning (red, bold) | Nebulae: "A Live loop exists only in RAM. Sharing restarts Nebulae and LOSES it; the SD samples are reloaded from the card." while `status.live` or confirmation needed. Looper: "The loop could not be saved to the card. Sharing restarts the looper and LOSES the unsaved changes; looper/loop.wav keeps the last saved loop." while confirmation needed or (`save_failed` and `unsaved`) |
| **Share SD card with Windows** | enabled: (`CanShare()` and not `NeedsConfirmation()`) or `CanShareInManagement()`; tooltip "Stops audio, restarts the instrument into SD management and opens the card as a Windows drive (USB mass storage). The looper saves its loop to the card first." |
| **Discard Live loop and share** / **Share without the unsaved changes** | visible only while `NeedsConfirmation()`; sends SHARE(confirm=1) |
| **Cancel** | visible while `NeedsConfirmation()` or phase Saving |
| **How to eject safely in Windows …** | enabled only while reader == SHARED; shows the three steps: close every file and program that uses the drive; “Safely Remove Hardware” in the taskbar or Eject in Explorer; wait until the page shows “Windows ejected the card”. "Only the firmware's EJECTED state counts; this button does not eject anything." |
| **Return to Nebulae** / **Return to the Looper** | enabled: `CanReturn()`; tooltip "Only after Windows ejected the card: the firmware returns it and restarts into performance (Nebulae rescans its banks, the looper reloads looper/loop.wav)." |
| **Acknowledge interrupted access …** | enabled: `CanAcknowledge()`; confirmation dialog "Windows may have lost pending changes. Close all files using this card. Acknowledging does not recover data and is not a clean eject. Continue?" with Yes/No, default No |
| Hint | "Performance mode uses USB-MIDI (no COM port); SD management uses USB CDC + mass storage. The card is never returned by closing this window or unplugging: only a Windows eject or an explicit acknowledgement. Power-cycling the kit always starts in performance." |
| Log lines | "P: SD card sharing requested", "P: sharing confirmed; unsaved audio in RAM will be lost", "P: return to performance requested", "P: interrupted access acknowledged" plus the workflow's own log events |

The VST3 shows instead of the page (D2): "Sharing the SD card needs the Daisy Control Panel app
(the kit restarts, and the DAW loses the Daisy MIDI port)".

## 7. Integration with the matrix session (control panel specific)

The Python panel owns its MIDI port alone. The control panel shares one JUCE MIDI port between
the Mod on Daisy session, the SQ session, the bootloader request and the SD share workflow:

* `ClassifySysEx` gains `Protocol::Nebulae` (`4E 42`) and `Protocol::Looper` (`4C 50`); those
  frames go to the workflow, everything else as before.
* The workflow's `MidiPorts` interface is implemented by the editor component: `List()` returns
  the Daisy MIDI ports (or the simulator ports), `Open(name)` connects the component to that
  port (SQ → Mod on Daisy detection as before) and returns a link that sends through the shared
  port. `Close()` of that link detaches the workflow; the port itself stays open for the
  matrix unless the workflow closed it because the kit restarts (T10): then the component
  disconnects the matrix session and releases the port.
* The workflow probes only a port the user connected to (or that it reconnects to itself
  while Restarting or Returning). It does not open ports on its own while idle. A port that
  does not answer (a Toy Piano) is probed again at most every 5 s as in the Python panel
  (two STATUS requests, which every firmware without `NB`/`LP` ignores); the matrix keeps
  the port meanwhile.
* Messages the shared port delivered together with its disappearance still reach the
  workflow (the SHARE ACK arrives in the same poll as the kit's restart); only an empty poll
  on a gone port counts as a lost link, as with the Python link and the simulator.
* The CDC side scans COM ports (VID `0483` / PID `5740`) continuously as the Python panel
  does; a kit found in SD management is shown even if the user never pressed Connect.
* Instruments without a matrix (D4): when the workflow reaches Performance or Management and
  the matrix session is offline and not pending, the component switches to the SD card page
  once and the matrix page shows "<Instrument> <fw> has no modulation matrix."

## 8. Patch save and restore around a share (D5)

| Step | Behaviour |
| --- | --- |
| Share requested from the page (not the confirmation) while the matrix session is connected | the component writes the current patch as `.modpatch` to `%LOCALAPPDATA%\Daisy Control Panel\autosave\<instrument>-<yyyymmdd-hhmmss>.modpatch` (JUCE `userApplicationDataDirectory`), keeps the `LoadedPatch` in memory and logs "Patch saved to <file>; it is sent back after the return." A failed write is logged as an error and the share still proceeds (the user was told). |
| SHARE accepted (T10) | `restore_pending = true` |
| SHARE refused, timed out, cancelled | the file stays, nothing is pending |
| Matrix session connects again after a restart the workflow caused (Returning → Performance, or R1 in T21) | the kept patch goes to the device through the project-patch path (`SendProjectPatch`: columns that mean something else stay off), log "Patch restored after the share (<file>)."; `restore_pending = false` |
| No matrix (plain Nebulae/Looper) | nothing is saved; the page says so in the log |
| Manual Save/Open | unchanged |

## 9. Python versus documents: checked points

| Point | Python | Documents | Port |
| --- | --- | --- | --- |
| Return from LOCAL | `return_to_performance()` sends PERFORM directly from LOCAL | CONTROL_PROTOCOL.md "PERFORM only from LOCAL" | same |
| Return from EJECTED | `READER a=RETURN`, then PERFORM once LOCAL | same | same |
| INTERRUPTED/ERROR | only `acknowledge_interruption()` sends RETURN; `return_to_performance()` refuses | same ("need the panel's acknowledgement or a power cycle") | same |
| Timeout of a READER command | reader → None ("ownership unknown") | PROTOCOL.md "a timeout is not successful eject" | same |
| Reconnect | HELLO, then `READER a=STATUS` only | "never automatically repeats SHARE" | same |
| STATUS 3 s / SHARE 2 s / restart 20 s | constants | NEBULAE_PROFILE.md | same |
| RX tolerance while SHARED | 5 s instead of 1.5 s | PROTOCOL.md "five-second receive gap" | same |
| Looper SAVING | repeat every 0.5 s up to 180 s | SD_MANAGEMENT.md | same |
| Nebulae result code 5 | reported as `CODE5`, treated as a refusal | not defined for Nebulae | same |
| `STARTING` | text only; Python simulator never produces it | PROTOCOL.md lists it | text ported; simulator may be told to produce it for a test |
| Nebulae mgmt rejects `LOOPER` | `ERR c=LOOPER e=UNKNOWN` (generic unknown) | not specified | simulator same |
| Looper 0.6.1 (`main` b7517b5, "make the SHARE decision final") | reference written for 0.6.0 | a busy engine answers SAVING, a first ACCEPTED is decided again under a hold; "the protocol is unchanged" | same: the workflow repeats SHARE on SAVING |

No contradiction that matters for card safety was found. No HOLD.

## 10. Card-safety rules and the state that enforces them

| Rule (profile documents) | Enforced by |
| --- | --- |
| "ejected" only for firmware state EJECTED | `Ejected()`; texts.ejected only in T26 for EJECTED |
| a timeout means "ownership unknown" | DDP `kReaderCommandTimeoutMs` → reader unknown, T26 "Waiting for the card-reader status …"; T31 for a lost session |
| INTERRUPTED and ERROR need an explicit acknowledge | `CanReturn()` excludes them; `CanAcknowledge()` and T30 |
| closing the app, a reconnect or BYE never returns or reshares the card | T34; DDP connect sends `READER a=STATUS` only; T24 runs once and only after a user-requested share |
| SHARE only by an explicit request | T7/T8/T25 need a user call; T24 is the continuation of that call |
| PERFORM only from LOCAL | T28/T29 require reader == LOCAL; EJECTED goes through RETURN first (T27) |
| a lost Live loop / unsaved loop needs confirmation | T12 → Confirm; T9 refuses an unconfirmed retry |
| R1 (wrong personality) is reported | T21 |
| no automatic reshare after an interruption | T24 only with `share_after_restart`, which T10 sets and T21/T22 clear; INTERRUPTED never sets it |

## 11. Test mapping (Python → C++)

C++ tests: `editors/mod-on-daisy/tests/test_sd_share.cpp` (core, no JUCE, run by
`make -C editors/mod-on-daisy test` and `test-sanitize`), `tests/test_serial_transport.cpp`
(phase 3), and the `--selftest` of the control panel (phase 4, GUI).

### 11.1 `test_nebulae.py`

| Python test | C++ |
| --- | --- |
| `test_requests_match_firmware_layout` | `CodecNebulaeRequests` + vector cross-check |
| `test_parse_status_reply` | `CodecNebulaeStatus` |
| `test_parse_rejects_foreign_or_malformed` (6 cases) | `CodecNebulaeRejects` |
| `test_parse_share_and_error` | `CodecNebulaeShareError` |
| `test_full_round_trip_share_eject_return` | `WfNebulaeRoundTrip` |
| `test_mod_build_traffic_on_the_port_does_not_disturb_the_workflow` | `WfForeignSysExSkipped` |
| `test_live_loop_needs_confirmation_and_cancel_keeps_playing` | `WfNebulaeConfirmCancel` |
| `test_refusals_change_nothing` (recording, no card) | `WfNebulaeRefusals` |
| `test_share_timeout_and_lost_device` | `WfShareTimeoutAndLost` |
| `test_restart_that_comes_back_in_performance_is_reported` | `WfWrongPersonalityR1` |
| `test_restart_timeout` | `WfRestartTimeout` |
| `test_interruption_is_quarantined_until_acknowledged` | `WfInterruptionQuarantine` |
| `test_closing_the_link_never_returns_the_card` | `WfCloseNeverReturnsCard` |
| `test_found_already_in_management_does_not_share` | `WfFoundInManagement` |
| `test_unsupported_diagnostic_commands_are_rejected_in_management` | diagnostics-only (the panel never sends LED); the part that matters, "an ERR keeps the session", is `DdpErrKeepsSession` |

### 11.2 `test_looper.py`

| Python test | C++ |
| --- | --- |
| `test_requests_match_firmware_layout` | `CodecLooperRequests` + vector cross-check |
| `test_parse_status_reply` | `CodecLooperStatus` |
| `test_parse_rejects_foreign_or_malformed` | `CodecLooperRejects` |
| `test_parse_share_and_error` | `CodecLooperShareError` |
| `test_management_status_line` | `DdpLooperStatusLine` |
| `test_probe_finds_the_looper_and_still_finds_nebulae` | `WfProbeProfiles` |
| `test_full_round_trip_share_eject_return` | `WfLooperRoundTrip` |
| `test_unsaved_loop_is_saved_before_the_share` | `WfLooperSaving` |
| `test_save_failure_needs_confirmation_and_cancel_keeps_playing` | `WfLooperSaveFailedConfirm` |
| `test_saving_can_be_cancelled_and_times_out` | `WfLooperSaveCancelTimeout` |
| `test_refusals_change_nothing` (recording, importing, no card) | `WfLooperRefusals` |
| `test_found_already_in_management_does_not_share` | `WfLooperFoundInManagement` |
| `test_nebulae_message_is_rejected_by_the_looper` | `SimLooperRejectsNeb` |

### 11.3 `test_gui_nebulae.py` (workflow parts)

| Python test | C++ |
| --- | --- |
| `test_performance_profile_shows_status_and_only_supported_actions` | self-test `SdNebulaeMod` (buttons and rows in performance) + `WfButtonRules` |
| `test_buttons_drive_share_eject_return` | self-test cycle (Nebulae MOD, Nebulae) |
| `test_live_loop_warning_and_confirmation` | self-test `SdNebulaeMod` with a Live loop (CONFIRM → cancel → confirm) |
| `test_refusal_and_interruption_messages` | self-test `SdInterruption` + `WfInterruptionQuarantine`, `WfNebulaeRefusals` |
| `test_long_texts_stay_inside_the_panel` | self-test: long message and error, every visible control inside the page bounds |
| `test_report_contains_workflow` | GUI-only: Daisy Diagnostic's JSON report; the control panel has no report |
| `test_diagnostics_simulation_has_no_nebulae` | GUI-only (diagnostics simulation); the equivalent "Toy Piano simulator shows no instrument" is checked in the self-test |

### 11.4 `test_gui_looper.py`

| Python test | C++ |
| --- | --- |
| `test_looper_profile_in_the_sd_share_tab` | self-test `SdLooperMod` (rows, button texts) |
| `test_save_then_share_eject_return` | self-test `SdLooperMod` with an unsaved loop |
| `test_failed_save_warning_and_confirmation` | `WfLooperSaveFailedConfirm` + self-test button text |
| `test_report_names_the_profile` | GUI-only (report) |

### 11.5 Additional DDP session and transport tests (not in the Python workflow files, needed by the port)

| Source | C++ |
| --- | --- |
| `test_card_reader.py::test_timeout_never_reports_successful_eject` | `DdpReaderTimeoutUnknown` |
| `test_card_reader.py::test_app_close_and_reconnect_do_not_revoke_windows_ownership` | `DdpReconnectStatusOnly` |
| `test_card_reader.py::test_slow_shared_block_operation_has_bounded_receive_tolerance` | `DdpSharedRxTolerance` |
| `test_card_reader.py::test_periodic_status_cannot_acknowledge_a_different_pending_action` | `DdpPendingActionMatch` |
| `test_card_reader.py::test_busy_transport_prevents_return_even_after_eject` | `WfBusyReaderBlocksReturn` |
| `test_session.py` handshake, HELLO retry, backoff, incompatible protocol | `DdpHandshake` |
| `test_protocol.py` CRC and framing | `DdpFraming` (plus the shared vectors) |
| phase 3 | `SerialOpenHello`, `SerialTimeout`, `SerialPortVanishes`, `SerialReconnect`, `SerialNeverBlocksMessageThread` (each call < 50 ms measured while the backend blocks 200 ms) |

Phase 2 PASS requires every row above that names a C++ test to be green under the normal
flags and under ASan/UBSan.
