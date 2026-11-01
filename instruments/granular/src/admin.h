// SD management personality: DDP v1 over USB CDC for the Windows control panel,
// next to the USB mass-storage interface of the shared card reader
// (common/sd/reader). libDaisy-free; main.cpp supplies the hardware through
// AdminPort. Protocol: diagnostics/protocol/PROTOCOL.md plus the NEB message in
// docs/CONTROL_PROTOCOL.md.
//
// Ownership rules (V0_2_ARCHITECTURE.md):
//  * the card is shared only by an explicit READER a=SHARE;
//  * BYE, a host timeout or a closed port never return the card;
//  * back to performance (NEB a=PERFORM, BOOT, or the S4 key) only from LOCAL,
//    i.e. after Windows ejected and READER a=RETURN, or after an interruption was
//    explicitly acknowledged with READER a=RETURN.
#pragma once
#include "card_reader.h"
#include "ddp.h"
#include "ring_buffer.h"
#include <cstddef>
#include <cstdint>

namespace neb {

struct AdminInputs {
    uint16_t buttons = 0;     // bit 0 = S1
    uint16_t pots[8] = {};    // 0..10000
    uint16_t pots_raw[8] = {};
    bool sw17_up = false;
    uint8_t sw18 = 1;         // 0 = up, 1 = center, 2 = down
    bool card = false;
};

struct ReaderIo {
    const char* operation = "NONE";
    uint8_t fault = 0;
    uint32_t hal_error = 0, lba = 0, elapsed_ms = 0;
};

enum class AdminSend : uint8_t { Ok, Busy, NotReady };

class AdminPort {
  public:
    virtual AdminSend Send(const uint8_t* data, size_t size) = 0;  // CDC IN, nonblocking
    virtual AdminInputs Inputs() = 0;
    virtual bool ReaderIdle() = 0;        // MSC BOT waits for a new command
    virtual ReaderIo LastIo() = 0;
    virtual void ApplyLeds(uint16_t mask) = 0;
    // Restart into the performance personality (writes the boot intent). Does not
    // return on hardware; host fakes record the call.
    virtual void RestartPerformance() = 0;
    virtual void EnterBootloader() = 0;

  protected:
    ~AdminPort() = default;
};

class Admin {
  public:
    static constexpr uint32_t kHostTimeoutMs = 3000;
    static constexpr uint32_t kFlushMs = 50;          // wait for a queued ACK before a reset
    static constexpr uint32_t kSettleMs = 20;         // minimum time for a sent ACK to leave
    static constexpr uint32_t kReturnHoldMs = 2000;   // S4 held: return to performance
    static constexpr size_t kTxSize = 1536;

    Admin(diag::CardReader& reader, AdminPort& port, const char* firmware,
          const char* bootloader, uint8_t restore_bank, uint8_t restore_slot);
    void OnUsbReceive(const uint8_t* data, size_t size) { rx_.Push(data, size); }  // USB IRQ
    void Step(uint32_t now_ms);

    bool SessionActive() const { return session_; }
    uint16_t Leds(uint32_t now_ms) const;
    uint32_t TxDropped() const { return tx_drop_; }

  private:
    enum class Pending : uint8_t { None, Perform, Boot };
    void Handle(const char* body, uint32_t now);
    void Queue(const ddp::Writer& body);
    void Pump();
    void SendState(uint32_t now);
    void SendSys(uint32_t now);
    void SendReader(const char* action);
    void SendNeb(const char* action);
    void Error(const char* command, const char* code);
    bool BackToPerformanceAllowed() const;

    diag::CardReader& reader_;
    AdminPort& port_;
    const char* firmware_;
    const char* bootloader_;
    uint8_t restore_bank_, restore_slot_;
    diag::SpscRing<1024> rx_;
    ddp::FrameParser parser_;
    uint8_t tx_[2][kTxSize] = {};
    size_t tx_fill_ = 0, tx_active_ = 0;
    uint32_t tx_ok_ = 0, tx_busy_ = 0, tx_drop_ = 0;
    bool session_ = false;
    uint32_t last_rx_ = 0, last_state_ = 0, last_sys_ = 0, start_ = 0, seq_ = 0, rate_hz_ = 10;
    bool started_ = false;
    uint16_t last_buttons_ = 0;
    uint8_t press_counts_[16] = {};
    uint32_t s4_since_ = 0;
    bool s4_held_ = false;
    Pending pending_ = Pending::None;
    uint32_t pending_since_ = 0;
    uint16_t last_leds_ = 0xFFFF;
};

} // namespace neb
