# Effect Projects

Separate project directories for audio input applications on the Seed3 Desktop
Dev Kit:

| Effect | Project ID | Status |
| --- | --- | --- |
| [Reverb](reverb/) | `reverb-fx` | Clouds/Nimbus experiments and a possible Slöer stereo reverb port |
| [Granular / Freeze](granular/) | `granular-fx` | Planned |

When implemented, each effect will have its own source, tests, README, and build
directory. DSP actually shared by an instrument and effect can then move to
`common/` instead of maintaining two engine copies. There is currently no
effect firmware or combined effect chain. Resonators remain models and
excitation variants within [Physical Modeling](../instruments/physical-modeling/).
