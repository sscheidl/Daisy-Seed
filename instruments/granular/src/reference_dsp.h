// Mincer is adapted from Csound pvlock.c (C) 2009 V Lazzarini, LGPL-2.1-or-later.
// De-Pattern/Qu-Bit MIT notices and Csound COPYING are in ../LICENSES/.
// Nebulae v2 granular looper -- portable DSP core.
// NO Daisy/hardware dependencies. Host-compilable for validation against
// the Python reference oracle. See Nebulae_Engine_Port_Notes.md.
#pragma once
#include "pcm.h"
#include <cstdint>
#include <cstddef>

namespace neb {

constexpr int   WIN_LEN    = 512;          // giwindowtablesize
constexpr int   FFT_N      = 2048;         // ifftlivesize
constexpr int   DECIM      = 4;            // idecimlive
constexpr int   HOP        = FFT_N / DECIM;// 512
constexpr int   MAX_GRAINS = 10;           // imax_grains
constexpr float GRAIN_AMP  = 0.45f;        // iamp
constexpr float RMS_CEIL   = 0.20f;
constexpr float LN32       = 3.4657359028f;

// ---------- window bank (index order = Window knob sweep, CCW -> CW) ----------
enum WinId { W_GAUSSIAN=0, W_BLKHARRIS, W_RAMPDOWN, W_BARTLETT, W_RAMPUP, W_SINC, W_RECT,
             W_COUNT, W_BANK=W_COUNT };
void  BuildWindows(float bank[W_BANK][WIN_LEN+1]);
void  WinMix(const float bank[W_BANK][WIN_LEN+1], float knob,
             float out[WIN_LEN+1]);

// ---------- real FFT, Csound packing: [DC, Nyq, re1, im1, ... ] ----------
class RFFT {
public:
    void Init();
    void Forward(float* d);   // in-place, length N -> Csound packing
    // Two real forward transforms with ONE complex FFT (a in the real, b in the
    // imaginary part, then split by conjugate symmetry). Same result as Forward(a);
    // Forward(b) up to float rounding; used by Mincer to cut the hop's FFT work
    // from three to two complex FFTs per channel without moving any hop (F3).
    void ForwardPair(float* a, float* b);
    void Inverse(float* d);   // in-place, Csound packing -> length N
private:
    float cs_[FFT_N], sn_[FFT_N];
    int   rev_[FFT_N];
    float re_[FFT_N], im_[FFT_N];
    void  Cfft(float* re, float* im, bool inverse);
};

// ---------- phase vocoder (Csound mincer, mono variant) ----------
// Reference implementation, the one shipped up to 0.2.1: plain radix-2 RFFT, libm hypotf,
// PcmView::Read per sample, 104 KB of state per channel. It measured 14 ms per hop and
// channel in SDRAM and 3.2 ms in AXI SRAM on the kit (docs/OVERLOAD_DIAGNOSIS.md); the
// firmware uses Mincer below. Kept unchanged as the numerical oracle of the host tests.
class ReferenceMincer {
public:
    void  Init(float sr);
    void  Reset();
    void  Process(const double* frame_pos, const PcmView& buf, unsigned channel,
                  float pitch, float amp, float* out, int n);
    // Host A/B tests only: false selects the v0.1 path with two separate forwards.
    void  SetPackedForward(bool packed) { packed_ = packed; }
private:
    bool  packed_ = true;
    float sr_, win_[FFT_N];
    float fwin_[FFT_N+2], bwin_[FFT_N+2], prev_[FFT_N+2];
    float outframe_[DECIM*FFT_N];
    int   framecnt_[DECIM], cnt_, curframe_;
    RFFT  fft_;
};

// Working memory the Mincer channels of one engine share: the Hann window and the
// frames of the hop being computed. Audio callback only, never across a Process call.
struct MincerWork {
    float window[FFT_N];
    float front[FFT_N], back[FFT_N];              // windowed frames; front is also the IFFT output
    float front_bins[FFT_N+2], back_bins[FFT_N+2];
    void  Init();
};

// Production vocoder since 0.2.2: the same algorithm, hop grid and arithmetic order as
// ReferenceMincer, with a CMSIS-DSP real FFT (src/fft.h), sqrtf for hypotf and
// PcmReader for the window fill. The output differs at float rounding level only
// (host A/B in tests/test_nebulae.cpp). State per channel: 41 KB.
class Mincer {
public:
    void  Init(float sr, MincerWork& work);
    void  Reset();
    void  Process(const double* frame_pos, const PcmView& buf, unsigned channel,
                  float pitch, float amp, float* out, int n);
    // Phase state and overlap-add frames of another channel (same MincerWork).
    void  CopyStateFrom(const Mincer& other);
private:
    void  Hop(double frame_pos, const PcmView& buf, unsigned channel, float pitch);
    MincerWork* work_ = nullptr;
    float sr_, prev_[FFT_N+2];
    float outframe_[DECIM*FFT_N];
    int   framecnt_[DECIM], cnt_, curframe_;
};

// RMS limiter: identity below RMS_CEIL, attenuation above. NOT a leveler.
class RmsLimiter {
public:
    void Init(float sr, float hp = 10.0f);
    void Process(float* sig, int n);
private:
    float b_, gprev_; double acc_;
};

// ---------- control laws ----------
float KnobSpeed(float pot);     // D5 remap: detent -> +1x
float KnobPitch(float pot);     // D6 remap: detent -> unity
float LawSpeed(float g, bool freeze);
float LawPitch(float g);
float LawDensity(float g);
float LawGrainSizeMs(float overlap, float grainfreq);
void  LawLoop(float g_start, float g_size, float buflen_sec, float* start, float* size);
void  LawBlendLive(float g, float* voc, float* dry, float* grain);

} // namespace neb
