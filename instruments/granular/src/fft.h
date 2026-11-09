// Real 2048-point FFT of the phase vocoder in the Csound packing the Mincer works on:
// [DC, Nyquist, re1, im1, ..., re1023, im1023]. Backend: CMSIS-DSP 1.10 from the pinned
// libDaisy (Apache-2.0, src/fft_cmsis.cpp); the plain radix-2 RFFT in reference_dsp.h
// stays as the oracle of the host tests.
#pragma once

namespace neb::fft {
void Init();  // idempotent; the tables are constants of the image
// time[FFT_N] is destroyed, spectrum[FFT_N] must not overlap it.
void Forward(float* time, float* spectrum);
// Scaled so that Inverse(Forward(x)) == x; spectrum[FFT_N] is read only, time[FFT_N]
// must not overlap it.
void Inverse(float* spectrum, float* time);
}
