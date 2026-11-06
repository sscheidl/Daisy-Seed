# Hardware test SC Reverb FX 0.1.0

Firmware `effects/sc-reverb/build/ScReverbFx-v0.1.0.bin` (`make PROJECT=sc-reverb-fx`). The owner runs this test on the kit.
Every result is **NOT MEASURED** until an entry with a date stands here. This accepts the effect alone; Audrey 3 has its own
CPU and listening acceptance.

Source: short sounds into the stereo input. Start quietly on headphones: at decay 1 the tail does not end.

| Check | Result |
| --- | --- |
| VR3 at zero: the input passes unchanged | NOT MEASURED |
| VR3 up: reverb, longer with VR1 up | NOT MEASURED |
| VR2 down: the tail gets darker | NOT MEASURED |
| VR1 at maximum with a loud input: stays bounded | NOT MEASURED |
| SW17 off: no new sound enters, the tail rings on; on again: no click | NOT MEASURED |
| D16 stays off with all pots moving | NOT MEASURED |
| Bootloader with S9 + S16 | NOT MEASURED |
