# Bootloader entry without BOOT/RESET

Shared by every firmware in this repository. It restarts the Seed3 into the
installed **external Daisy bootloader v6.x** so a new application can be flashed
over J7 while the case is closed. The physical RESET/BOOT sequence in
[docs/FLASHING.md](../../docs/FLASHING.md) stays the recovery path.

The firmware calls libDaisy's
`System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT)` (pinned
libDaisy `f715e48`). It writes `0xB0074EFA` (`INF_TIMEOUT`) into the boot-info word in
backup SRAM and performs a software reset. The bootloader then waits for DFU without
its grace timeout (2 s with the installed `extdfu-2000ms` variant), until an image
arrives or the kit is power-cycled.

## Triggers

| Trigger | Where | Detail |
| --- | --- | --- |
| **S13 + S14** held together, with no other key, for **3 s** | all apps except the diagnostic firmware | Released and pressed again for another attempt. Another key, or a release, restarts the timer. |
| USB-MIDI SysEx on J7 | apps with USB-MIDI: Toy Piano, Physical Modeling (both builds), Nebulae, Mod bench | Sent by the HTML editor, the JUCE app and the VST3 plugin (**Firmware-Update …**) |
| DDP command `BOOT` | diagnostic firmware (USB CDC) | Button **Firmware update mode …** in the Windows diagnostics app; see [its protocol](../../diagnostics/protocol/PROTOCOL.md) |

S13 and S14 (the 5th and 6th key of the second row) have no function in the Looper,
Ambient Reverb and the Nebulae function mode. In the note layouts they are G5 and A5;
Toy Piano and Physical Modeling play them as one-shots, so holding them does nothing
musically. In the Nebulae KEYBOARD mode a held G5+A5 dyad does sustain: do not hold
exactly these two keys alone for 3 s there unless an update is intended.

The diagnostic firmware is a key tester and has no key gesture.

Before the reset every app lights **all 16 LEDs** (D1–D16). The PCA9685 LED driver
keeps them on through the MCU reset, so the panel shows "update mode" while the
bootloader waits; the next application's `hw.Init()` turns them off. This is the
expected behaviour of the driver; it has not been checked on hardware yet.

The Looper first finishes a running card write or import, then resets. Changes
from the last 2 s may still be unsaved (`LoopStore::kSaveDelayMs`).

## SysEx format

```text
request  F0 7D 42 4C 01 01 42 4F 4F 54 F7
reply    F0 7D 42 4C 01 41 F7
         │  │  └─┬─┘ │  │  └──┬──────┘
         │  │   "BL" │  │   "BOOT" (request only)
         │  │        │  └ command 0x01 ENTER; reply = 0x01 | 0x40
         │  │        └ version 1
         │  └ non-commercial manufacturer ID, shared with Mod on Daisy ("MD", 4D 44)
```

* Only this exact byte sequence is accepted: no checksum, but a fixed length and the
  four-byte "BOOT" word, so a truncated or damaged frame does nothing.
* The firmware answers before the reset and waits 20 ms for the transfer. The reply
  is never a request, so a DAW that echoes MIDI back to the kit cannot cause a loop.
* Old firmware without this module ignores the request; the editors report "no
  answer" after 2 s and point to the keys or the buttons.
* Nebulae parses the raw USB-MIDI stream itself and uses `SysExMatcher`; real-time
  bytes inside the frame are skipped.

## Using it in an app

```cpp
#include "bootloader_daisy.h" // + "bootloader_midi.h" for MidiUsbHandler events
bootloader::KeyCombo boot_keys;   // control/audio context only
bootloader::Request boot_request; // lock-free flag to the main loop

// Audio callback or control timer, with the debounced held-key mask (bit 0 = S1):
if(boot_keys.Process(held, daisy::System::GetNow()))
    boot_request.Set(bootloader::Request::kKeys);
// USB-MIDI events in the main loop (before Mod on Daisy sees SysEx):
if(bootloader::IsRequest(event)) boot_request.Set(bootloader::Request::kSysEx);
// Main loop: reply if a host asked, light the LEDs, reset.
bootloader::Service(boot_request, hw, [](uint8_t* b, size_t n) { /* send if configured */ });
```

`common/desktop.mk` adds this directory to the include path. Nothing runs in the
audio callback except the key timer; LEDs, USB and the reset stay in the main loop.

## Bootloader compatibility

libDaisy added `DAISY_INFINITE_TIMEOUT` together with the v6.0 bootloader images
(libDaisy changelog v6.0.0); older bootloaders ignore the boot-info word and only
offer their normal grace period. The pinned images `libDaisy/core/dsy_bootloader_v6_4-*.bin`, including the
`extdfu-2000ms` variant this repository installs, contain the `0xB0074EFA` constant
and the backup-SRAM address `0x38800000` (checked statically in the binaries on
2026-10-25). On the kit, the diagnostic firmware read the installed bootloader as
`v6_1` ("v6.1 or newer") on 17 October 2026
([TEST_REPORT.md](../../diagnostics/docs/TEST_REPORT.md), H2).
Whether the installed bootloader honours `INF_TIMEOUT`: **NICHT GEMESSEN**.

## Tests

```bash
make -C common/bootloader test
```

Host tests cover the frame bytes, body and stream matching (truncated, longer,
corrupted, interrupted, echoed reply, Mod on Daisy frames, real-time bytes), the
3 s gesture (extra keys, bounce, release, counter wrap) and the service order
(reply before reset, one reset, LEDs). Each app's callback tests drive both triggers
through its production `main.cpp` with a fake `ResetToBootloader()`.
