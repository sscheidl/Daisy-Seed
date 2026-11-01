# Tests

* `test_instrument.cpp`: the instrument component alone.
* `test_product.cpp`: register, audio graph, slot exchange, kit controls, own fingerprint.
* `test_firmware.cpp`: the production `src/main.cpp` on host fakes (SQ, Mod on Daisy, MIDI).
* `reference/own_dsp_model.py`: the independent historical Python model.

FX tests live in `effects/streichwurst-fx/tests`. Build outputs go to the ignored
`build/` directory. Synthetic signals are not original renders.
