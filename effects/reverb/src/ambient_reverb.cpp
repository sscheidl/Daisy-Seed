// SPDX-License-Identifier: MIT
#include "ambient_reverb.hpp"
namespace ambient {
namespace {
constexpr float kTankSeconds[8] = {.0593f, .0719f, .0835f, .0971f, .1097f, .1271f, .1399f, .1573f};
constexpr float kApSeconds[8] = {.0071f, .0083f, .0113f, .0131f, .0173f, .0191f, .0217f, .0239f};
constexpr float kInputSeconds[8] = {.0041f, .0097f, .0137f, .0193f, .0053f, .0089f, .0157f, .0181f};
constexpr float kLfoRatios[8] = {1.f, .79f, 1.13f, .61f, .47f, 1.37f, .91f, 1.61f};
constexpr float kOutLeft[8] = {1, 1, -1, 1, -1, -1, 1, -1};
constexpr float kOutRight[8] = {1, -1, 1, 1, 1, -1, -1, -1};
float Pole(float hz, float fs) {
    return std::exp(-2 * kPi * hz / fs);
}
} // namespace
bool AmbientReverb::Prepare(float sampleRate, Memory &memory) {
    if (!std::isfinite(sampleRate) || sampleRate < 32000 || sampleRate > 96000)
        return false;
    sampleRate_ = sampleRate;
    ready_ = true;
    state_ = {};
    feedback_ = {};
    diagnostics_ = {};
    envelope_ = 0;
    swell_ = 1;
    gate_ = false;
    for (unsigned i = 0; i < 8; ++i) {
        tank_[i].Bind(memory.tank[i]);
        tankAp_[i].Bind(memory.tankAllpass[i]);
        inputAp_[i].Bind(memory.inputAllpass[i]);
        tankLength_[i] = unsigned(std::round(kTankSeconds[i] * sampleRate));
        apLength_[i] = unsigned(std::round(kApSeconds[i] * sampleRate));
        inputLength_[i] = unsigned(std::round(kInputSeconds[i] * sampleRate));
        damping_[i] = {};
        highpass_[i] = {};
        lfo_[i].Seed(i);
    }
    pre_[0].Bind(memory.preDelay[0]);
    pre_[1].Bind(memory.preDelay[1]);
    up_.Prepare(sampleRate, memory.pitch[0], memory.pitch[1], 2.f);
    down_.Prepare(sampleRate, memory.pitch[2], memory.pitch[3], .5f);
    waveSmoothing_ = 1 - Pole(8.f, sampleRate);
    envelopeRelease_ = Pole(2.f, sampleRate);
    SetParameters(parameters_, true);
    return true;
}
void AmbientReverb::SetParameters(const Parameters &input, bool immediate) {
    Parameters p = input;
    if (unsigned(p.mode) > unsigned(Mode::Scatter))
        p.mode = Mode::Cloud;
    if (unsigned(p.shape) > unsigned(Shape::Random))
        p.shape = Shape::Sine;
    p.decaySeconds = Bounded(p.decaySeconds, 1, 45, 14);
    p.toneHz = Bounded(p.toneHz, 400, 14000, 5500);
    p.lowCutHz = Bounded(p.lowCutHz, 20, 350, 65);
    p.preDelayMs = Bounded(p.preDelayMs, 0, 150, 25);
    p.modRateHz = Bounded(p.modRateHz, .03f, 1.5f, .18f);
    p.modDepthMs = Bounded(p.modDepthMs, 0, 3, .8f);
    p.width = Bounded(p.width, 0, 1, 1);
    p.mix = Bounded(p.mix, 0, 1, .45f);
    p.diffusion = Bounded(p.diffusion, 0, 1, .85f);
    p.texture = Bounded(p.texture, 0, 1, .4f);
    p.riseSeconds = Bounded(p.riseSeconds, .05f, 4, 1);
    p.outputGain = Bounded(p.outputGain, 0, 1, .8f);
    parameters_ = p;
    if (!ready_)
        return;
    unsigned smooth = immediate ? 0 : unsigned(sampleRate_ * .04f);
    for (unsigned i = 0; i < 8; ++i) {
        float seconds = float(tankLength_[i] + apLength_[i]) / sampleRate_;
        state_.gain[i].Set(std::exp(-6.907755279f * seconds /
                                    (p.sustain ? 45.f : p.decaySeconds)), smooth);
    }
    for (unsigned i = 0; i < 5; ++i)
        state_.mode[i].Set(i == unsigned(p.mode) ? 1.f : 0.f, smooth);
    state_.tone.Set(1 - Pole(std::min(p.toneHz, .4f * sampleRate_), sampleRate_), smooth);
    state_.highpass.Set(Pole(p.lowCutHz, sampleRate_), smooth);
    state_.preDelay.Set(p.preDelayMs * .001f * sampleRate_, smooth);
    state_.depth.Set(p.modDepthMs * .001f * sampleRate_, smooth);
    state_.rate.Set(2 * std::sin(kPi * p.modRateHz / sampleRate_), smooth);
    state_.width.Set(p.width, smooth);
    const float dry = p.bypass ? 1.f : (p.mix == 1 ? 0.f : std::cos(p.mix * kPi * .5f));
    const float wet = p.bypass && !p.trails ? 0.f : std::sin(p.mix * kPi * .5f);
    state_.dry.Set(dry, smooth);
    state_.wet.Set(wet, smooth);
    state_.diffusion.Set(p.diffusion, smooth);
    state_.texture.Set(p.texture, smooth);
    state_.riseStep.Set(1 / (p.riseSeconds * sampleRate_), smooth);
    state_.level.Set(p.outputGain, smooth);
    state_.send.Set(p.bypass ? 0.f : 1.f, smooth);
    state_.hold.Set(p.hold ? 1.f : 0.f, immediate ? 0 : unsigned(sampleRate_ * .3f));
    state_.sustain.Set(p.sustain ? 1.f : 0.f, smooth);
}
float AmbientReverb::Guard(float x) {
    if (x > 8) {
        ++diagnostics_.guardEvents;
        return 8;
    }
    if (x < -8) {
        ++diagnostics_.guardEvents;
        return -8;
    }
    return x;
}
Stereo AmbientReverb::Process(Stereo in) {
    in = {Input(in.left), Input(in.right)};
    if (!ready_)
        return in;
    const float hold = state_.hold.Next(), send = state_.send.Next();
    const float tone = state_.tone.Next(), hp = state_.highpass.Next(),
                depth = state_.depth.Next() * (1 - hold), rate = state_.rate.Next();
    const float sustain = state_.sustain.Next();
    const float diffusion = state_.diffusion.Next(), texture = state_.texture.Next();
    std::array<float, 5> mode{};
    for (unsigned i = 0; i < 5; ++i)
        mode[i] = state_.mode[i].Next();
    std::array<float, 8> gain{};
    float maximumGain = 0;
    for (unsigned i = 0; i < 8; ++i) {
        gain[i] = state_.gain[i].Next();
        maximumGain = std::max(maximumGain, gain[i]);
    }
    envelope_ =
        std::max(std::max(std::abs(in.left), std::abs(in.right)), envelope_ * envelopeRelease_);
    if (!gate_ && envelope_ > .004f) {
        gate_ = true;
        swell_ = 0;
    }
    if (gate_ && envelope_ < .0015f)
        gate_ = false;
    swell_ = std::min(1.f, swell_ + state_.riseStep.Next());
    const float swellGain = swell_ * swell_ * (3 - 2 * swell_);
    pre_[0].Push(in.left * send);
    pre_[1].Push(in.right * send);
    const float pd = state_.preDelay.Next();
    Stereo x{pre_[0].Read(pd + 1), pre_[1].Read(pd + 1)};
    Stereo early{.35f * pre_[0].Read(pd + .029f * sampleRate_ + 1) +
                     .25f * pre_[1].Read(pd + .089f * sampleRate_ + 1) -
                     .15f * pre_[0].Read(pd + .137f * sampleRate_ + 1),
                 .35f * pre_[1].Read(pd + .037f * sampleRate_ + 1) +
                     .25f * pre_[0].Read(pd + .101f * sampleRate_ + 1) -
                     .15f * pre_[1].Read(pd + .149f * sampleRate_ + 1)};
    const float loopSend = .35f * (1 - maximumGain) * (1 - hold);
    Stereo pitchIn{.7f * x.left + loopSend * feedback_.left,
                   .7f * x.right + loopSend * feedback_.right};
    const Stereo octaveUp = up_.Process(pitchIn), octaveDown = down_.Process(pitchIn);
    const float amount = .5f * texture;
    x.left += amount * (mode[2] * octaveDown.left + mode[3] * octaveUp.left);
    x.right += amount * (mode[2] * octaveDown.right + mode[3] * octaveUp.right);
    float l = x.left, r = x.right;
    for (unsigned i = 0; i < 4; ++i) {
        l = inputAp_[i].Allpass(l, inputLength_[i], .6f);
        r = inputAp_[i + 4].Allpass(r, inputLength_[i + 4], .6f);
    }
    const float diffuse = diffusion * (1 - .8f * mode[4] * (1 - texture));
    x.left += (l - x.left) * diffuse;
    x.right += (r - x.right) * diffuse;
    std::array<float, 8> y{};
    Stereo wet{};
    for (unsigned i = 0; i < 8; ++i) {
        const float mod = lfo_[i].Process(rate * kLfoRatios[i], parameters_.shape, waveSmoothing_);
        y[i] = tank_[i].Read(float(tankLength_[i]) +
                            depth * (1 + .75f * texture * mode[0] * sustain) * mod);
        y[i] = tankAp_[i].Allpass(y[i], apLength_[i], .5f);
        wet.left += y[i] * kOutLeft[i] * .3535533906f;
        wet.right += y[i] * kOutRight[i] * .3535533906f;
        float filtered = highpass_[i].Process(y[i], hp);
        filtered = damping_[i].Process(filtered, tone);
        y[i] = (filtered + (y[i] - filtered) * hold) * (gain[i] + (1 - gain[i]) * hold);
    }
    Scatter(y);
    const float inject = .45f * (1 - hold);
    const float inputL = .5f * inject * x.left, inputR = .5f * inject * x.right;
    const float excitation[8] = {inputL,  inputR, inputL,  -inputR,
                                 -inputL, inputR, -inputL, -inputR};
    for (unsigned i = 0; i < 8; ++i)
        tank_[i].Push(Guard(y[i] + excitation[i]));
    feedback_ = wet;
    const float lateLevel = .65f - .3f * mode[4],
                earlyLevel = .12f + mode[4] * (.2f + .4f * texture);
    wet.left = lateLevel * wet.left + earlyLevel * early.left * (1 - hold);
    wet.right = lateLevel * wet.right + earlyLevel * early.right * (1 - hold);
    const float bloom = 1 - mode[1] * (1 - swellGain);
    wet.left *= bloom;
    wet.right *= bloom;
    const float mid = .5f * (wet.left + wet.right),
                side = .5f * (wet.left - wet.right) * state_.width.Next();
    wet = {mid + side, mid - side};
    diagnostics_.peakWet =
        std::max({diagnostics_.peakWet, std::abs(wet.left), std::abs(wet.right)});
    ++diagnostics_.samples;
    const float dry = state_.dry.Next(), wetMix = state_.wet.Next(), level = state_.level.Next();
    return {level * (dry * in.left + wetMix * wet.left),
            level * (dry * in.right + wetMix * wet.right)};
}
void AmbientReverb::ProcessBlock(const float *l, const float *r, float *ol, float *or_,
                                 std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const Stereo y = Process({l[i], r[i]});
        ol[i] = y.left;
        or_[i] = y.right;
    }
}
Parameters AmbientReverb::Preset(Mode mode) {
    Parameters p;
    p.mode = mode;
    switch (mode) {
    case Mode::Bloom:
        p.decaySeconds = 18;
        p.riseSeconds = 1.4f;
        p.mix = .65f;
        p.toneHz = 4200;
        break;
    case Mode::Abyss:
        p.decaySeconds = 22;
        p.toneHz = 2700;
        p.lowCutHz = 40;
        p.texture = .8f;
        p.mix = .6f;
        p.modDepthMs = 1.1f;
        break;
    case Mode::Halo:
        p.decaySeconds = 20;
        p.toneHz = 8000;
        p.texture = .75f;
        p.mix = .6f;
        p.modDepthMs = .6f;
        break;
    case Mode::Scatter:
        p.decaySeconds = 10;
        p.texture = .8f;
        p.diffusion = .45f;
        p.preDelayMs = 8;
        p.mix = .55f;
        break;
    default:
        p.mode = Mode::Cloud;
        break;
    }
    return p;
}
} // namespace ambient
