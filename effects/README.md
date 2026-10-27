# Effect Projects

Separate project directories for audio input applications on the Seed3 Desktop
Dev Kit:

| Effect | Project ID | Status |
| --- | --- | --- |
| [Ambient Reverb](reverb/) | `reverb-fx` | Buildable original stereo reverb; hardware test pending. Clouds/Nimbus remains a separate study |
| [Tape FX](tape-fx/) | `tape-fx` | Buildable TapeVibe/MixForge port as reusable component and stereo effect; also used by `toy-piano-tape`; hardware test pending |
| [Drive](drive/) | `drive-fx` | v0.1.0: saturation curves shared with Tape FX, tone, auto level; buildable, hardware test pending |
| [Stereo Delay](stereo-delay/) | `delay-fx` | v0.1.0: ping-pong and stereo L/R delay, sync up to 12 s; buildable, hardware test pending |
| [Granular / Freeze](granular/) | `granular-fx` | Planned |

Ambient Reverb has its own source, host tests, README, and build directory.
Other effects will get the same structure when implemented. DSP actually shared
by an instrument and effect can then move to `common/` instead of maintaining
two engine copies. There is no combined effect chain. Resonators remain models and
excitation variants within [Physical Modeling](../instruments/physical-modeling/).
