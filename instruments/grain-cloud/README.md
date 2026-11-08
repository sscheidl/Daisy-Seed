# Grain Cloud 0.1.0

A new, independent granular engine for the Seed3 Desktop Kit: up to eight notes over one global
pool of up to 256 grains, stereo, 48 kHz. This is **stage 2: the offline host core**. There is no
firmware yet, nothing was flashed, and no sound was judged on hardware: **NOT MEASURED**.

It is separate from `instruments/granular`, the faithful Nebulae port, which stays as it is.

## Build and test

```bash
make -C instruments/grain-cloud test
make -C instruments/grain-cloud test-sanitize
make -C instruments/grain-cloud test-thread
make -C instruments/grain-cloud cost
```

`make PROJECT=grain-cloud` is refused on purpose: the project is `planned` and has no image.
On Windows, pass `HOST_CXX=clang++`. TSan needs Linux.

## Modules

| File in `include/grain_cloud/` | Job |
| --- | --- |
| `engine.h` | Voices, scheduler and block order |
| `grain_pool.h`, `grain_renderer.h` | Grain state, pool, the per-sample path |
| `window_table.h` | Shared window with a slow morph |
| `envelope.h` | Voice envelope with fast release |
| `laws.h`, `random.h` | Own control laws, ramps, the voice's random generator |
| `modulation.h` | Connection to the Mod on Daisy matrix, one-sided targets |
| `control_bridge.h`, `params.h` | Typed events, parameter table |
| `output_stage.h` | The one clip point |
| `sample_view.h` | The sample the engine reads |

## Using it

```cpp
grain_cloud::Engine engine;                  // in static memory: about 21 KB, no allocation
engine.Prepare(grain_cloud::Config{});       // startup only
engine.AttachModulation(&mod_engine);        // optional, the shared matrix
engine.PublishSample(view);                  // main loop
engine.Post(grain_cloud::NoteOn(60, 100));   // main loop

// Audio callback, 48 frames:
engine.RenderSum(left, right, 48);           // voice sum, unclipped
// ... FX contract stages here, once per output ...
engine.Output().Process(left, right, left, right, 48);
```

The product owns effects, memory placement, MIDI translation and the mapping of controls. Initialize
the Mod on Daisy engine with the tick length (`block_frames x tick_blocks`), so its coefficients are
not recomputed in the callback.

## Dependencies

Mod on Daisy (`mod_on_daisy/engine.h`), Step Seq queues (`step_seq/spsc.h`) and `common/dsp/fastmath.h`.
Effects connect through FX contract 1.0. No libDaisy or DaisySP code is used.

## Documents

* [Rules and behaviour](docs/BEHAVIOUR.md): block order, R1 to R9, the birth-or-running table, limits.
* [Provenance](docs/PROVENANCE.md).
* [Changelog](CHANGELOG.md).
