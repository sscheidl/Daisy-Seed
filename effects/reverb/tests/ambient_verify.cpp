// SPDX-License-Identifier: MIT
#include "../src/ambient_reverb.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <stdexcept>
static bool audio = false;
static std::size_t allocations = 0;
[[gnu::noinline]] void *operator new(std::size_t n) {
    if (audio)
        ++allocations;
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept {
    std::free(p);
}
[[gnu::noinline]] void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete[](void *p) noexcept {
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}
using namespace ambient;
void Require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
float Energy(Stereo x) {
    return x.left * x.left + x.right * x.right;
}
struct Fixture {
    std::unique_ptr<Memory> memory = std::make_unique<Memory>();
    AmbientReverb fx;
    void Prepare(float fs, Parameters p = {}) {
        fx.SetParameters(p);
        Require(fx.Prepare(fs, *memory), "prepare failed");
    }
};
void Algebra() {
    std::array<float, 8> x{.4f, -.7f, .21f, -.3f, .6f, .9f, -.2f, .01f}, original = x;
    float a = 0, b = 0;
    for (auto v : x)
        a += v * v;
    Scatter(x);
    for (auto v : x)
        b += v * v;
    Require(std::abs(a - b) < 1e-6f, "matrix loses energy");
    Scatter(x);
    for (unsigned i = 0; i < 8; ++i)
        Require(std::abs(x[i] - original[i]) < 1e-6, "matrix inverse");
    std::array<float, 16> ram;
    Delay<16> d;
    d.Bind(ram);
    d.Push(.7f);
    Require(d.Read(1) == .7f, "delay age convention");
    d.Push(.2f);
    Require(d.Read(2) == .7f, "delay length");
    Require(std::abs(d.Read(1.5f) - .45f) < 1e-6, "fractional interpolation");
    Ramp r;
    r.Set(1, 4);
    for (int i = 0; i < 4; ++i)
        r.Next();
    Require(r.value == 1 && r.remaining == 0, "finite hold ramp");
    r.Set(0, 4);
    r.Next();
    r.Set(r.value, 4);
    const float stopped = r.value;
    for (int i = 0; i < 8; ++i)
        Require(r.Next() == stopped, "ramp retarget to current value drifts");
    r.Set(1, 4);
    r.Next();
    r.Set(1, 0);
    Require(r.Next() == 1 && r.remaining == 0, "immediate ramp target isn't immediate");
}
void Pitch() {
    auto ram = std::make_unique<Memory>();
    for (float ratio : {.5f, 2.f}) {
        Octave shift;
        shift.Prepare(48000, ram->pitch[0], ram->pitch[1], ratio);
        double real = 0, imag = 0, wrongReal = 0, wrongImag = 0, energy = 0;
        for (int n = 0; n < 48000 * 3; ++n) {
            float x = .1f * std::sin(2 * kPi * 375.f * float(n) / 48000);
            auto y = shift.Process({x, x});
            Require(y.left == y.right, "pitch stereo phase mismatch");
            if (n >= 48000) {
                double phase = 2 * double(kPi) * 375 * n / 48000;
                real += y.left * std::cos(phase * ratio);
                imag += y.left * std::sin(phase * ratio);
                wrongReal += y.left * std::cos(phase);
                wrongImag += y.left * std::sin(phase);
                energy += y.left * y.left;
            }
        }
        const double target = std::hypot(real, imag), wrong = std::hypot(wrongReal, wrongImag);
        Require(target > 1000 && wrong < target * .02, "octave target/original spectral ratio");
        // An upward shift of 15kHz would fold without input filtering. Compare total energy.
        if (ratio == 2) {
            shift.Prepare(48000, ram->pitch[0], ram->pitch[1], ratio);
            double high = 0;
            for (int n = 0; n < 48000 * 3; ++n) {
                auto y = shift.Process({.1f * std::sin(2 * kPi * 15000.f * float(n) / 48000), 0});
                if (n >= 48000)
                    high += y.left * y.left;
            }
            Require(high < energy * .0025, "up-octave high-frequency rejection inadequate");
            std::cout << "Up-octave 15kHz/375Hz energy dB: " << 10 * std::log10(high / energy)
                      << "\n";
        }
    }
}
void Interfaces() {
    Fixture a, b;
    Parameters p;
    p.mix = .6f;
    a.Prepare(48000, p);
    b.Prepare(48000, p);
    Require(!a.fx.Prepare(std::numeric_limits<float>::quiet_NaN(), *a.memory),
            "NaN sample rate accepted");
    Require(!a.fx.Prepare(192000, *a.memory), "unsupported rate accepted");
    std::array<float, 29003> l{}, r{}, ol{}, or_{};
    std::mt19937 g(817);
    for (unsigned i = 0; i < l.size(); ++i) {
        l[i] = float(int(g() % 2001) - 1000) * .00005f;
        r[i] = float(int(g() % 2001) - 1000) * .00005f;
        auto y = a.fx.Process({l[i], r[i]});
        ol[i] = y.left;
        or_[i] = y.right;
    }
    for (unsigned i = 0; i < l.size(); i += 37)
        b.fx.ProcessBlock(l.data() + i, r.data() + i, l.data() + i, r.data() + i,
                          std::min(std::size_t(37), l.size() - i));
    Require(l == ol && r == or_, "block/in-place changes state");
    p.mix = 0;
    p.outputGain = 1;
    a.Prepare(48000, p);
    for (int n = 0; n < 10000; ++n) {
        Stereo x{.2f * std::sin(.01f * n), -.13f};
        auto y = a.fx.Process(x);
        Require(y.left == x.left && y.right == x.right, "dry endpoint changed");
    }
    p.mix = 1;
    p.width = 0;
    a.Prepare(48000, p);
    double wet = 0;
    for (int n = 0; n < 48000; ++n) {
        auto y = a.fx.Process({n == 0 ? .1f : 0.f, 0});
        Require(y.left == y.right, "width zero isn't mono");
        wet += Energy(y);
    }
    Require(wet > 1e-5, "wet tank silent");
    p.mix = 1;
    p.width = 1;
    p.preDelayMs = 70;
    a.Prepare(48000, p);
    for (int n = 0; n < 3360; ++n) {
        auto y = a.fx.Process({n == 0 ? .1f : 0.f, 0});
        Require(Energy(y) == 0, "predelay has early leakage");
    }
    p.decaySeconds = std::numeric_limits<float>::infinity();
    p.mix = std::numeric_limits<float>::quiet_NaN();
    p.mode = Mode(99);
    a.fx.SetParameters(p);
    auto safe = a.fx.GetParameters();
    Require(std::isfinite(safe.decaySeconds) && safe.mode == Mode::Cloud, "parameter sanitization");
    auto y = a.fx.Process(
        {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()});
    Require(std::isfinite(y.left) && std::isfinite(y.right), "nonfinite audio propagated");
}
void Hold() {
    Fixture a, b;
    Parameters p;
    p.mix = 1;
    p.modDepthMs = 1.2f;
    p.decaySeconds = 8;
    a.Prepare(48000, p);
    b.Prepare(48000, p);
    std::mt19937 rng(70);
    for (int n = 0; n < 48000; ++n) {
        float x = .1f * float(std::sin(.025 * n));
        Stereo in{x, x * .7f};
        a.fx.Process(in);
        b.fx.Process(in);
    }
    p.hold = true;
    a.fx.SetParameters(p);
    b.fx.SetParameters(p);
    for (int n = 0; n < 24000; ++n) {
        a.fx.Process({});
        b.fx.Process({});
    }
    double first = 0, last = 0;
    for (int n = 0; n < 48000 * 12; ++n) {
        auto y = a.fx.Process({}), z = b.fx.Process({.2f * float(std::sin(.1 * n)), -.1f});
        Require(y.left == z.left && y.right == z.right, "hold admits new input");
        if (n < 48000 * 2)
            first += Energy(y);
        if (n >= 48000 * 10)
            last += Energy(y);
    }
    double change = 10 * std::log10(last / first);
    Require(std::abs(change) < 2, "hold energy drifts");
    std::cout << "Hold late/early energy dB: " << change << "\n";
    p.hold = false;
    a.fx.SetParameters(p);
    double release = 0;
    for (int n = 0; n < 48000 * 20; ++n) {
        auto y = a.fx.Process({});
        if (n >= 48000 * 18)
            release += Energy(y);
    }
    Require(release < last * .03, "hold release does not decay");
}
void CharactersAndBypass() {
    Fixture a, b;
    Parameters p;
    p.mix = 1;
    p.outputGain = 1;
    p.preDelayMs = 0;
    a.Prepare(48000, p);
    p.mode = Mode::Bloom;
    p.riseSeconds = 1.4f;
    b.Prepare(48000, p);
    double cloud = 0, bloom = 0;
    for (int n = 0; n < 48000 * 3; ++n) {
        Stereo in{n == 0 ? .1f : 0.f, 0};
        auto y = a.fx.Process(in), z = b.fx.Process(in);
        if (n < 24000) {
            cloud += Energy(y);
            bloom += Energy(z);
        }
        if (n > 96000)
            Require(y.left == z.left && y.right == z.right, "Bloom alters tank after rise");
    }
    Require(bloom < cloud * .08, "Bloom doesn't suppress the onset");
    p.mode = Mode::Cloud;
    a.Prepare(48000, p);
    b.Prepare(48000, p);
    for (int n = 0; n < 24000; ++n) {
        Stereo x{.05f * float(std::sin(.02 * n)), 0};
        a.fx.Process(x);
        b.fx.Process(x);
    }
    p.bypass = true;
    p.trails = true;
    a.fx.SetParameters(p);
    b.fx.SetParameters(p);
    for (int n = 0; n < 12000; ++n) {
        a.fx.Process({});
        b.fx.Process({});
    }
    double tail = 0;
    for (int n = 0; n < 24000; ++n) {
        float x = .1f * float(std::sin(.03 * n));
        auto y = a.fx.Process({x, x}), z = b.fx.Process({});
        Require(std::abs(y.left - x - z.left) < 1e-7f && std::abs(y.right - x - z.right) < 1e-7f,
                "bypass admits input to tail");
        tail += Energy(z);
    }
    Require(tail > 1e-6, "trails bypass drops tail");
    p.trails = false;
    a.fx.SetParameters(p);
    for (int n = 0; n < 2400; ++n)
        a.fx.Process({});
    auto dry = a.fx.Process({.123f, -.07f});
    Require(dry.left == .123f && dry.right == -.07f, "silent bypass dry endpoint");
}
void LongDecay() {
    Fixture f;
    for (Mode mode : {Mode::Abyss, Mode::Halo}) {
        Parameters p;
        p.mode = mode;
        p.mix = 1;
        p.decaySeconds = 45;
        p.modDepthMs = 3;
        p.modRateHz = 1.5f;
        p.texture = 1;
        f.Prepare(48000, p);
        double early = 0, late = 0;
        for (int n = 0; n < 48000 * 90; ++n) {
            float x = n < 48000 ? .1f * float(std::sin(.057 * n)) : 0;
            auto y = f.fx.Process({x, x});
            Require(std::isfinite(y.left) && std::isfinite(y.right), "long-decay nonfinite");
            if (n >= 48000 * 5 && n < 48000 * 10)
                early += Energy(y);
            if (n >= 48000 * 85)
                late += Energy(y);
        }
        Require(f.fx.GetDiagnostics().guardEvents == 0 && late < early * .001,
                "long-decay tail grows or hits guard");
    }
}
void Stability() {
    Fixture f;
    std::mt19937 rng(901);
    float peak = 0;
    for (float fs : {32768.f, 44100.f, 48000.f, 96000.f}) {
        Parameters p;
        p.mix = 1;
        p.decaySeconds = 45;
        p.modDepthMs = 3;
        p.modRateHz = 1.5f;
        p.texture = 1;
        f.Prepare(fs, p);
        audio = true;
        for (unsigned n = 0; n < unsigned(fs * 8); ++n) {
            if (n % unsigned(fs) == 0) {
                p.mode = Mode((n / unsigned(fs)) % 5);
                p.shape = Shape((n / unsigned(fs)) % 5);
                f.fx.SetParameters(p);
            }
            float x = n < unsigned(fs * 3) ? float(int(rng() % 2001) - 1000) * .0002f : 0;
            auto y = f.fx.Process({x, -x * .6f});
            Require(std::isfinite(y.left) && std::isfinite(y.right), "unstable output");
            peak = std::max({peak, std::abs(y.left), std::abs(y.right)});
        }
        audio = false;
        Require(f.fx.GetDiagnostics().guardEvents == 0,
                "guard needed in moderate-level stability test");
    }
    Require(allocations == 0, "allocation in audio/parameter path");
    Require(peak < 4, "excessive normal peak");
    std::cout << "4 rates / 5 modes / 5 shapes: peak=" << peak
              << "; audio allocations=" << allocations << "\n";
    // Worst valid controls with loud bursts: output finite even under a deliberately abusive input.
    Parameters p;
    p.mix = 1;
    p.decaySeconds = 45;
    p.modDepthMs = 3;
    p.texture = 1;
    p.mode = Mode::Halo;
    f.Prepare(48000, p);
    for (int n = 0; n < 48000 * 5; ++n) {
        auto y = f.fx.Process({n < 48000 ? 4.f : 0.f, n < 48000 ? -4.f : 0.f});
        Require(std::isfinite(y.left) && std::isfinite(y.right), "overload becomes nonfinite");
    }
}
void Benchmark() {
    Fixture f;
    f.Prepare(48000, AmbientReverb::Preset(Mode::Halo));
    auto start = std::chrono::steady_clock::now();
    float sum = 0;
    for (int n = 0; n < 480000; ++n) {
        auto y = f.fx.Process({.03f * float(std::sin(.017 * n)), 0});
        sum += y.left;
    }
    double ns =
        std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() /
        480000;
    std::cout << "Memory=" << sizeof(Memory) << " bytes, engine=" << sizeof(AmbientReverb)
              << " bytes; host ns/frame=" << ns << "; checksum=" << sum << "\n";
}
int main() {
    try {
        Algebra();
        Pitch();
        Interfaces();
        Hold();
        CharactersAndBypass();
        Stability();
        LongDecay();
        Benchmark();
        std::cout << "PASS ambient\n";
    } catch (const std::exception &e) {
        audio = false;
        std::cerr << e.what() << "\n";
        return 1;
    }
}
