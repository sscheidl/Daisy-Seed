#pragma once
#include "pcm.h"
#include <atomic>

namespace neb {
constexpr uint8_t kNotes[16]={60,62,64,65,67,69,71,72,72,74,76,77,79,81,83,84};
enum Param { Start, Speed, Size, Density, Pitch, Overlap, Blend, Window };
enum class Action { Record, FileNext, Source, Reset, Freeze, Clear, Circular, Order, GrainTrigger, ResetAlt };
struct ControlFrame { float pot[8]{}; uint32_t ms=0; uint16_t keys=0; uint8_t toggle=0; };
struct Midi { uint8_t type=0, channel=0, a=0, b=0; }; // status upper nibble
class MidiParser {
  public:
    bool Byte(uint8_t byte,Midi& result);
  private:
    uint8_t status_=0,first_=0,used_=0;
    bool sysex_=false;
};
template<class T,unsigned N> class Queue {
    static_assert((N&(N-1))==0);
  public:
    bool Push(const T& v) {
        const unsigned w=write_.load(std::memory_order_relaxed);
        if(w-read_.load(std::memory_order_acquire)==N) return false;
        values_[w&(N-1)]=v; write_.store(w+1,std::memory_order_release); return true;
    }
    bool Pop(T& v) {
        const unsigned r=read_.load(std::memory_order_relaxed);
        if(r==write_.load(std::memory_order_acquire)) return false;
        v=values_[r&(N-1)]; read_.store(r+1,std::memory_order_release); return true;
    }
  private:
    T values_[N]{}; std::atomic<unsigned> write_{0},read_{0};
};
static_assert(std::atomic<unsigned>::is_always_lock_free);
struct Sink {
    virtual ~Sink()=default;
    virtual void Act(Action action)=0;
    virtual void Select(unsigned slot)=0;
    virtual void Key(unsigned id,unsigned note,bool on)=0;
};
class Parameters {
  public:
    float primary[8]={0,.5f,1,.4f,.5f,.5f,0,0};
    float secondary[8]={0,0,0,0,0,0,0,0};
    void Update(const float* raw,bool alt);
    void CC(unsigned index,float value);
    void ResetAlt();
  private:
    float last_[8]{}; bool caught_[2][8]{}; bool ready_=false, page_=false;
};
class Ui {
  public:
    Parameters params;
    unsigned mode=1;
    uint16_t keyboard=0;
    bool secondary=false;
    void Process(const ControlFrame& f,Sink& sink);
  private:
    uint16_t held_=0, suppressed_=0;
    uint8_t stable_=0,candidate_=0;
    uint32_t switch_at_=0,source_at_=0,file_at_=0;
    bool initialized_=false, source_used_=false,file_used_=false;
};
// Distinct source IDs prevent keyboard/MIDI and per-channel note-off collisions.
class Notes {
  public:
    void Set(unsigned source,unsigned note,unsigned velocity);
    void Off(unsigned source,unsigned note);
    void Clear(unsigned source); // 0..15 MIDI channels; 16 keyboard
    void AllOff();
    int Current() const;
    float Velocity() const;
    float Bend(const float* bends) const;
    bool engaged=false; // Historical note activity, never an audio gate.
  private:
    uint32_t order_[17][128]{}; uint8_t velocity_[17][128]{}; uint32_t serial_=0;
};
}
