// MIT. Exercise both typed adapters through the same audio-block contract.
#include "../../effects/torus-reverb/src/torus_reverb.h"
#include "../../effects/reverb/src/fx_adapter.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>

static bool audio = false;
static unsigned allocations = 0;
[[gnu::noinline]] void* operator new(std::size_t size) {
    if(audio) ++allocations;
    if(void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }

template<class Effect> void Contract() {
    auto fx = std::make_unique<Effect>();
    auto storage = std::make_unique<typename Effect::Storage>();
    float left[48]{}, right[48]{}, out_left[48]{}, out_right[48]{};
    seed::audio::StereoBlock block{left, right, out_left, out_right, 48};
    out_left[0] = .7f;
    assert(!fx->Process(block) && out_left[0] == .7f);
    assert(!fx->Prepare({44100, 48}, *storage));
    assert(fx->Prepare({48000, 48}, *storage));
    auto bad = block;
    bad.frames = 49;
    assert(!fx->Process(bad) && out_left[0] == .7f);
    bad = block;
    bad.left = nullptr;
    assert(!fx->Process(bad));
    bad = block;
    bad.out_right = bad.out_left;
    assert(!fx->Process(bad));
    assert(fx->Process({nullptr, nullptr, nullptr, nullptr, 0}));
    double energy = 0, tail = 0;
    audio = true;
    for(unsigned n = 0; n < 3000; ++n) {
        if(n % 100 == 0) fx->SetParameters(typename Effect::Parameters{});
        left[0] = n == 0 ? .2f : 0.f;
        assert(fx->Process(block));
        for(unsigned i = 0; i < 48; ++i) {
            assert(std::isfinite(out_left[i]) && std::isfinite(out_right[i]));
            energy += double(out_left[i]) * out_left[i] + double(out_right[i]) * out_right[i];
            if(n > 100) tail += double(out_left[i]) * out_left[i];
        }
        // A wet allpass network may have immediate feedthrough. Wet-only routing
        // is checked against the upstream amount=1 reference below, not zero latency.
    }
    audio = false;
    assert(energy > 1e-10 && tail > 1e-12 && allocations == 0);
}

static void TorusReference() {
    seed::fx::TorusReverb adapter;
    auto memory = std::make_unique<seed::fx::TorusReverbStorage>();
    auto reference_memory = std::make_unique<seed::fx::TorusReverbStorage>();
    torus::Reverb reference;
    assert(adapter.Prepare({48000, 48}, *memory));
    reference.Init(reference_memory->words.data());
    seed::fx::TorusReverbParameters p{.7f, .3f, .6f, .5f};
    adapter.SetParameters(p);
    reference.set_time(.35f + .63f * p.decay);
    reference.set_lp(.05f + .9f * (1.f - p.damping));
    reference.set_diffusion(.9f * p.diffusion);
    reference.set_input_gain(.5f * p.input_gain);
    reference.set_amount(1.f);
    for(unsigned block = 0; block < 2000; ++block) {
        float a[48], b[48], c[48], d[48];
        for(unsigned i = 0; i < 48; ++i) {
            a[i] = c[i] = block < 100 ? .05f * std::sin(float(block * 48 + i) * .02f) : 0.f;
            b[i] = d[i] = a[i] * .6f;
        }
        assert(adapter.Process({a, b, a, b, 48}));
        reference.Process(c, d, 48);
        for(unsigned i = 0; i < 48; ++i) assert(a[i] == c[i] && b[i] == d[i]);
    }
}

int main() {
    Contract<seed::fx::TorusReverb>();
    Contract<seed::fx::AmbientReverb>();
    TorusReference();
    {
        seed::fx::AmbientReverb fx;
        auto memory = std::make_unique<seed::fx::AmbientReverb::Storage>();
        ambient::Parameters p;
        p.mix = 0; p.bypass = true; p.outputGain = 0;
        fx.SetParameters(p); // Host routing must override standalone mix controls.
        assert(fx.Prepare({48000, 1}, *memory));
        float left = .5f, right = .2f, out_left = 1, out_right = 1;
        assert(fx.Process({&left, &right, &out_left, &out_right, 1}));
        assert(out_left == 0 && out_right == 0);
    }
    std::puts("PASS: both FX adapters, wet-only routing, bounds, tails, zero audio allocations and exact Torus reference");
}
