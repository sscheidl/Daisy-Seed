// CMSIS-DSP adapter for src/fft.h. The instance is filled by hand instead of through
// arm_rfft_fast_init_f32(): the generic init references the tables of every FFT length
// and would keep about 80 KB of them in the SRAM image. Only the 1024-point complex
// twiddles, their bit-reversal table and the 2048-point real twiddles are linked.
#include "fft.h"
#include "arm_math.h"
#include "arm_common_tables.h"

namespace neb::fft {
namespace {
arm_rfft_fast_instance_f32 instance;
bool ready = false;
}

void Init() {
    if(ready) return;
    instance.Sint.fftLen = 1024;
    instance.Sint.pTwiddle = twiddleCoef_1024;
    instance.Sint.pBitRevTable = armBitRevIndexTable1024;
    instance.Sint.bitRevLength = ARMBITREVINDEXTABLE_1024_TABLE_LENGTH;
    instance.fftLenRFFT = 2048;
    instance.pTwiddleRFFT = twiddleCoef_rfft_2048;
    ready = true;
}

void Forward(float* time, float* spectrum) { arm_rfft_fast_f32(&instance, time, spectrum, 0); }
void Inverse(float* spectrum, float* time) { arm_rfft_fast_f32(&instance, spectrum, time, 1); }
}
