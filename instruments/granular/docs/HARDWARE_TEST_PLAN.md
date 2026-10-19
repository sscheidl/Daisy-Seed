# User hardware smoke test - PENDING-HARDWARE

Use `build/NebulaeOnDaisy-v0.1.0.bin` after checking `SHA256SUMS`. This is a
BOOT_SRAM application for the Seed3 Desktop kit with an existing external v6.x
Daisy bootloader. It has not been flashed or listened to by the implementation
agent. Do not install/update the bootloader for this ordinary application test.

1. Connect a USB data cable to **carrier J7**, above the Seed3. Press/release
   RESET, then press BOOT within the external bootloader's roughly two-second
   window. In [Daisy Web Programmer](https://flash.daisy.audio/), connect, select
   Custom, choose this BIN and program. This updates the QSPI application at
   `0x90040000`; the bootloader copies it into SRAM. It is not the ROM-DFU
   BOOT-held-reset sequence. See [FLASHING](../../../docs/FLASHING.md).
2. With headphones/output initially low, test no-card startup. Upper SW18 should
   show NEBULAE, center SAMPLE, lower KEYBOARD. Verify lever direction against
   the API mapping; keep SW17 in either position (reserved). Confirm key presses
   only light their own LEDs in KEYBOARD and held keys release across a mode
   change. There is no permanent animation.
3. Put known PCM16 WAVs in `/NEBULAE/SAMPLES/01.wav` ... `16.wav` on FAT32 microSD,
   including a 44.1 kHz mono tone and a 48 kHz stereo file with clearly different
   left/right content. Verify startup/slot selection, all 16 slot buttons, active
   LED, missing/invalid-file pulse patterns and retained old audio on bad headers.
   Check correct stereo separation and no pitch/time error at 44.1 kHz.
4. In NEBULAE, test all eight primary pots, SOURCE tap versus hold/catch, FILE,
   RESET to START and FREEZE without silence. Feed distinct audio to L/R, select
   Live, set BLEND center, record/stop and listen at both wet endpoints. Test an
   overdub, early stop/finalization and circular start/stop. SOURCE+RESET restores
   secondary defaults. Do not erase a wanted volatile loop with FILE+RESET.
5. On J7 in a DAW, check external USB-MIDI enumeration. Try Note On/Off,
   velocity-zero, channel collisions, pitch bend and CC20-27 in every UI mode;
   test all-notes-off and unplug/replug. Verify local keyboard note output without
   enabling a DAW echo loop back into the device.
6. Test maximum Density/Overlap and simultaneous SD/MIDI/LED activity. Inspect
   `callback_overruns` and `callback_peak_cycles` in a debugger if available; the
   deadline is `SystemCoreClock*256/48000` cycles. Any increasing overrun counter,
   repeated clicking/dropout, wrong switch orientation, SD error or stuck MIDI
   note is a concrete defect to report along with the BIN hash. Host execution
   speed is not Cortex-M7 evidence.

No extra hardware, factory calibration, external gate source or full musical
review is required for the first smoke test. Grain gate input can be tested later.
