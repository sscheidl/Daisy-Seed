#pragma once
#include "pcm.h"
#include <atomic>

namespace neb {
enum class FileError : uint8_t { None, Missing, Format, TooLarge, Io, NoCard };
struct File {
    virtual ~File() = default;
    virtual FileError Open(unsigned slot) = 0;
    virtual uint32_t Size() const = 0;
    virtual bool Read(uint32_t offset, void* dst, uint32_t count) = 0;
    virtual void Close() = 0;
    virtual unsigned Bank() const { return 0; }  // bank of the open file (0 = none)
};
struct WavInfo { uint32_t offset=0, bytes=0, frames=0, rate=0; uint16_t channels=0; };
// One RIFF chunk per Step, metadata reads <=24 bytes, no payload scans.
class WavParser {
  public:
    void Begin(uint32_t bytes, size_t capacity);
    bool Step(File& file); // true when terminal
    FileError error = FileError::None;
    WavInfo info{};
  private:
    uint32_t size_=0, end_=0, pos_=0, chunks_=0;
    size_t capacity_=0;
    bool fmt_=false, data_=false, done_=false;
};
// Main owns this object and PCM storage, except while state is Published/Idle.
// Audio owns its view. Gate/Ack fence all destructive writes to the ONE file buffer.
struct FileBridge {
    std::atomic<bool> gate{false}, ack{false};
    std::atomic<uint32_t> generation{0};
    WavInfo published{}; // protected by generation release/acquire + gate/ack
    unsigned slot=0, bank=0;
};
class Loader {
  public:
    enum class State { Idle, Validate, WaitMute, Read, Publish };
    void Init(File& file, int16_t* pcm, size_t capacity, FileBridge& bridge);
    bool Request(unsigned slot);
    void Service(); // at most one parser chunk or 4096 payload bytes
    State state = State::Idle;
    FileError error = FileError::None;
    unsigned target=0, active=0;
  private:
    void Fail(FileError e, bool destructive);
    File* file_=nullptr; int16_t* pcm_=nullptr; FileBridge* bridge_=nullptr;
    size_t capacity_=0; WavParser parser_;
    uint32_t read_=0;
    alignas(32) uint8_t bounce_[4096]{};
};
uint16_t SlotLeds(unsigned mode, uint16_t keys, unsigned active, unsigned target,
                  bool loading, FileError error, uint32_t ms, uint16_t status);
// Complete panel: full-brightness and dimmed LEDs (bit 0 = D1). Main loop only.
struct LedFrame { uint16_t on=0, dim=0; };
struct LedState {
    unsigned mode=1;            // 0 NEBULAE, 1 SAMPLE, 2 KEYBOARD
    bool bank_view=false;       // SAMPLE with SW17 in the bank position
    uint16_t keys=0;            // KEYBOARD: debounced presses
    unsigned active=0;          // playing slot, 0 unless it belongs to the shown bank
    unsigned target=0;          // slot being loaded / last failed
    bool loading=false;
    FileError error=FileError::None;
    uint32_t ms=0;
    uint16_t status=0;          // Engine::Status()
    unsigned bank=0;            // selected bank, 0 = none
    uint16_t bank_mask=0;       // existing banks
    unsigned scanning_bank=0;   // file list being read
    bool bank_failed=false;     // selected bank's folder could not be read
    bool overrun=false;         // latched audio deadline overrun
};
constexpr uint16_t kOverrunLed = 1u << 15;  // D16 in NEBULAE mode (S16 is reserved there)
constexpr float kDimLevel = 0.15f;          // PCA9685 brightness of dimmed bank LEDs
LedFrame PanelLeds(const LedState& s);
}
