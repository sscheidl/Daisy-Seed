# Firmware product

Composition of the Streichwurst product for the Seed3 Desktop Kit.

* `product.h`: audio graph (instrument, FX string stage, balance, global slot,
  ensemble gain word, master, panic ramp, output protection); host-tested.
* `register.h`: one parameter register for pots, MIDI CC, SQ and the matrix.
* `controls.h`: pot pages with soft takeover, keys, LEDs, bootloader chord.
* `mod_targets.h`: the eight Mod on Daisy columns (firmware and editor simulator).
* `main.cpp`: board, queues, SQ device, Mod on Daisy, USB/DIN MIDI, bootloader.
* `version.h`: product version `STREICHWURST_VERSION`.
