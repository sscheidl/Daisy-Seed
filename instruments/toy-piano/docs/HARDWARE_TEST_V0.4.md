# Hardware test — Toy Piano v0.4 (echo guard)

Image: `ToyPiano-v0.4.bin` (check the SHA-256 against `SHA256SUMS`). v0.4 differs from
v0.3 only by the echo guard ([CHANGELOG](../CHANGELOG.md)), so the v0.3 checklist applies
unchanged, with version `0.4` in T2:
[HARDWARE_TEST_V0.3.md](HARDWARE_TEST_V0.3.md), T1–T15.

New step, as E7b with Physical Modeling `0.1.0-mod4` (`modules/mod-on-daisy/docs/VALIDATION.md`):

| Nr. | Step | Expected | Result |
| --- | --- | --- | --- |
| T16 | Studio One plays the kit with **input monitoring on** for "Daisy Seed External" (Windows MIDI Services shares the port); then start the editor (or `P monitor --seconds 10`) on the same port, connect, set `LFO1 → Pitch` | editor stays green; monitor values arrive; the vibrato is audible; Studio One keeps playing | |
| T17 | As T16, but start the editor first, then Studio One | as T16 | |

Please note the results in the table or send the console output; they go into
[VALIDATION.md](VALIDATION.md) and `modules/mod-on-daisy/docs/VALIDATION.md`.
