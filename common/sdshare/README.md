# Shared SD management (`common/sdshare`)

Lets a performance firmware hand its microSD card to Windows and take it back, with the
card left in the kit. One image, two boot personalities, switched by a controlled restart
with a one-shot boot intent. Generalised from Nebulae on Daisy 0.2.0
([architecture](../../instruments/granular/docs/V0_2_ARCHITECTURE.md),
[protocol](../../instruments/granular/docs/CONTROL_PROTOCOL.md)).

| File | Purpose | Host-tested |
| --- | --- | --- |
| `boot_intent.h` | one-shot, checksummed intent in backup SRAM; per-firmware magic | yes |
| `control_sysex.h` | USB-MIDI SysEx frame `F0 7D <p0> <p1> ...`: STATUS/SHARE/errors, result `PENDING` | yes |
| `admin.{h,cpp}` | DDP v1 responder of the SD management personality (`READER`, `NEB`, `BOOT`) | yes |
| `sd_management.{h,cpp}` | board side: card reader wiring, LEDs, S4 escape, restart (ARM only) | no (hardware) |

Users: **Looper 0.5.0** (prefix `LP`, magic `LPR1`). Nebulae 0.2.0 still carries its own
copies (`instruments/granular/src`) so that the image under hardware test stays unchanged;
moving it onto this component is a follow-up after its hardware acceptance.

Rules every user must keep: SHARE only on explicit request; the card returns only after a
Windows eject (EJECTED) or an explicit acknowledgement of an interrupted access; BYE,
timeouts or unplugging never return it; power-on is always the performance personality.

A firmware links `admin.cpp`, `sd_management.cpp`, `common/sd/reader/*`,
`diagnostics/protocol/ddp.cpp` and sets its USB identity with `READER_USB_PRODUCT`,
`READER_USB_BCD_MAJOR` and `READER_USB_SERIAL_SUFFIX`.
