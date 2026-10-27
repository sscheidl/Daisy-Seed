// SPDX-License-Identifier: MIT
#include "wet_path.hpp"

namespace ambient {
bool WetPath::Prepare(float sampleRate, Memory &memory) {
    if (!std::isfinite(sampleRate) || sampleRate < 32000 || sampleRate > 96000)
        return false;
    sampleRate_ = sampleRate;
    ready_ = false;
    historyLeft_.fill(0);
    historyRight_.fill(0);
    historyIndex_ = 0;
    phase_ = 0;
    previousWet_ = currentWet_ = {};
    ratio_ = targetRatio_ = std::pow(.25f, Bounded(parameters_.stretch, 0, 1, 0));
    filterRatio_ = -1;
    DesignFilterBank();
    ConfigureFilter(ratio_);
    Parameters wetParameters = parameters_;
    wetParameters.mix = 1;
    wetParameters.outputGain = 1;
    wetParameters.bypass = false;
    wetParameters.trails = true;
    wetParameters.hold = false;
    core_.SetParameters(wetParameters);
    if (!core_.Prepare(sampleRate, memory))
        return false;
    const float dry = parameters_.bypass ? 1.f : std::cos(parameters_.mix * kPi * .5f);
    dry_.Set(dry, 0);
    wet_.Set(std::sin(parameters_.mix * kPi * .5f), 0);
    send_.Set(parameters_.bypass ? 0.f : 1.f, 0);
    panLeft_.Set(parameters_.pan > 0 ? 1 - parameters_.pan : 1, 0);
    panRight_.Set(parameters_.pan < 0 ? 1 + parameters_.pan : 1, 0);
    mono_.Set(0, 0);
    ready_ = true;
    return true;
}
void WetPath::SetParameters(const Parameters &input) {
    Parameters p = input;
    p.mix = Bounded(p.mix, 0, 1, .45f);
    p.pan = Bounded(p.pan, -1, 1, 0);
    p.stretch = Bounded(p.stretch, 0, 1, 0);
    p.outputGain = Bounded(p.outputGain, 0, 1, .8f);
    parameters_ = p;
    targetRatio_ = std::pow(.25f, p.stretch);
    if (!ready_)
        return;
    Parameters wetParameters = p;
    wetParameters.mix = 1;
    wetParameters.outputGain = 1;
    wetParameters.bypass = false;
    wetParameters.trails = true;
    wetParameters.hold = false;
    core_.SetParameters(wetParameters);
    const unsigned smooth = unsigned(sampleRate_ * .04f);
    dry_.Set(p.bypass ? 1.f : std::cos(p.mix * kPi * .5f), smooth);
    wet_.Set(std::sin(p.mix * kPi * .5f), smooth);
    send_.Set(p.bypass ? 0.f : 1.f, smooth);
    panLeft_.Set(p.pan > 0 ? 1 - p.pan : 1, smooth);
    panRight_.Set(p.pan < 0 ? 1 + p.pan : 1, smooth);
    ServiceFilter();
}
void WetPath::SetLegacyMono(bool enabled) {
    mono_.Set(enabled ? 1.f : 0.f, unsigned(sampleRate_ * .04f));
}
void WetPath::ServiceFilter() {
    if (ready_)
        ConfigureFilter(std::min(ratio_, targetRatio_));
}
void WetPath::DesignFilterBank() {
    for (unsigned band = 0; band < kFilterBands; ++band) {
        const float ratio = .25f + .75f * float(band) / float(kFilterBands - 1);
        // Keep the stopband below the new Nyquist rate at maximum stretch.
        const float cutoff = .35f * ratio + .1f * (ratio - .25f) / .75f;
        float sum = 0;
        for (unsigned i = 0; i < kTaps; ++i) {
            const float t = float(i) - float(kTaps - 1) * .5f;
            const float sinc = std::abs(t) < 1.e-6f
                                   ? 2 * cutoff
                                   : std::sin(2 * kPi * cutoff * t) / (kPi * t);
            const float window = .42f - .5f * std::cos(2 * kPi * float(i) / float(kTaps - 1)) +
                                 .08f * std::cos(4 * kPi * float(i) / float(kTaps - 1));
            coefficientBank_[band][i] = sinc * window;
            sum += coefficientBank_[band][i];
        }
        for (float &c : coefficientBank_[band])
            c /= sum;
    }
}
void WetPath::ConfigureFilter(float safeRatio) {
    safeRatio = Bounded(safeRatio, .25f, 1.f, 1.f);
    if (std::abs(safeRatio - filterRatio_) < .003f)
        return;
    filterRatio_ = safeRatio;
    const float position = (safeRatio - .25f) * float(kFilterBands - 1) / .75f;
    const unsigned lower = std::min(unsigned(position), kFilterBands - 1);
    const unsigned upper = std::min(lower + 1, kFilterBands - 1);
    const float blend = position - float(lower);
    for (unsigned i = 0; i < kTaps; ++i)
        coefficients_[i] = coefficientBank_[lower][i] +
                           blend * (coefficientBank_[upper][i] - coefficientBank_[lower][i]);
}
Stereo WetPath::Filter(Stereo input) {
#if AMBIENT_CONTIGUOUS_FIR
    historyLeft_[historyIndex_] = historyLeft_[historyIndex_ + kTaps] = input.left;
    historyRight_[historyIndex_] = historyRight_[historyIndex_ + kTaps] = input.right;
    // Newest first, as in the ring version: tap k reads the sample k steps back.
    const float *left = historyLeft_.data() + historyIndex_ + kTaps;
    const float *right = historyRight_.data() + historyIndex_ + kTaps;
    Stereo result{};
    for (unsigned k = 0; k < kTaps; ++k) {
        result.left += coefficients_[k] * left[-int(k)];
        result.right += coefficients_[k] * right[-int(k)];
    }
#else
    historyLeft_[historyIndex_] = input.left;
    historyRight_[historyIndex_] = input.right;
    Stereo result{};
    unsigned index = historyIndex_;
    for (float c : coefficients_) {
        result.left += c * historyLeft_[index];
        result.right += c * historyRight_[index];
        index = (index - 1) & (kTaps - 1);
    }
#endif
    historyIndex_ = (historyIndex_ + 1) & (kTaps - 1);
    return result;
}
Stereo WetPath::Process(Stereo input, bool rightConnected) {
    input = {Input(input.left), Input(input.right)};
    if (!ready_)
        return input;
    if (!rightConnected)
        input.right = input.left;
    ratio_ += .0008f * (targetRatio_ - ratio_); // ~26 ms at 48 kHz.
    const float mono = mono_.Next();
    const float midInput = .5f * (input.left + input.right);
    const Stereo adapted = mono >= 1.f
                               ? Stereo{midInput, midInput}
                               : Stereo{input.left + mono * (midInput - input.left),
                                        input.right + mono * (midInput - input.right)};
    const float send = send_.Next();
    const Stereo filtered = Filter({adapted.left * send, adapted.right * send});
    phase_ += ratio_;
    if (phase_ >= 1.f) {
        phase_ -= 1.f;
        previousWet_ = currentWet_;
        currentWet_ = core_.Process(filtered);
    }
    const Stereo wet{previousWet_.left + phase_ * (currentWet_.left - previousWet_.left),
                     previousWet_.right + phase_ * (currentWet_.right - previousWet_.right)};
    const float left = wet.left * panLeft_.Next(), right = wet.right * panRight_.Next();
    // Pan has no meaning in Legacy Mono; avoid a 6 dB drop at either pan end.
    const float wetMono = .5f * (wet.left + wet.right);
    const float wetLeft = mono >= 1.f ? wetMono : left + mono * (wetMono - left);
    const float wetRight = mono >= 1.f ? wetMono : right + mono * (wetMono - right);
    const float dry = dry_.Next(), wetGain = wet_.Next(), level = parameters_.outputGain;
    return {level * (dry * adapted.left + wetGain * wetLeft),
            level * (dry * adapted.right + wetGain * wetRight)};
}
} // namespace ambient
