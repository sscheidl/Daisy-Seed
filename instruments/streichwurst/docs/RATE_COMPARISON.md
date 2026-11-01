# Sample rate: 40 kHz core with resampler versus direct 48 kHz

As of 01.11.2026. The original computes internally at 40 kHz in steps of 16 and
then resamples to the host rate (SF-A05 CONFIRMED; resampling filter OPEN).
The Seed3 Desktop Kit runs at 48 kHz. The assignment requires keeping both paths separate
and, as far as locally sensible, comparing them.

## Experiment

`make -C instruments/streichwurst compare-rates` ([tools/compare_rates.cpp](../tools/compare_rates.cpp)):
the same own string core once with `Prepare(40000)` and an own Kaiser sinc
resampler (48 taps, cutoff frequency 0.45 · 40 kHz) to 48 kHz, once directly with
`Prepare(48000)` (all time quantities converted to seconds). Measured is the share
of the energy up to 20 kHz outside the harmonics of the lowest footage (±3 FFT bins,
16384 points, Hann). Own test signals, no original render.

**MEASURED 01.11.2026** (Windows, LLVM-MinGW Clang 23.1.3, `-O2`, host PC):

| Note / Registration / Octaves | 40 kHz → 48 kHz | direct 48 kHz | Host time t40/t48 |
| --- | --- | --- | --- |
| 57 / 0.50 / Base | −37.1 dB | −39.6 dB | 0.90 |
| 69 / 0.50 / Base | −34.4 dB | −37.5 dB | 0.86 |
| 81 / 0.50 / Both | −31.2 dB | −36.0 dB | 1.01 |
| 93 / 1.00 / Base | −33.7 dB | −39.0 dB | 0.95 |
| 100 / 1.00 / Both | −36.5 dB | −27.0 dB | 0.92 |
| 108 / 1.00 / Base | −24.1 dB | −36.6 dB | 0.86 |

The host time does not include the resampler; it would be added on the board
(48 multiplications per output sample and channel).

## Decision

The product computes **directly at 48 kHz**:

* In five of six cases the inharmonic share is lower directly at 48 kHz,
  because the higher Nyquist frequency leaves more distance to the square-wave overtones.
* A 40 kHz core saves only about one sixth of the core load on the M7, but costs the
  resampler and latency. The CPU load of both paths on the Seed3 is **NOT MEASURED**.
* Exception note 100 with both octaves: here an overtone falls less favourably directly at
  48 kHz. This remains a documented sonic deviation.

Consequences: the aliasing patterns do not match those of the original core with its
unknown resampling filter. Thanks to the time scaling, envelopes, ensemble LFOs and delays have the
same time constants as at 40 kHz. The core remains
sample-rate independent (`Prepare(fs)`); later 40 kHz operation with a documented
resampler is possible without changing the module boundaries.
