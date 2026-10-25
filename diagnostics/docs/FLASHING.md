# Flashing over USB

These steps are performed manually on the kit. Automated builds do not flash hardware.

## Two USB-C ports

* **Seed3 module:** STM32 ROM DFU mode and one-time Daisy bootloader installation.
* **Desktop carrier, J7 (above the Seed3):** power, normal operation, and firmware
  updates through the **external** Daisy bootloader.

Use a USB data cable and connect to the port specified for each step.

## One time: install the external bootloader

**Skip this if a suitable external v6.x bootloader is already installed.**
Diagnostics is a BOOT_SRAM application and does not replace the bootloader. Direct
application installation through ROM DFU is not the procedure used here.

1. Connect the computer to the USB-C port **on the Seed3 module**.
2. Hold **BOOT**, press and release RESET, then release BOOT to enter STM32 ROM DFU.
3. Open the [Daisy Web Programmer](https://flash.daisy.audio/) in Chrome/Edge,
   select bootloader **v6.x (external)**, and install it. Alternatively, with
   `dfu-util` installed, run from the project root:

   ```bash
   make -C firmware program-boot
   ```

   The Makefile selects `libDaisy/core/dsy_bootloader_v6_4-extdfu-2000ms.bin`, like
   the official Desktop template. This separate command writes the bootloader;
   a normal `make` does not run it.
4. Move the cable to **J7 on the Desktop carrier**.

Only install a bootloader when it is missing or the wrong variant. Do not repeat
`program-boot` for routine application updates.

## Routine updates: flash Diagnostics

1. Connect and power the kit through **J7 on the Desktop carrier**.
2. Put the kit into update mode. Without touching the Seed3 buttons:
   * any firmware of this repository, including the diagnostics: hold **S9 + S16**
     (outer keys of the second row) for 5 s;
   * diagnostic firmware running: **Firmware update mode …** in the Windows app
     (DDP `BOOT`);
   * an instrument or effect running: **Firmware-Update …** in the Mod on Daisy editor
     ([../../docs/FLASHING.md](../../docs/FLASHING.md)).

   All 16 LEDs light up and the bootloader waits without a time limit.
   **Recovery** (older or hung firmware): press and release **RESET**, then press
   **BOOT** within approximately two seconds. The USR LED blinks quickly, then pulses
   slowly: the Daisy bootloader is waiting for the application. This differs from the
   ROM DFU sequence above.
3. Connect in the [Daisy Web Programmer](https://flash.daisy.audio/), choose
   **Custom**, and upload **`firmware/build/DaisyDiagnostic.bin`**. Alternatively, run:

   ```bash
   make -C firmware program-dfu
   ```

   The official Makefile writes the application into QSPI at `0x90040000`.
   The bootloader loads it into SRAM at `0x24000000` for execution.
4. The firmware starts after flashing; press RESET if needed. The bootloader
   remains installed for later updates.

Flashing Diagnostics replaces Toy Piano. Flash the Toy Piano binary to switch back;
only one application runs at a time.

## If the device does not appear

Check the data cable, port, and that the kit is in update mode (all LEDs on, or
the USR LED pulsing after BOOT/RESET). Routine updates require the
external bootloader through J7. WebUSB requires a supported browser and USB access
permissions. Linux may need udev rules; Windows may need the official Daisy/Zadig
driver setup. See the [official help](https://docs.daisy.audio/tutorials/daisy-web-programmer/)
and [Desktop kit instructions](https://github.com/daisyaudio/Seed3-DevKit-Desktop#4-install-the-daisy-bootloader-one-time).
