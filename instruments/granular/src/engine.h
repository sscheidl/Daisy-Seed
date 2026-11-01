#pragma once
#include "reference_dsp.h"
#include "control.h"
#include "wav.h"

namespace neb {
struct Grain {
    double position=0;
    float step=1, envelope=1, increment=1, pan=0;
    uint32_t delay=0;
    bool active=false;
};
class Engine : public Sink {
  public:
    void Init(int16_t* live_a,int16_t* live_b,uint32_t capacity,int16_t* file,FileBridge& bridge);
    void Process(const float* left,const float* right,float* out_l,float* out_r,unsigned n);
    void Act(Action action) override;
    void Select(unsigned slot) override {requested_slot=slot;}
    void SelectBank(unsigned bank) override {requested_bank=bank;}
    void Key(unsigned id,unsigned note,bool on) override;
    void MidiEvent(const Midi& event);
    void Disconnect();
    Ui ui;
    Notes notes;
    unsigned requested_slot=0, requested_bank=0, file_order=0;
    uint16_t occupied=0;      // occupied slots of the selected bank (main -> audio)
    unsigned selected_bank=0; // bank shown in SAMPLE mode and stepped by FILE
    bool live=false, frozen=false, recording=false, circular=false, trigger_mode=false;
    bool finalizing=false;
    uint32_t LiveLength() const {return live_length_;}
    double Position() const {return position_;}
    float EffectivePitch() const {return pitch_;}
    float EffectiveSpeed() const {return speed_;}
    uint32_t resets=0;
    uint16_t Status() const;
    void TriggerGrain() {pending_trigger_=true;clock_edge_=true;}
    // Trigger mode at minimum DENSITY: grain size is capped at 6x the smoothed external
    // clock period (original instr lines 446-470: portk half-time 0.5 s, 0.5 ms floor).
    float SmoothedClockSeconds() const {return smooth_clock_;}
    float LastGrainMs() const {return last_grain_ms_;}
    unsigned PlayingBank() const {return playing_bank_;}
  private:
    void ResetDsp();
    void Transition();
    void PublishLive(bool resume=false);
    void BeginRecord();
    void EndRecord();
    void RenderGrains(const PcmView& source,const float* left,const float* right,
                      unsigned n,float density,float pitch,float input_gain);
    float Random();
    int16_t* bank_[2]{}; int16_t* file_=nullptr; FileBridge* bridge_=nullptr;
    PcmView file_view_{};
    uint32_t generation_=0,cap_=0,live_length_=0,write_=0,copy_=0;
    uint32_t write_start_=0,last_read_frame_=0;
    unsigned active_bank_=0,active_slot_=0,playing_bank_=0;
    uint32_t clock_samples_=0;
    float clock_period_=0,smooth_clock_=0,last_grain_ms_=0;
    bool clock_edge_=false;
    bool overdub_=false,pending_trigger_=false,file_detached_=false;
    uint16_t key_mask_=0;
    uint32_t random_=0x49302487u;
    double position_=0,grain_clock_[2]{};
    float file_gain_=1, musical_gain_=1, transition_=0;
    float tail_[2]{},fade_from_[2]{};
    uint32_t reset_light_=0;
    float smoothed_[8]{},alt_[8]{},bend_[16]{},pitch_=1,speed_=1;
    Mincer vocoder_[2]; RmsLimiter limiter_[2];
    Grain grains_[2][MAX_GRAINS]{};
    float bank_windows_[W_BANK][WIN_LEN+1]{},window_[WIN_LEN+1]{};
    double positions_[kBlock]{};
    float voc_[2][kBlock]{},gran_[2][kBlock]{};
};
}
