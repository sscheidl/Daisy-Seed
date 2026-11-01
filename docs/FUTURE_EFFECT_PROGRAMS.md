# Future effect programs

**Wish of 24.10.2026, not yet implemented.** For further
Daisy development, the effect bundled so far and labelled as Clouds reverb
shall be replaced or extended by the following own programs:

* A stereo delay program.
* Our already developed Ambient Reverb from the Slöer work,
  [effects/reverb](../effects/reverb/README.md).

The existing Ambient Reverb is an own development with Slöer as
a design reference. It is not an identified original Walrus algorithm.
Since Acid Seq phase 4, the stereo delay exists as the component `stereo-delay` with its own
firmware `delay-fx` ([effects/stereo-delay](../effects/stereo-delay/)): ping-pong and
stereo L/R, sync up to 12 s in the SDRAM. The Acid Seq product places it after the Drive. Other
instrument firmwares do not integrate it yet.

The expectation is that Seed3 performance and memory suffice. **CPU reserve,
callback peaks, RAM/SDRAM usage and the behaviour of the combination are
NOT MEASURED.** Before integration, profile the effect programs individually and together with
the respective sound generation; derive delay time, buffer format and
reverb memory requirements from that. No allocation or blocking
operation in the audio callback.

Existing instruments and their golden hashes remain unchanged until a targeted,
documented sound change. This entry starts neither a
firmware rework nor a release workflow.
