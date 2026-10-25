# Flashing over USB

These are local hardware steps. No device was flashed from cloud.
Commands below run at the **repository root**. The examples select `PROJECT=toy-piano`;
substitute `PROJECT=reverb-fx` for Ambient Reverb.
Each project has its own BIN; the last flashed application runs on the kit.
Runtime switching between instruments is not implemented.

## Two USB-C connectors

* **Seed3 module:** used for STM32 ROM DFU and the one-time Daisy bootloader installation.
* **Desktop carrier J7, above the Seed3:** power, normal operation and application
  updates through the **external** Daisy bootloader.

Use a USB data cable. These are separate USB connections; move the cable to the
connector required by the procedure.

## One time: install the external bootloader

**Skip this if an appropriate external v6.x bootloader is already installed.**
The application is BOOT_SRAM firmware; it does not replace the bootloader and is
not installed directly through STM32 ROM DFU in this workflow.

1. Connect the computer to the USB-C connector **on the Seed3 module**.
2. Hold BOOT, press and release RESET, then release BOOT. This enters STM32 ROM DFU.
3. Open the [Daisy Web Programmer](https://flash.daisy.audio/) in Chrome/Edge and
   install **v6.x (external)**. Alternatively, with `dfu-util` installed:

   ```bash
   make PROJECT=toy-piano program-boot
   ```

   The shared Makefile selects
   `libDaisy/core/dsy_bootloader_v6_4-extdfu-2000ms.bin`, as in the official template.
   This explicit command writes the bootloader; ordinary `make` does not.
4. Move the cable to **J7 on the Desktop carrier**.

Only install a bootloader when the existing one is missing or unsuitable.
Do not repeat `program-boot` for ordinary firmware updates.

## Each update: flash Toy Piano

1. Connect and power the kit through **J7 on the Desktop carrier**.
2. Put the running firmware into update mode, **without the BOOT/RESET buttons**
   ([details](../common/bootloader/README.md)):
   * **Keys:** hold **S13 and S14** (5th and 6th key of the second row) together,
     with no other key, for **3 seconds**; or
   * **Editor:** click **Firmware-Update …** in the HTML editor, the JUCE app or the
     VST3 plugin (USB-MIDI SysEx on J7; needs an app with USB-MIDI); or
   * **Diagnostic firmware:** **Firmware update mode …** in the Windows diagnostics app.

   All 16 LEDs light up and the kit restarts into the Daisy bootloader, which now
   waits for firmware **without a time limit**. This needs firmware that contains
   the entry; an older image, or a crashed one, needs the recovery sequence below.
   Keys work in Toy Piano, Physical Modeling, Nebulae, Looper, Ambient Reverb and the
   Mod bench; the Looper first finishes a running card write.
3. Connect with the [Daisy Web Programmer](https://flash.daisy.audio/), choose Custom
   and select **`instruments/toy-piano/build/ToyPiano-v0.3.bin`**. Alternatively:

   ```bash
   make PROJECT=toy-piano program-dfu
   ```

   The official Makefile writes the application to QSPI at `0x90040000`. The Daisy
   bootloader loads it into SRAM at `0x24000000` for execution.
4. The application starts after flashing and turns the LEDs off. If not, power-cycle
   the kit by unplugging J7 (or press RESET). The bootloader remains installed for the
   next update. To leave update mode without flashing, power-cycle the kit the same way.

### Recovery: RESET and BOOT

If the firmware does not react (an older image without the entry, a crash, a hang),
use the buttons on the Seed3. With a closed case they are reached through the plungers.

1. Connect and power the kit through J7.
2. Press/release RESET, then press BOOT during the approximately two-second window.
   The USR LED first flashes quickly, then pulses slowly while waiting for firmware.
   This differs from the ROM-DFU sequence above.
3. Continue with step 3 above.

For the first Ambient Reverb test, select
`effects/reverb/build/AmbientReverb-v0.2.bin` in the Web Programmer, or use
`make PROJECT=reverb-fx program-dfu` with the same J7 connection and update mode
(S13+S14; the reverb has no USB-MIDI, so no editor button). See the [reverb controls and test steps](../effects/reverb/README.md).

For Nebulae on Daisy v0.1.0, use the same **carrier J7 / external bootloader**
application sequence with `instruments/granular/build/NebulaeOnDaisy-v0.1.0.bin`,
or explicitly run `make PROJECT=granular program-dfu`. No bootloader update is
needed for an already compatible external v6.x installation. This implementation
did not flash a device. Follow its [short smoke plan](../instruments/granular/docs/HARDWARE_TEST_PLAN.md).

## If the device is missing

Check the data cable and connector first, then whether the kit really is in update
mode: all LEDs on after S13+S14, or the USR LED pulsing after the RESET/BOOT sequence. Updates
require the external bootloader to enumerate through J7. WebUSB needs a supported
browser and USB permissions. Linux may need udev rules; Windows may need the
Daisy/Zadig driver setup. See the [official guide](https://docs.daisy.audio/tutorials/daisy-web-programmer/)
and [Desktop Kit instructions](https://github.com/daisyaudio/Seed3-DevKit-Desktop#4-install-the-daisy-bootloader-one-time).
