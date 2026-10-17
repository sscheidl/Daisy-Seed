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
2. Press/release RESET, then press BOOT during the approximately two-second window.
   The USR LED first flashes quickly, then pulses slowly while waiting for firmware.
   This differs from the ROM-DFU sequence above.
3. Connect with the [Daisy Web Programmer](https://flash.daisy.audio/), choose Custom
   and select **`instruments/toy-piano/build/ToyPiano-v0.2.bin`**. Alternatively:

   ```bash
   make PROJECT=toy-piano program-dfu
   ```

   The official Makefile writes the application to QSPI at `0x90040000`. The Daisy
   bootloader loads it into SRAM at `0x24000000` for execution.
4. The application starts after flashing; press RESET if necessary. The bootloader
   remains installed for the next update.

For the first Ambient Reverb test, select
`effects/reverb/build/AmbientReverb-v0.1.bin` in the Web Programmer, or use
`make PROJECT=reverb-fx program-dfu` with the same J7 connection and bootloader
sequence. See the [reverb controls and test steps](../effects/reverb/README.md).

## If the device is missing

Check the data cable, connector and correct BOOT/RESET sequence first. Updates
require the external bootloader to enumerate through J7. WebUSB needs a supported
browser and USB permissions. Linux may need udev rules; Windows may need the
Daisy/Zadig driver setup. See the [official guide](https://docs.daisy.audio/tutorials/daisy-web-programmer/)
and [Desktop Kit instructions](https://github.com/daisyaudio/Seed3-DevKit-Desktop#4-install-the-daisy-bootloader-one-time).
