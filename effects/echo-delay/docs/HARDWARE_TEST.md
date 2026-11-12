# Hardware test Echo Delay FX 0.1.1

Firmware `effects/echo-delay/build/EchoDelayFx-v0.1.1.bin` (`make PROJECT=echo-delay-fx`). The owner runs this test on the kit.
Every result is **NOT MEASURED** until an entry with a date stands here. This accepts the effect alone; Audrey 3 has its own
CPU and listening acceptance.

Source: short sounds into the stereo input. Start quietly on headphones: above feedback 1 the signal builds up.

| Check | Result |
| --- | --- |
| VR3 at zero: the input passes unchanged | NOT MEASURED |
| VR3 up: repeats at the time of VR1, narrower with each repeat | NOT MEASURED |
| Left stays left, right stays right | NOT MEASURED |
| Moving VR1: the pitch bends and settles, faster with VR4 down; no clicks | NOT MEASURED |
| VR2 at maximum with a loud input: saturates, stays bounded | NOT MEASURED |
| SW17 off: no new sound enters, the tail rings on; on again: no click | NOT MEASURED |
| 5 s delay time reached with VR1 at maximum | NOT MEASURED |
| D16 stays off with all pots moving | NOT MEASURED |
| Bootloader with S9 + S16 | NOT MEASURED |
