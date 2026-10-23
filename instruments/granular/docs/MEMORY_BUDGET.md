# Actual ARM memory budget

Validated 2026-10-20 using xPack ARM GCC 13.2.1-1.1, official BOOT_SRAM linker
script plus `src/memory.ld`, `arm-none-eabi-size`, MAP and
`python scripts/check-granular-elf.py build/NebulaeOnDaisy-v0.1.0.elf` (adjust
path when run from repository root). Numbers describe this exact build, not a
runtime measurement of stack high-water or CPU utilization.

| Region | Used/reserved | Capacity | Purpose |
| --- | ---: | ---: | --- |
| AXI SRAM | 163,072 B | 491,520 B | BOOT_SRAM image plus isolated 512-byte SD IDMA sector and alignment |
| DTCM | 77,100 B | 131,072 B | Board/USB/UI/queues/loader and normal static data |
| D2 noncacheable DMA | 18,596 B | 32,768 B | Official audio/LED/UART buffers |
| D2 regular | 65,536 B | 262,144 B | Explicit bounded C-library/USB heap |
| SDRAM allocated | 58,720,256 B | 67,108,864 B | Live banks, DSP and dynamically sized one-file PCM region |
| SDRAM unallocated reserve | 8,388,608 B | included above | Not exposed as file capacity |

`arm-none-eabi-size`: text 159,664; data 2,868; BSS 58,879,852 bytes. BSS includes
NOLOAD reservations and is not BIN size. Actual BIN is **162,540 bytes**. The
checker verifies its loaded bytes against the actual ARM ELF segments.

## SDRAM layout and format limits

| Address / symbol | Bytes | Contents |
| --- | ---: | --- |
| `0xc0000000` - `0xc1000000` | 16,777,216 | Two 8 MiB interleaved stereo PCM16 Live banks |
| `0xc1000000` - `0xc103c780` | 247,680 | Engine storage (247,664 B) and alignment: vocoders, FFT/window workspaces, grain pool, notes and scratch |
| `__neb_file_start=0xc103c780` - `__neb_file_end=0xc3800000` | **41,695,360** | Exactly one complete file sample, PCM16 |
| `0xc3800000` - physical end `0xc4000000` | 8,388,608 | Robust unallocated reserve |

`capacity = __neb_file_end - __neb_file_start`,
`max_frames = floor(capacity / (channels*2))`,
`max_seconds = max_frames / source_rate`.

| Rate | Channels | Maximum frames | Maximum seconds |
| --- | ---: | ---: | ---: |
| 44,100 | 1 | 20,847,680 | 472.736508 |
| 44,100 | 2 | 10,423,840 | 236.368254 |
| 48,000 | 1 | 20,847,680 | 434.326667 |
| 48,000 | 2 | 10,423,840 | 217.163333 |

There are no 16 resident files and no full float32 file copy. PCM converts to
float only during interpolated reads. The two Live banks each hold 2,097,152
stereo frames = 43.690667 s at 48 kHz. Capture never writes the active read bank;
publication retires existing grains/FFT tails before reusing that bank. Partial
overdub finalization copies at most 8192 frames (32 KiB) per callback.

The linker fixes the physical SDRAM end, reserves the top 8 MiB, and requires at
least 1 MiB of file capacity. The board app independently checks start/end before
constructing the PCM view. Increasing DSP/Live storage reduces file capacity
automatically. The ELF checker verifies actual symbol placement, alignment,
single-file extent and both reserved bank/object symbols. Re-run it after changes;
the table is a build result, not a hard-coded duration limit.

## Stack, heap, cache and publication

Static DTCM headroom is 53,972 bytes. Linker and ELF gates enforce a minimum
16 KiB stack floor. `-fstack-usage` emits `.su`: application Audio is 104 bytes,
Engine::Process 168, RenderGrains 144 and Mincer::Process 128 in the validated
configuration. These per-function sizes do not prove a complete nested HAL/IRQ
worst case. Actual stack high-water remains a hardware measurement.

The default nosys `_sbrk` had no limit. Project `heap.c` replaces it with checked
growth/shrink inside a dedicated 64 KiB D2 reservation; overflow and signed-min
boundary cases are tested. No heap operation occurs in the audio callback.
USB middleware can allocate at initialization/enumeration, outside audio. No
file, DSP or recorder buffer depends on a successful runtime heap allocation.

Official audio/I2C DMA buffers remain in libDaisy's noncacheable D2 section.
SDMMC1 IDMA uses only a dedicated 32-byte-aligned, 512-byte AXI SRAM sector in
`.neb_sd`, beyond the executable image. It never targets DTCM or SDRAM. The driver
cleans/invalidates this isolated sector before DMA and invalidates after completion;
CPU then copies to FatFS/loader buffers. The official BSP/HAL and board remain
unmodified. SD IRQ priority is lowered to 2 after initialization; audio remains 0.

File PCM in cacheable SDRAM is written/read by the same Cortex-M7 CPU, not DMA.
Main sets gate; audio fades, detaches its view/tails, then release-stores ack.
Main acquire-loads ack before destructive writes. Metadata/generation publication
uses release/acquire; main waits for audio to release ack before another load.
Preflight failures preserve old PCM; a later read failure publishes an empty file
view and active slot 0. Producer/consumer tests exercise 128 back-to-back loads.

## CPU and latency scope

Audio block 256 at 48 kHz: deadline 5.333 ms. The pinned official driver does not
support 512. FFT 2048, hop 512, two channels: worst callback performs six radix-2
transforms (67,584 butterflies), plus at most 20 active/pending grains, 256 output
frames, fixed note scans and bounded input/control queues. Half the callbacks
normally do not compute a new FFT frame. This operation budget is not a cycle
measurement. Cortex-M7 has no NEON; no invented SIMD acceleration is claimed.

Vocoder window duration is 42.667 ms; bank/file transitions use 256-sample ramps.
CPU peak/overrun DWT counters must be checked on target, especially high overlap,
short files and SD/MIDI activity. Pedal-port timing reports are not this board's
measurements. Numerical tests and host wall time cannot certify real-time margin.
