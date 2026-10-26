#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include "unit.h"

class TapeVibe {
 public:
  TapeVibe() {
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
    for (uint8_t ch = 0; ch < 2; ++ch) {
      head_bump_lp_[ch] = 0.0f;
      tone_lp_[ch] = 0.0f;
      hiss_lp_[ch] = 0.0f;
      dropout_lp_[ch] = 0.0f;
      sat_env_[ch] = 0.0f;
      dropout_alpha_[ch] = 0.0f;
    }

    for (size_t i = 0; i < kDelayBufferSize; ++i) {
      delay_l_[i] = 0.0f;
      delay_r_[i] = 0.0f;
    }

    delay_index_ = 0;
    wow_phase_ = 0.0f;
    flutter_phase_a_ = 0.0f;
    flutter_phase_b_ = 0.0f;
    dropout_count_ = 0;
    dropout_gain_l_ = 1.0f;
    dropout_gain_r_ = 1.0f;
    rng_ = 0x12345678U;
  }

  inline void Resume() {}
  inline void Suspend() {}

  fast_inline void Process(const float * in, float * out, size_t frames) {
    const float drive = percentToUnit(getClampedParam(kParamDrive));
    const float tone = percentToUnit(getClampedParam(kParamTone));
    const float age = percentToUnit(getClampedParam(kParamAge));
    const float mix = percentToUnit(getClampedParam(kParamMix));
    const float wow = percentToUnit(getClampedParam(kParamWow));
    const float flutter = percentToUnit(getClampedParam(kParamFlutter));
    const float hiss = percentToUnit(getClampedParam(kParamHiss));
    const int32_t hiss_type = getClampedParam(kParamHissType);
    const float dropout = percentToUnit(getClampedParam(kParamDropout));
    const int32_t drop_type = getClampedParam(kParamDropType);

    const float drive_gain = 1.0f + drive * 9.0f;
    const float post_gain = (1.0f + 0.08f * drive) / (1.0f + 0.75f * drive + 0.2f * age);

    const float * __restrict in_p = in;
    float * __restrict out_p = out;
    for (size_t i = 0; i < frames; ++i, in_p += input_channels_, out_p += 2) {
      const float dry_l = in_p[0];
      const float dry_r = in_p[1];

      float wet_l = processHeadBump(dry_l, 0, age);
      float wet_r = processHeadBump(dry_r, 1, age);

      wet_l = processTapeDrive(wet_l, 0, drive_gain, drive, age);
      wet_r = processTapeDrive(wet_r, 1, drive_gain, drive, age);

      processWowFlutter(wet_l, wet_r, wow, flutter, age);
      applyDropout(wet_l, wet_r, dropout, drop_type, age);

      wet_l = processTone(wet_l, 0, tone, age);
      wet_r = processTone(wet_r, 1, tone, age);

      wet_l += generateHiss(0, hiss, hiss_type, age);
      wet_r += generateHiss(1, hiss, hiss_type, age);

      wet_l *= post_gain;
      wet_r *= post_gain;

      out_p[0] = clampAudio(dry_l + (wet_l - dry_l) * mix);
      out_p[1] = clampAudio(dry_r + (wet_r - dry_r) * mix);
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
      case kParamHissType:
        return kNoiseTypeNames[clampInt(value, 0, 2)];
      case kParamDropType:
        return kDropTypeNames[clampInt(value, 0, 2)];
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
    kParamDrive = 0,
    kParamTone,
    kParamAge,
    kParamMix,
    kParamWow,
    kParamFlutter,
    kParamHiss,
    kParamHissType,
    kParamDropout,
    kParamDropType,
    kNumParams
  };

  static constexpr uint32_t kSampleRate = 48000U;
  static constexpr size_t kDelayBufferSize = 2048U;
  static constexpr float kPi = 3.14159265358979323846f;

  static const int32_t kParamMin[kNumParams];
  static const int32_t kParamMax[kNumParams];
  static const int32_t kParamInit[kNumParams];
  static const char * const kNoiseTypeNames[3];
  static const char * const kDropTypeNames[3];

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

  static inline float percentToUnit(int32_t value) {
    return clampFloat(static_cast<float>(value) * 0.01f, 0.0f, 1.0f);
  }

  static inline float saturateSoft(float x) {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
  }

  static inline float wrapPhase(float phase) {
    while (phase >= 2.0f * kPi)
      phase -= 2.0f * kPi;
    while (phase < 0.0f)
      phase += 2.0f * kPi;
    return phase;
  }

  inline float nextRand01() {
    rng_ = 1664525U * rng_ + 1013904223U;
    return static_cast<float>((rng_ >> 8) & 0x00FFFFFFU) / 16777215.0f;
  }

  inline float nextRandSigned() { return nextRand01() * 2.0f - 1.0f; }

  inline float processHeadBump(float x, uint8_t ch, float age) {
    const float alpha = 0.010f + 0.010f * (1.0f - age);
    head_bump_lp_[ch] += alpha * (x - head_bump_lp_[ch]);
    return x + head_bump_lp_[ch] * (0.04f + 0.12f * age);
  }

  inline float processTapeDrive(float x, uint8_t ch, float drive_gain,
                                float drive, float age) {
    sat_env_[ch] = 0.995f * sat_env_[ch] + 0.005f * fabsf(x);
    const float compression = 1.0f / (1.0f + sat_env_[ch] * (0.35f + 0.9f * age + 0.75f * drive));
    return saturateSoft(x * drive_gain * compression);
  }

  inline void processWowFlutter(float &l, float &r, float wow, float flutter,
                                float age) {
    const float wow_rate = 0.25f + 0.55f * age;
    const float flutter_rate_a = 6.0f + 4.0f * age;
    const float flutter_rate_b = 9.0f + 5.0f * age;

    wow_phase_ = wrapPhase(wow_phase_ + 2.0f * kPi * wow_rate / static_cast<float>(kSampleRate));
    flutter_phase_a_ = wrapPhase(flutter_phase_a_ + 2.0f * kPi * flutter_rate_a / static_cast<float>(kSampleRate));
    flutter_phase_b_ = wrapPhase(flutter_phase_b_ + 2.0f * kPi * flutter_rate_b / static_cast<float>(kSampleRate));

    delay_l_[delay_index_] = l;
    delay_r_[delay_index_] = r;

    const float wow_depth = wow * (0.6f + 4.5f * age);
    const float flutter_depth = flutter * (0.25f + 1.4f * age);
    const float mod_delay = 10.0f + age * 8.0f +
                            wow_depth * sinf(wow_phase_) +
                            flutter_depth * (0.7f * sinf(flutter_phase_a_) +
                                             0.3f * sinf(flutter_phase_b_));

    const int32_t base = static_cast<int32_t>(mod_delay);
    const float frac = mod_delay - static_cast<float>(base);

    int32_t read0 = static_cast<int32_t>(delay_index_) - base;
    while (read0 < 0)
      read0 += static_cast<int32_t>(kDelayBufferSize);
    int32_t read1 = read0 - 1;
    while (read1 < 0)
      read1 += static_cast<int32_t>(kDelayBufferSize);

    l = delay_l_[read0] * (1.0f - frac) + delay_l_[read1] * frac;
    r = delay_r_[read0] * (1.0f - frac) + delay_r_[read1] * frac;

    delay_index_ = (delay_index_ + 1U) % kDelayBufferSize;
  }

  inline void startDropout(int32_t drop_type, float dropout, float age) {
    (void)dropout;
    (void)age;
    const float random = nextRand01();

    int32_t min_len = 32;
    int32_t max_len = 120;
    float min_gain = 0.80f;
    float tone = 0.15f;

    switch (drop_type) {
      case 0:
        min_len = 24;
        max_len = 80;
        min_gain = 0.88f;
        tone = 0.10f;
        break;
      case 2:
        min_len = 64;
        max_len = 420;
        min_gain = 0.18f;
        tone = 0.55f;
        break;
      case 1:
      default:
        min_len = 40;
        max_len = 220;
        min_gain = 0.45f;
        tone = 0.32f;
        break;
    }

    dropout_count_ = min_len + static_cast<int32_t>((max_len - min_len) * random);
    dropout_gain_l_ = lerp(min_gain, 0.98f, nextRand01());
    dropout_gain_r_ = lerp(min_gain, 0.98f, nextRand01());

    if (drop_type == 2 && nextRand01() > 0.55f) {
      dropout_gain_l_ *= 0.6f;
    }
    if (drop_type == 2 && nextRand01() > 0.55f) {
      dropout_gain_r_ *= 0.6f;
    }

    dropout_alpha_[0] = 0.05f + tone * 0.20f;
    dropout_alpha_[1] = 0.05f + tone * 0.20f;
  }

  inline void applyDropout(float &l, float &r, float dropout, int32_t drop_type,
                           float age) {
    if (dropout <= 0.001f)
      return;

    if (dropout_count_ <= 0) {
      float probability = dropout * dropout * 0.00065f;
      if (drop_type == 1)
        probability *= 1.8f;
      else if (drop_type == 2)
        probability *= 3.4f;
      probability *= 0.6f + 0.9f * age;

      if (nextRand01() < probability) {
        startDropout(drop_type, dropout, age);
      }
    }

    if (dropout_count_ > 0) {
      dropout_lp_[0] += dropout_alpha_[0] * (l - dropout_lp_[0]);
      dropout_lp_[1] += dropout_alpha_[1] * (r - dropout_lp_[1]);
      l = dropout_lp_[0] * dropout_gain_l_;
      r = dropout_lp_[1] * dropout_gain_r_;
      --dropout_count_;
    }
  }

  inline float processTone(float x, uint8_t ch, float tone, float age) {
    const float alpha = 0.08f + (0.42f * tone) * (1.0f - 0.35f * age);
    tone_lp_[ch] += alpha * (x - tone_lp_[ch]);
    const float high = x - tone_lp_[ch];

    if (tone < 0.5f) {
      const float dark = (0.5f - tone) * 2.0f;
      return x - high * (0.85f * dark + 0.15f * age);
    }

    const float bright = (tone - 0.5f) * 2.0f;
    return x + high * (0.28f * bright);
  }

  inline float generateHiss(uint8_t ch, float hiss, int32_t hiss_type, float age) {
    if (hiss <= 0.0001f)
      return 0.0f;

    float noise = nextRandSigned();
    float lp_alpha = 0.02f;
    float level = 0.0006f;

    switch (hiss_type) {
      case 0:
        lp_alpha = 0.015f;
        level = 0.0015f;
        break;
      case 2:
        lp_alpha = 0.08f;
        level = 0.0085f;
        break;
      case 1:
      default:
        lp_alpha = 0.04f;
        level = 0.0045f;
        break;
    }

    hiss_lp_[ch] += lp_alpha * (noise - hiss_lp_[ch]);
    const float hp_noise = noise - hiss_lp_[ch];
    return hp_noise * level * hiss * (0.45f + 0.95f * age);
  }

  std::atomic<int32_t> params_[kNumParams];
  float head_bump_lp_[2] = {0.0f, 0.0f};
  float tone_lp_[2] = {0.0f, 0.0f};
  float hiss_lp_[2] = {0.0f, 0.0f};
  float dropout_lp_[2] = {0.0f, 0.0f};
  float sat_env_[2] = {0.0f, 0.0f};
  float delay_l_[kDelayBufferSize] = {};
  float delay_r_[kDelayBufferSize] = {};
  size_t delay_index_ = 0U;
  float wow_phase_ = 0.0f;
  float flutter_phase_a_ = 0.0f;
  float flutter_phase_b_ = 0.0f;
  int32_t dropout_count_ = 0;
  float dropout_gain_l_ = 1.0f;
  float dropout_gain_r_ = 1.0f;
  float dropout_alpha_[2] = {0.0f, 0.0f};
  uint32_t rng_ = 0x12345678U;
  uint8_t input_channels_ = 4;
};

const int32_t TapeVibe::kParamMin[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const int32_t TapeVibe::kParamMax[10] = {100, 100, 100, 100, 100, 100, 100, 2, 100, 2};
const int32_t TapeVibe::kParamInit[10] = {35, 50, 40, 40, 20, 15, 20, 1, 10, 1};
const char * const TapeVibe::kNoiseTypeNames[3] = {"STUDIO", "TAPE", "CASSETTE"};
const char * const TapeVibe::kDropTypeNames[3] = {"STUDIO", "TAPE", "CASSETTE"};
