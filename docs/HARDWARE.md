# Seed3 Desktop Development Kit

The target is the **Seed3 Desktop Dev Kit**, not Pod, Field, Patch or a bare Seed.
Toy Piano uses `daisy::desktop_devkit::Hardware` from the pinned libDaisy commit;
application code does not invent GPIO mappings.

## Verified API and mapping

| Function | Official class API | Official wiring (reference only) |
| --- | --- | --- |
| 16 buttons | `button[0..15]`, `UpdateAllControls()` | Two chained shift registers; clock D8, latch D9, data D10 |
| 16 LEDs | `SetLed(0..15, brightness)`, `UpdateLeds()` | PCA9685, I2C1, SCL D11, SDA D12 |
| 8 pots | `pot[0..7].Value()` | 8:1 mux: ADC D15, select D24/D25/D26 |
| Stereo output | `seed.StartAudio()` | J3 left, J4 right, J5 headphones |
| Audio | 48 kHz, 48 samples/block | Seed3/TAC5242 initialized by libDaisy |

UpdateAllControls reverses shift-register indices internally with
`button[i].Debounce(button_sr.State(15 - i))`. Do not reverse them again:
index 0 is S1/D1, index 15 is S16/D16. The LED class already handles brightness
inversion. VR1/VR2 are mux/pot indices 0/1.

## Existing reference application: Toy Piano

The following control rates, filters and LED behavior describe
[Toy Piano](../instruments/toy-piano/). New projects define their own interaction
and timing budgets while retaining the official board abstraction.

DigitalControl maintains eight bits of history, qualifying edges after seven
stable samples (about 7–8 ms at 1 kHz). The application latches held state between
press/release edges. Extra rising edges after a short contact dropout are ignored
while held; a single bad raw sample does not flicker the LED or retrigger a note.

AnalogControl is configured for 1 kHz. At the default 2 ms slew its coefficient is
1.0, so it adds no smoothing itself. The ADC uses 32× oversampling; PotFilter adds
20 ms smoothing and 0.1% hysteresis, mapping the outermost 0.3% to exact endpoints.

LED state crosses a lock-free 32-bit mailbox to the main loop. The official DMA
buffer swap can wait for a previous transfer, so UpdateLeds stays outside audio
and runs at most every 10 ms, only on changes. A very brief qualified press may
fall entirely between LED services and sound without a visible LED pulse.

Known unmodified libDaisy limitation: an immediately failed TransmitDma start can
leave the next buffer swap waiting indefinitely. LEDs then freeze, while the
higher-priority audio callback continues. This has not been observed or measured
on the operator's board.

Hardware::Init also configures unused DIN MIDI/CV peripherals. Toy Piano does not
process DIN MIDI or generate CV/gate output.

## USB-MIDI

MidiUsbHandler with MidiUsbTransport::Config::EXTERNAL uses the peripheral routed
to carrier J7, also used by the template's serial logger. The product name is
Daisy Seed External. Incoming USB packets are parsed into MidiEvents in the USB
interrupt; ServiceUsbMidi consumes them in main and passes Note Ons to audio
through a lock-free queue. Transmit also runs in main: SendMessage may block for
up to three retries of 100 microseconds and must not run in the audio callback.

Output compares the current debounced key mask with the last reported host state,
bundling up to eight changes per transfer. There is no output event queue that
could overflow and discard releases. A complete press/release between services
is not reported; USB transport failure remains a separate limitation.

libDaisy does not check whether a host configured the device before sending.
The app checks `hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED` and otherwise
discards output state changes. This field comes from the pinned middleware.

If the host has not collected a previous transfer, the next transfer may fail
silently after retries. SendMessage exposes no result. A reset/unplug during a
transfer is also a theoretical window between the state check and send. The main
loop attempts at most one send per millisecond.

MidiHandler's FIFO holds 256 MidiEvents (about 37 KB). DTCM use is 116,072 of
131,072 bytes (88.6%), leaving about 14.9 KB nominally for the stack. Stack headroom
and callback time still require measurements on hardware.

## Official references

Mapping/API baseline checked on 2026-10-16:

* [Hardware](https://github.com/daisyaudio/Seed3-DevKit-Desktop), commit
  `987297fb7c9a8c58721ef40abf6d6eedd1fcda2a`.
* [Product documentation](https://docs.daisy.audio/product/Seed3-Desktop-Dev-Kit/).
* [libDaisy](https://github.com/daisyaudio/libDaisy), commit
  `f715e4875b2d28445d0c11a0a17f064a248f69d0`.
* [Desktop hardware class](https://github.com/daisyaudio/libDaisy/blob/f715e4875b2d28445d0c11a0a17f064a248f69d0/src/desktop_devkit.cpp).
* [Desktop template](https://github.com/daisyaudio/libDaisy/tree/f715e4875b2d28445d0c11a0a17f064a248f69d0/examples/devkits/Desktop-DevKit-UI-Template).
* [DaisySP](https://github.com/daisyaudio/DaisySP), commit
  `2c72eaf9eac5fc0dca1919d65d606da907832618`.

API/wiring checks and operator-reported functional tests are distinct; see the
[validation report](../instruments/toy-piano/docs/VALIDATION.md) for their scope.
