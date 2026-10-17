# Seed3 Desktop Development Kit

This project explicitly targets the **Seed3 Desktop Dev Kit**, not Pod, Field,
Patch, or an unconnected Seed board. It uses only `daisy::desktop_devkit::Hardware`
from the pinned libDaisy revision. The application does not assign GPIO pins itself.

## Verified API and mapping

| Function | Official hardware API | Official wiring (reference only) |
| --- | --- | --- |
| 16 buttons | `button[0..15]`, `UpdateAllControls()` | Two chained shift registers; clock D8, latch D9, data D10 |
| 16 LEDs | `SetLed(0..15, brightness)`, `UpdateLeds()` | PCA9685, I²C1, SCL D11, SDA D12 |
| 8 knobs | `pot[0..7].Value()` | 8:1 mux: ADC D15, selection D24/D25/D26 |
| Stereo outputs | `seed.StartAudio()` | J3 left, J4 right, J5 headphones |
| Audio format | 48 kHz, 48 samples/block | Seed3/TAC5242, initialized by libDaisy |

`Hardware::UpdateAllControls()` reverses shift-register indices internally:
`button[i].Debounce(button_sr.State(15 - i))`. Application code must **not reverse
them again**. Index 0 is S1/D1; index 15 is S16/D16. The LED hardware class handles
inverted brightness. VR1/VR2 are mux channels and knob indices 0/1.

DigitalControl uses an 8-bit debounce history and reports edges after seven stable
samples, approximately 7–8 ms at 1 kHz. The firmware holds each LED's debounced state
between press and release edges. A second press edge while already held is ignored:
DigitalControl can report another rising edge after a short contact dropout without
first reporting a release edge.

`Hardware::Init()` configures AnalogControls for 1 kHz. The application uses
48-sample blocks at 48 kHz and calls `UpdateAllControls()` once per block. With the
default 2 ms slew, AnalogControl has a coefficient of 1.0 and does not smooth the
signal itself. The ADC uses 32× oversampling. PotFilter adds 20 ms smoothing and
0.1% hysteresis; the outermost 0.3% maps to the time endpoints.

LED data reaches the main loop through a lock-free 32-bit mailbox. `UpdateLeds()`
uses the official DMA driver, whose buffer swap may wait for a previous transfer.
It runs only outside the audio callback, at most every 10 ms and only when the
state changes. A very short debounced press can fall entirely between LED polls:
the note sounds without a visible LED flash.

Known limitation of the unmodified libDaisy driver: an immediately failed
`TransmitDma()` start can make the next buffer swap wait indefinitely. LEDs may
freeze while higher-priority audio continues. This has not been observed or
measured on the actual board in the recorded Toy Piano validation.

`Hardware::Init()` also initializes unused peripherals such as MIDI/CV.
The application does not process MIDI or generate CV/gate signals.

## Official sources

Checked on 16 October 2026:

* [Hardware and schematic](https://github.com/daisyaudio/Seed3-DevKit-Desktop), revision `987297fb7c9a8c58721ef40abf6d6eedd1fcda2a`.
* [Product documentation](https://docs.daisy.audio/product/Seed3-Desktop-Dev-Kit/).
* [libDaisy](https://github.com/daisyaudio/libDaisy), `f715e4875b2d28445d0c11a0a17f064a248f69d0`.
* [Desktop hardware class](https://github.com/daisyaudio/libDaisy/blob/f715e4875b2d28445d0c11a0a17f064a248f69d0/src/desktop_devkit.cpp).
* [Official Desktop template](https://github.com/daisyaudio/libDaisy/tree/f715e4875b2d28445d0c11a0a17f064a248f69d0/examples/devkits/Desktop-DevKit-UI-Template).
* [DaisySP](https://github.com/daisyaudio/DaisySP), `2c72eaf9eac5fc0dca1919d65d606da907832618`.

Mappings were checked against the API and official hardware tables. Actual board
assembly and operation must be confirmed during hardware testing.
