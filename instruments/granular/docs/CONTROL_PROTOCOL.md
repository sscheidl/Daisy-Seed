# Nebulae on Daisy 0.2.0 - control protocol

Two transports, one per boot personality ([architecture](V0_2_ARCHITECTURE.md)). Hardware
behaviour is **NICHT GEMESSEN**; everything below is host-tested only.

## 1. Performance personality: USB-MIDI SysEx

Class-compliant USB-MIDI on J7. Requests and replies are SysEx with the non-commercial
manufacturer ID `0x7D`, kept apart from the other users of that ID by two bytes:

| Bytes after `F0 7D` | Owner |
| --- | --- |
| `42 4C` ("BL") | bootloader entry ([common/bootloader](../../../common/bootloader/README.md)), unchanged and active in every mode |
| `4D 44` ("MD") | Mod on Daisy |
| `4E 42` ("NB") | **this protocol** |

```text
request  F0 7D 4E 42 <ver=01> <cmd> <payload...> F7
reply    F0 7D 4E 42 01 <cmd|0x40> <payload...> F7
```

All payload bytes are 7-bit. The USB interrupt only collects one request into a
one-slot mailbox (`src/control_sysex.h`); a second request before the main loop took
the first is dropped and the host retries. SysEx cancels MIDI running status, as in
MIDI 1.0; note, CC and bend parsing are otherwise unchanged.

### STATUS `01` (no payload) -> reply `41`, 19 bytes

| Byte | Meaning |
| --- | --- |
| 0-2 | firmware major, minor, patch (from `src/version.h`) |
| 3 | personality, `0` = performance |
| 4 | flags: bit0 card present, bit1 Live loop in RAM, bit2 recording/finalizing, bit3 sample loading, bit4 audio deadline overrun latched, bit5 bank scan running |
| 5 | selected bank (0 = none) |
| 6 | number of banks |
| 7, 8 | bank and slot of the playing sample (0 = none) |
| 9-11 | existing banks, 16-bit mask in 7-bit groups (bits 0-6, 7-13, 14-15) |
| 12-14 | occupied slots of the selected bank, same encoding |
| 15 | last load error (0 none, 1 missing, 2 invalid WAV, 3 too large, 4 I/O, 5 no card) |
| 16-17 | audio overruns since power-on, 14 bit, saturating |
| 18 | callback peak in percent of the 256-sample deadline, saturating at 127 |

Bytes 16-18 are firmware counters (DWT), not bench measurements.

### SHARE `02` -> reply `42`

Request payload: `confirm` (0/1) and a 4-byte token chosen by the host. The reply
echoes the token after the result byte:

| Result | Meaning | Restart |
| --- | --- | --- |
| `00` ACCEPTED | SD management follows | yes, after the reply |
| `01` CONFIRM_REQUIRED | a Live loop exists only in RAM and would be lost; resend with `confirm=1` | no |
| `02` BUSY | recording/finalizing or a sample load runs | no |
| `03` NO_CARD | no microSD in the slot | no |
| `04` BAD_REQUEST | wrong length or confirm > 1 | no |

On ACCEPTED the firmware runs PREPARE_SHARE: fade to silence within one block, Note
Off for every note it sent plus CC 123, stop audio, close/unmount FatFs, power the SD
interface down, write the boot intent (`src/boot_intent.h`), wait 20 ms for USB, and
restart through the external bootloader with `DAISY_SKIP_TIMEOUT`.

### Errors -> reply `7F`

`7F <cmd> <code>`: `01` unsupported version, `02` unknown command, `03` wrong length.

### Data kept across the restart

| Kept | Reloaded / reset |
| --- | --- |
| SD card content | the playing sample is reloaded from the card (bank/slot restored when it still exists) |
| pot positions (physical) | Live loop and overdubs (RAM): **lost**, hence CONFIRM_REQUIRED |
| selected bank and slot (in the boot intent) | secondary parameters, FREEZE, circular, trigger mode, file order: defaults |

A power cycle always starts in performance: the intent is one-shot and checksummed.

## 2. SD management personality: DDP v1 over CDC + MSC

USB composite (CDC interfaces 0/1, MSC interface 2) of the shared card reader
(`common/sd/reader`), product string `Nebulae on Daisy SD`, bcdDevice 2.00, serial
suffix `N`, so Windows keeps it apart from the Diagnostics reader (3.00, `A`).
Framing, HELLO, PING, GET, RATE, BYE, BOOT and READER follow
[diagnostics/protocol/PROTOCOL.md](../../../diagnostics/protocol/PROTOCOL.md):

```text
HELLO proto=1 fw=0.2.0 app=NebulaeOnDaisy dev=seed3-desktop boot=BOOT_SRAM bl=v6_1 libdaisy=f715e48 caps=btn,pot,potraw,sw17,sw18,sd,boot,sdmsc,nebmgmt
```

No `led`, `sdread`, `jack`, `cvin`, `cvout` or `audio`: LED, SDTEST, CVOUT, TONE and
SCOPE are answered with `ERR c=<cmd> e=UNSUPPORTED`. ST is sent at the RATE (default
10 Hz) with real buttons, pots, switches and card detect; `jl`/`jr` are `0` because the
capability `jack` is not advertised.

### NEB (capability `nebmgmt`)

| Command | Response |
| --- | --- |
| `NEB a=STATUS` | `NEB mode=SDMGMT fw=0.2.0 card=1 ret=1 bank=2 slot=5 a=STATUS` |
| `NEB a=PERFORM` | from LOCAL only: `NEB ... a=PERFORM`, then restart into performance (intent with bank/slot); otherwise `ERR c=NEB e=STATE` |

`ret=1` means the card is LOCAL and a return to performance is allowed. NEB status is
also sent once per second with SYS and READER while a session is active.

### Ownership rules

* SHARE only by an explicit `READER a=SHARE` (the control panel sends it once after a
  share it requested itself).
* PERFORM and BOOT only from LOCAL: after Windows ejected (EJECTED) and `READER
  a=RETURN`, or after an interruption was explicitly acknowledged with `READER
  a=RETURN`.
* BYE, a 3 s host timeout, a closed port or an unplugged cable never return the card.
* Without a PC: holding **S4 alone for 2 s** returns from LOCAL or EJECTED. INTERRUPTED
  and ERROR need the panel's acknowledgement or a power cycle.

### Panel LEDs in SD management

| LED | State |
| --- | --- |
| D1 steady | LOCAL (card with Nebulae, not shared) |
| D2 flashing / steady | STARTING / SHARED |
| D4 steady | EJECTED (safe to return) |
| D5 flashing 2 Hz | INTERRUPTED or ERROR (quarantined) |
| D16 slow blink | SD management personality |
