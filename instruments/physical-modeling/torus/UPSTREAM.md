# Vendored DSP provenance

* DaisyExamples / patch/Torus:
  https://github.com/daisyaudio/DaisyExamples/tree/259ed82c7d4c0d7699f695945e6e111b7dc7cb27/patch/Torus
* stmlib: https://github.com/pichenettes/stmlib/tree/5dc8b89b9d166de5f47ba91ee9d25c8805c2bd86
  This is the gitlink recorded by that DaisyExamples revision.
* Original Rings: https://github.com/pichenettes/eurorack/tree/f4cfa7697095a28d292bff758beb51deb49b5a86/rings
  Reference only; no additional unpinned source imported.
* libDaisy f715e4875b2d28445d0c11a0a17f064a248f69d0 and
  DaisySP 2c72eaf9eac5fc0dca1919d65d606da907832618 remain unchanged gitlinks.

Copyright Emilie Gillet and the respective upstream contributors. MIT notices
remain in every source; stmlib LICENSE is retained. Vendored files were copied
from these exact revisions, not a floating branch. Patch UI/CV scaler and the
alternate string-synth/easter-egg engine are not needed for the six Part models.
Only the transitive stmlib DSP dependencies are vendored. No new submodule.

The application also includes an MIT-licensed local copy of libDaisy
`src/hid/midi_parser.cpp` at the pinned revision. Its Parse/Reset symbols are
linked only into this project, with two tested corrections: fully initialize
parser state on Reset, and consult SystemCommon subtypes only on system messages.
Copyright/license text is included in that file. Library gitlinks and files are
unchanged. The separate regression projects continue using their existing parser.

Local changes:

* Part: four independent keyboard strikes and velocities, direct MIDI pitch,
  fixed four voices, 12-sample LFO step, one shared global reverb instead of
  Part's model-local instance; original resonator and string equations retained.
* Model 6: stereo cross-routing and string damping retained; right polarity
  affects only dry, after splitting off the non-inverted global reverb send.
  Wet polarity/tails are independent of the current model. Original synthesis
  Damping/Brightness-to-reverb parameter coupling is intentionally removed.
* FMVoice: velocity envelope/oscillator baseline; initialize feedback sample;
  use a defined signed-64 intermediate for modular float-to-phase conversion;
  clamp the carrier below Nyquist at extreme MIDI + transpose pitches.
* String: initialize SRC phase explicitly.
* Reverb: initialize decay/filter/mix state explicitly, including non-static use.
* Resources: explicit cmath and sine initialization; sine LUT in SDRAM on ARM.
* stmlib Interpolate: return an exact table entry before reading the next one
  when the fractional position is zero. ASan found the original final-entry
  access at Structure/Damping=1; the fix preserves exact endpoint values.

The original note filter/onset/strummer helpers remain as provenance/reference;
keyboard events do not run automatic CV strumming or its inhibit timer.
