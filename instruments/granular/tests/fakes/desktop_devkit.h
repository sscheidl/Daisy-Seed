#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>
#define DSY_SDRAM_BSS
#define DSY_SYSTEM_H
namespace daisy {
struct System {
    inline static uint32_t now=0;
    static uint32_t GetNow() {return now;}
    static void Delay(uint32_t n) {now+=n;}
};
}
#include "hid/digital_control.h"
struct FakeDwt {uint32_t CYCCNT=0,CTRL=0;};
struct FakeCore {uint32_t DEMCR=0;};
inline FakeDwt fake_dwt;inline FakeCore fake_core;
#define DWT (&fake_dwt)
#define CoreDebug (&fake_core)
constexpr unsigned CoreDebug_DEMCR_TRCENA_Msk=1,DWT_CTRL_CYCCNTENA_Msk=1,SystemCoreClock=480000000;
namespace daisy {
struct AudioHandle {using InputBuffer=const float* const*;using OutputBuffer=float**;};
struct SaiHandle {struct Config {enum class SampleRate {SAI_48KHZ};};};
struct MidiUsbTransport {
    struct Config {enum Periph {EXTERNAL};Periph periph=EXTERNAL;};
    using Callback=void(*)(uint8_t*,size_t,void*);
    Callback callback=nullptr;void* context=nullptr;bool active=true;
    std::vector<std::vector<uint8_t>> sent;
    void Init(Config) {}
    void StartRx(Callback cb,void* ctx) {callback=cb;context=ctx;active=true;}
    bool RxActive() const {return active;}
    void FlushRx() {}
    void Tx(uint8_t* bytes,size_t n) {sent.emplace_back(bytes,bytes+n);}
};
struct SdmmcHandler {enum class Speed {STANDARD};struct Config {Speed speed;void Defaults(){speed=Speed::STANDARD;}};void Init(Config) {}};
namespace desktop_devkit {
struct Hardware {
    struct Seed {
        void SetAudioSampleRate(SaiHandle::Config::SampleRate) {}
        void SetAudioBlockSize(unsigned) {}
        unsigned AudioBlockSize() const {return 256;}
        template<class T> void StartAudio(T) {}
    } seed;
    struct Pot {float value=0;float Value() const {return value;}};
    struct Switch {bool state=true;bool Pressed() const {return state;}} sd_detect;
    struct Switch3 {unsigned value=0;unsigned Read() const {return value;}} tog_sw18;
    struct Gate {bool pulse=false;bool Trig() {bool p=pulse;pulse=false;return p;}} gate_in[2];
    std::array<Pot,8> pot;
    std::array<DigitalControl,16> button;
    std::array<bool,16> pressed{};
    void Init() {for(auto& b:button) b.Init(true);}
    void UpdateAllControls() {for(unsigned i=0;i<16;++i) button[i].Debounce(!pressed[i]);}
    void SetLed(unsigned,float) {}
    void UpdateLeds() {}
    template<class T> void StartLowPriorityCallback(T,unsigned) {}
};
}
}
