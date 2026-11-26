// New product variants: same keyboard/MIDI host, selectable downstream effect.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
int main() {
    hw.Init();
    cpu_meter.Init(48000, kBlockSize);
    hw.tog_sw18.value = 1;
    InitializeSynth();
    InitModulation(48000);
    for(auto& pot : hw.pot) pot.value = .5f;
    hw.pot[5].value = .8f;
    hw.pot[7].value = 1.f;
    assert(std::strcmp(kModInfo.version, PHYSICAL_MODELING_COMPOSED_VERSION) == 0);
    assert(synth->NoteOn(60, 100));
    float left[kBlockSize]{}, right[kBlockSize]{};
    float* out[]{left, right};
    synth->parameters.Set(Wet, 1.f);
    double energy = 0;
    for(unsigned block = 0; block < 3000; ++block) {
        AudioCallback(nullptr, out, kBlockSize);
        ++daisy::System::now;
        for(unsigned i = 0; i < kBlockSize; ++i) {
            assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            assert(std::fabs(left[i]) < .8f && std::fabs(right[i]) < .8f);
            energy += double(left[i]) * left[i] + double(right[i]) * right[i];
        }
    }
    assert(energy > 1e-8);
    std::printf("PASS composed product %s, protected wet output, energy %.8f\n", kFirmwareId, energy);
}
