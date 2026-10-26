#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include "unit.h"

class MasterFX {
 public:
  MasterFX() {
    for (uint8_t i = 0; i < kNumParams; ++i) {
      params_[i].store(kParamInit[i], std::memory_order_relaxed);
    }
  }

  inline int8_t Init(const unit_runtime_desc_t * desc) {
    if (desc->samplerate != kSampleRate)
      return k_unit_err_samplerate;

    if ((desc->input_channels != 2 && desc->input_channels != 4) ||
        desc->output_channels != 2)
      return k_unit_err_geometry;

    input_channels_ = desc->input_channels;
    Reset();
    return k_unit_err_none;
  }

  inline void Teardown() {}

  inline void Reset() {
    for (uint8_t i = 0; i < 2; ++i) {
      filter_a_[i].reset();
      filter_b_[i].reset();
      tape_pre_lp_[i] = 0.0f;
      tape_post_lp_[i] = 0.0f;
    }
    comp_env_ = 0.0f;
    gain_smooth_ = 1.0f;
    crush_hold_l_ = 0.0f;
    crush_hold_r_ = 0.0f;
    crush_counter_ = 0;
    crush_lp_l_ = 0.0f;
    crush_lp_r_ = 0.0f;
  }

  inline void Resume() {}
  inline void Suspend() {}

  fast_inline void Process(const float * in, float * out, size_t frames) {
    const int32_t filter = getClampedParam(kParamFilter);
    const int32_t resonance = getClampedParam(kParamResonance);
    const int32_t slope = getClampedParam(kParamSlope);
    const int32_t threshold = getClampedParam(kParamThreshold);
    const int32_t ratio = getClampedParam(kParamRatio);
    const int32_t attack = getClampedParam(kParamAttack);
    const int32_t release = getClampedParam(kParamRelease);
    const int32_t drive = getClampedParam(kParamDrive);
    const int32_t sat_mode = getClampedParam(kParamSatMode);
    const int32_t bitcrush = getClampedParam(kParamBitcrush);
    const int32_t bits = getClampedParam(kParamBits);

    const FilterSettings filter_settings = getFilterSettings(filter, resonance, slope);
    const float threshold_lin = dbToLin(threshold * 0.1f);
    const float ratio_f = static_cast<float>(ratio);
    const float attack_coef = timeToCoef(static_cast<float>(attack) * 0.001f);
    const float release_coef = timeToCoef(static_cast<float>(release) * 0.001f);
    const float drive_norm = percentToUnit(drive);
    const float drive_gain = 1.0f + 11.0f * drive_norm;
    const float compensation = 1.0f + 0.23f * drive_norm * 11.0f;
    const float loudness_bias = 1.0f + 0.08f * drive_norm;
    const float post_gain = loudness_bias / compensation;

    const float * __restrict in_p = in;
    float * __restrict out_p = out;
    for (size_t i = 0; i < frames; ++i, in_p += input_channels_, out_p += 2) {
      float l = in_p[0];
      float r = in_p[1];

      processFilterPair(l, r, filter_settings);
      processCompressor(l, r, threshold_lin, ratio_f, attack_coef, release_coef);

      if (sat_mode == 2) {
        l = tapePreEmphasis(l, 0);
        r = tapePreEmphasis(r, 1);
      }

      l = processSaturation(l, drive_gain, post_gain, sat_mode);
      r = processSaturation(r, drive_gain, post_gain, sat_mode);

      if (sat_mode == 2) {
        l = tapeDeEmphasis(l, 0);
        r = tapeDeEmphasis(r, 1);
      }

      if (bitcrush) {
        processBitcrush(l, r, bits, drive);
        postCrushLowpass(l, r);
      }

      out_p[0] = clampAudio(l);
      out_p[1] = clampAudio(r);
    }
  }

  inline void setParameter(uint8_t index, int32_t value) {
    if (index >= kNumParams)
      return;
    params_[index].store(clampInt(value, kParamMin[index], kParamMax[index]),
                         std::memory_order_relaxed);
  }

  inline int32_t getParameterValue(uint8_t index) const {
    if (index >= kNumParams)
      return 0;
    return params_[index].load(std::memory_order_relaxed);
  }

  inline const char * getParameterStrValue(uint8_t index, int32_t value) const {
    switch (index) {
      case kParamSlope:
        return kSlopeNames[clampInt(value, 0, 2)];
      case kParamSatMode:
        return kSatModeNames[clampInt(value, 0, 3)];
      default:
        return nullptr;
    }
  }

  inline const uint8_t * getParameterBmpValue(uint8_t index, int32_t value) const {
    (void)index;
    (void)value;
    return nullptr;
  }

  inline void LoadPreset(uint8_t idx) { (void)idx; }
  inline uint8_t getPresetIndex() const { return 0; }

  static inline const char * getPresetName(uint8_t idx) {
    (void)idx;
    return nullptr;
  }

 private:
  enum ParamIndex : uint8_t {
    kParamFilter = 0,
    kParamResonance,
    kParamSlope,
    kParamUnused,
    kParamThreshold,
    kParamRatio,
    kParamAttack,
    kParamRelease,
    kParamDrive,
    kParamSatMode,
    kParamBitcrush,
    kParamBits,
    kNumParams
  };

  struct TptSvf {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    inline void reset() {
      ic1eq = 0.0f;
      ic2eq = 0.0f;
    }

    inline void process(float x, float g, float r2, float &low, float &band,
                        float &high) {
      const float h = 1.0f / (1.0f + r2 * g + g * g);
      const float v3 = x - ic2eq;
      const float v1 = (ic1eq + g * v3) * h;
      const float v2 = ic2eq + g * v1;
      ic1eq = 2.0f * v1 - ic1eq;
      ic2eq = 2.0f * v2 - ic2eq;
      low = v2;
      band = v1;
      high = x - r2 * v1 - v2;
    }
  };

  struct FilterSettings {
    float g = 0.0f;
    float r2 = 1.0f;
    uint8_t mode = 0;
    bool bypass = true;
    bool cascade = false;
  };

  static constexpr uint32_t kSampleRate = 48000U;
  static constexpr float kPi = 3.14159265358979323846f;

  static const int32_t kParamMin[kNumParams];
  static const int32_t kParamMax[kNumParams];
  static const int32_t kParamInit[kNumParams];
  static const char * const kSlopeNames[3];
  static const char * const kSatModeNames[4];

  inline int32_t getClampedParam(uint8_t index) const {
    return clampInt(params_[index].load(std::memory_order_relaxed),
                    kParamMin[index], kParamMax[index]);
  }

  static inline int32_t clampInt(int32_t value, int32_t lo, int32_t hi) {
    return value < lo ? lo : (value > hi ? hi : value);
  }

  static inline float clampFloat(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
  }

  static inline float clampAudio(float value) {
    return clampFloat(value, -1.2f, 1.2f);
  }

  static inline float smoothstep01(float value) {
    const float x = clampFloat(value, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
  }

  static inline float percentToUnit(int32_t value) {
    return clampFloat(static_cast<float>(value) * 0.01f, 0.0f, 1.0f);
  }

  static inline float dbToLin(float db) {
    return powf(10.0f, db * 0.05f);
  }

  static inline float timeToCoef(float seconds) {
    return expf(-1.0f / (static_cast<float>(kSampleRate) * seconds));
  }

  static inline float freqFromNorm(float norm, float min_hz, float max_hz) {
    const float t = clampFloat(norm, 0.0f, 1.0f);
    return min_hz * powf(max_hz / min_hz, t);
  }

  static inline float saturateSoft(float x) {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  static inline float saturateHard(float x) {
    return clampFloat(x, -1.0f, 1.0f);
  }

  static inline float saturateTape(float x) {
    const float biased = x + 0.055f;
    return saturateSoft(biased) - 0.052f;
  }

  static inline float saturateFold(float x) {
    if (x > 1.0f || x < -1.0f) {
      x = 2.0f * fabsf(0.5f * x + 0.5f -
                       floorf(0.5f * x + 1.0f)) -
          1.0f;
    }
    return x;
  }

  static inline float processSaturation(float x, float drive_gain,
                                        float post_gain, int32_t mode) {
    x *= drive_gain;
    switch (mode) {
      case 1:
        x = saturateHard(x);
        break;
      case 2:
        x = saturateTape(x);
        break;
      case 3:
        x = saturateFold(x);
        break;
      case 0:
      default:
        x = saturateSoft(x);
        break;
    }
    return x * post_gain;
  }

  inline float tapePreEmphasis(float x, uint8_t ch) {
    const float alpha = 0.18f;
    const float amount = 0.35f;
    tape_pre_lp_[ch] += alpha * (x - tape_pre_lp_[ch]);
    const float hp = x - tape_pre_lp_[ch];
    return x + amount * hp;
  }

  inline float tapeDeEmphasis(float x, uint8_t ch) {
    const float alpha = 0.10f;
    const float amount = 0.30f;
    tape_post_lp_[ch] += alpha * (x - tape_post_lp_[ch]);
    const float hp = x - tape_post_lp_[ch];
    return x - amount * hp;
  }

  static inline FilterSettings getFilterSettings(int32_t filter,
                                                 int32_t resonance,
                                                 int32_t slope) {
    FilterSettings settings;
    settings.mode = static_cast<uint8_t>(slope);
    settings.cascade = slope == 1;

    const float shaped = smoothstep01(percentToUnit(resonance));
    const float q = 0.55f + shaped * 7.0f;
    settings.r2 = 1.0f / q;

    const int32_t distance = filter - 50;
    if (distance >= -4 && distance <= 4) {
      settings.bypass = true;
      return settings;
    }

    if (slope == 2) {
      const float freq = freqFromNorm(percentToUnit(filter), 80.0f, 14000.0f);
      settings.g = tanf(kPi * freq / static_cast<float>(kSampleRate));
      settings.bypass = false;
      return settings;
    }

    const float amount = fabsf(static_cast<float>(distance)) / 50.0f;
    const float freq = distance < 0
                           ? freqFromNorm(1.0f - amount, 35.0f, 19000.0f)
                           : freqFromNorm(amount, 25.0f, 15500.0f);
    settings.mode = distance < 0 ? 0 : 1;
    settings.g = tanf(kPi * freq / static_cast<float>(kSampleRate));
    settings.bypass = false;
    return settings;
  }

  inline void processFilterPair(float &l, float &r,
                                const FilterSettings &settings) {
    if (settings.bypass)
      return;

    l = processFilterSample(l, 0, settings);
    r = processFilterSample(r, 1, settings);
  }

  inline float processFilterSample(float x, uint8_t ch,
                                   const FilterSettings &settings) {
    float low = 0.0f;
    float band = 0.0f;
    float high = 0.0f;

    filter_a_[ch].process(x, settings.g, settings.r2, low, band, high);
    float y = filterOutput(low, band, high, settings);

    if (settings.cascade) {
      filter_b_[ch].process(y, settings.g, settings.r2, low, band, high);
      y = filterOutput(low, band, high, settings);
    }

    return y;
  }

  static inline float filterOutput(float low, float band, float high,
                                   const FilterSettings &settings) {
    (void)band;
    if (settings.mode == 0)
      return low;
    if (settings.mode == 1)
      return high;
    return low + high;
  }

  inline void processCompressor(float &l, float &r, float threshold_lin,
                                float ratio, float attack_coef,
                                float release_coef) {
    const float detector = sqrtf(0.5f * (l * l + r * r) + 1.0e-12f);
    const float coef = detector > comp_env_ ? attack_coef : release_coef;
    comp_env_ = coef * comp_env_ + (1.0f - coef) * detector;

    float target_gain = 1.0f;
    const float env_db = 20.0f * log10f(comp_env_ + 1.0e-12f);
    const float threshold_db = 20.0f * log10f(threshold_lin);
    const float over_db = env_db - threshold_db;
    const float knee_db = 6.0f;

    if (2.0f * over_db <= -knee_db) {
      target_gain = 1.0f;
    } else if (2.0f * over_db >= knee_db) {
      const float gain_db = (1.0f / ratio - 1.0f) * over_db;
      target_gain = dbToLin(gain_db);
    } else {
      const float knee_pos = over_db + 0.5f * knee_db;
      const float gain_db =
          (1.0f / ratio - 1.0f) * knee_pos * knee_pos / (2.0f * knee_db);
      target_gain = dbToLin(gain_db);
    }

    gain_smooth_ = 0.985f * gain_smooth_ + 0.015f * target_gain;
    l *= gain_smooth_;
    r *= gain_smooth_;
  }

  inline void processBitcrush(float &l, float &r, int32_t bits, int32_t drive) {
    const int32_t stride = 1 + (drive / 13);
    if (crush_counter_ <= 0) {
      crush_hold_l_ = quantize(l, bits);
      crush_hold_r_ = quantize(r, bits);
      crush_counter_ = stride;
    }
    --crush_counter_;
    l = crush_hold_l_;
    r = crush_hold_r_;
  }

  inline void postCrushLowpass(float &l, float &r) {
    const float alpha = 0.2f;
    crush_lp_l_ += alpha * (l - crush_lp_l_);
    crush_lp_r_ += alpha * (r - crush_lp_r_);
    l = crush_lp_l_;
    r = crush_lp_r_;
  }

  static inline float quantize(float x, int32_t bits) {
    const int32_t clamped_bits = clampInt(bits, 4, 16);
    const float levels = static_cast<float>(1U << clamped_bits);
    const float scaled = x * levels;
    return (scaled >= 0.0f ? floorf(scaled + 0.5f) : ceilf(scaled - 0.5f)) /
           levels;
  }

  std::atomic<int32_t> params_[kNumParams];
  TptSvf filter_a_[2];
  TptSvf filter_b_[2];
  float comp_env_ = 0.0f;
  float gain_smooth_ = 1.0f;
  float crush_hold_l_ = 0.0f;
  float crush_hold_r_ = 0.0f;
  int32_t crush_counter_ = 0;
  float crush_lp_l_ = 0.0f;
  float crush_lp_r_ = 0.0f;
  float tape_pre_lp_[2] = {0.0f, 0.0f};
  float tape_post_lp_[2] = {0.0f, 0.0f};
  uint8_t input_channels_ = 4;
};

const int32_t MasterFX::kParamMin[12] = {
    0, 0, 0, 0, -600, 1, 1, 10, 0, 0, 0, 4};
const int32_t MasterFX::kParamMax[12] = {
    100, 100, 2, 0, 0, 20, 200, 2000, 100, 3, 1, 16};
const int32_t MasterFX::kParamInit[12] = {
    50, 15, 0, 0, -180, 4, 10, 300, 0, 0, 0, 16};
const char * const MasterFX::kSlopeNames[3] = {"12dB", "24dB", "NOTCH"};
const char * const MasterFX::kSatModeNames[4] = {"SOFT", "HARD", "TAPE",
                                                 "FOLD"};
