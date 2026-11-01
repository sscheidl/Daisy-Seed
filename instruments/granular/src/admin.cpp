#include "admin.h"
#include <cstring>

namespace neb {
namespace {
constexpr uint16_t kS4 = 1u << 3;
const char* Sw18Name(uint8_t p) { return p == 0 ? "U" : p == 2 ? "D" : "C"; }
} // namespace

Admin::Admin(diag::CardReader& reader, AdminPort& port, const char* firmware,
             const char* bootloader, uint8_t restore_bank, uint8_t restore_slot)
    : reader_(reader), port_(port), firmware_(firmware), bootloader_(bootloader),
      restore_bank_(restore_bank), restore_slot_(restore_slot) {}

void Admin::Queue(const ddp::Writer& body) {
    char frame[ddp::kMaxFrame];
    const size_t n = body.Ok() ? ddp::BuildFrame(body.CStr(), body.Length(), frame, sizeof frame) : 0;
    if(!n || tx_fill_ + n > kTxSize) { ++tx_drop_; return; }
    std::memcpy(tx_[tx_active_] + tx_fill_, frame, n);
    tx_fill_ += n;
}

void Admin::Pump() {
    if(!tx_fill_) return;
    switch(port_.Send(tx_[tx_active_], uint16_t(tx_fill_))) {
    case AdminSend::Ok: ++tx_ok_; tx_active_ ^= 1; tx_fill_ = 0; break;
    case AdminSend::Busy: ++tx_busy_; break;
    case AdminSend::NotReady: ++tx_drop_; tx_fill_ = 0; break;  // nobody listens
    }
}

void Admin::Error(const char* command, const char* code) {
    char b[64];
    ddp::Writer w(b, sizeof b);
    w.Str("ERR c=").Str(command).Str(" e=").Str(code);
    Queue(w);
}

bool Admin::BackToPerformanceAllowed() const {
    return reader_.State() == diag::ReaderState::Local;
}

void Admin::SendReader(const char* action) {
    char b[200];
    ddp::Writer w(b, sizeof b);
    w.Str("READER state=").Str(diag::ReaderStateName(reader_.State()))
        .Str(" sectors=").U32(reader_.Sectors()).Str(" busy=").U32(!port_.ReaderIdle())
        .Str(" a=").Str(action);
    const ReaderIo io = port_.LastIo();
    w.Str(" io=").Str(io.operation).Str(" fault=").U32(io.fault).Str(" he=").U32(io.hal_error)
        .Str(" lba=").U32(io.lba).Str(" ms=").U32(io.elapsed_ms);
    Queue(w);
}

void Admin::SendNeb(const char* action) {
    char b[160];
    ddp::Writer w(b, sizeof b);
    w.Str("NEB mode=SDMGMT fw=").Str(firmware_).Str(" card=").U32(port_.Inputs().card)
        .Str(" ret=").Str(BackToPerformanceAllowed() ? "1" : "0")
        .Str(" bank=").U32(restore_bank_).Str(" slot=").U32(restore_slot_).Str(" a=").Str(action);
    Queue(w);
}

void Admin::SendState(uint32_t now) {
    const AdminInputs in = port_.Inputs();
    char b[400];
    ddp::Writer w(b, sizeof b);
    w.Str("ST n=").U32(seq_++).Str(" t=").U32(now - start_).Str(" b=").Hex(in.buttons, 4).Str(" pc=");
    for(uint8_t c : press_counts_) w.Hex(c, 2);
    w.Str(" p=");
    for(int i = 0; i < 8; ++i) { if(i) w.Char(','); w.U32(in.pots[i]); }
    w.Str(" r=");
    for(int i = 0; i < 8; ++i) { if(i) w.Char(','); w.U32(in.pots_raw[i]); }
    w.Str(" s17=").Str(in.sw17_up ? "U" : "D").Str(" s18=").Str(Sw18Name(in.sw18))
        .Str(" sd=").U32(in.card).Str(" jl=0 jr=0 lm=MASK l=").Hex(Leds(now), 4).Str(" br=100");
    Queue(w);
}

void Admin::SendSys(uint32_t now) {
    char b[160];
    ddp::Writer w(b, sizeof b);
    w.Str("SYS up=").U32((now - start_) / 1000).Str(" cbhz=1000 txok=").U32(tx_ok_)
        .Str(" txbusy=").U32(tx_busy_).Str(" txdrop=").U32(tx_drop_).Str(" rxok=").U32(parser_.ok)
        .Str(" rxbad=").U32(parser_.bad_crc + parser_.bad_format + parser_.overflow + rx_.Overflow())
        .Str(" host=").U32(session_).Str(" rate=").U32(rate_hz_);
    Queue(w);
}

void Admin::Handle(const char* body, uint32_t now) {
    char type[12];
    ddp::MessageType(body, type, sizeof type);
    char value[16];
    if(!std::strcmp(type, "HELLO")) {
        uint32_t proto = 0;
        if(!ddp::FindField(body, "proto", value, sizeof value) || !ddp::ParseU32(value, proto)) {
            Error("HELLO", "ARG");
            return;
        }
        if(proto != uint32_t(ddp::kProtocolVersion)) { Error("HELLO", "PROTO"); return; }
        tx_fill_ = 0;  // a new session discards the previous one's pending output
        session_ = true;
        char b[300];
        ddp::Writer w(b, sizeof b);
        w.Str("HELLO proto=").U32(ddp::kProtocolVersion).Str(" fw=").Str(firmware_)
            .Str(" app=NebulaeOnDaisy dev=seed3-desktop boot=BOOT_SRAM bl=").Str(bootloader_)
            .Str(" libdaisy=f715e48 caps=btn,pot,potraw,sw17,sw18,sd,boot,sdmsc,nebmgmt");
        Queue(w);
        SendState(now); SendSys(now); SendReader("STATUS"); SendNeb("STATUS");
        last_state_ = last_sys_ = now;
        return;
    }
    if(!std::strcmp(type, "PING")) {
        uint32_t n = 0;
        if(!ddp::FindField(body, "n", value, sizeof value) || !ddp::ParseU32(value, n)) { Error("PING", "ARG"); return; }
        char b[32];
        ddp::Writer w(b, sizeof b);
        w.Str("PONG n=").U32(n);
        Queue(w);
        return;
    }
    // Everything else needs a session (HELLO), like the diagnostics firmware.
    if(!session_) { Error(type, "SESSION"); return; }
    if(!std::strcmp(type, "BYE")) {
        char b[16]; ddp::Writer w(b, sizeof b); w.Str("ACK c=BYE"); Queue(w);
        session_ = false;  // never returns the card
        return;
    }
    if(!std::strcmp(type, "GET")) {
        SendState(now); SendSys(now); SendReader("STATUS"); SendNeb("STATUS");
        return;
    }
    if(!std::strcmp(type, "RATE")) {
        uint32_t hz = 0;
        if(!ddp::FindField(body, "hz", value, sizeof value) || !ddp::ParseU32(value, hz) || hz < 10 || hz > 100) {
            Error("RATE", "ARG");
            return;
        }
        rate_hz_ = hz;
        char b[32]; ddp::Writer w(b, sizeof b); w.Str("ACK c=RATE hz=").U32(hz); Queue(w);
        return;
    }
    if(!std::strcmp(type, "READER")) {
        if(!ddp::FindField(body, "a", value, sizeof value)) { Error("READER", "ARG"); return; }
        bool ok = true;
        if(!std::strcmp(value, "STATUS")) {
        } else if(!std::strcmp(value, "SHARE")) {
            ok = port_.ReaderIdle() && reader_.Share(port_.Inputs().card);
        } else if(!std::strcmp(value, "RETURN")) {
            ok = port_.ReaderIdle() && reader_.Return();
        } else {
            Error("READER", "ARG");
            return;
        }
        if(!ok) Error("READER", "STATE");
        else SendReader(value);
        return;
    }
    if(!std::strcmp(type, "NEB")) {
        if(!ddp::FindField(body, "a", value, sizeof value)) { Error("NEB", "ARG"); return; }
        if(!std::strcmp(value, "STATUS")) { SendNeb("STATUS"); return; }
        if(!std::strcmp(value, "PERFORM")) {
            if(!BackToPerformanceAllowed() || pending_ != Pending::None) { Error("NEB", "STATE"); return; }
            SendNeb("PERFORM");
            pending_ = Pending::Perform;
            pending_since_ = now;
            return;
        }
        Error("NEB", "ARG");
        return;
    }
    if(!std::strcmp(type, "BOOT")) {
        if(!BackToPerformanceAllowed() || pending_ != Pending::None) { Error("BOOT", "BUSY"); return; }
        char b[16]; ddp::Writer w(b, sizeof b); w.Str("ACK c=BOOT"); Queue(w);
        pending_ = Pending::Boot;
        pending_since_ = now;
        return;
    }
    // LED, SDTEST, CVOUT, TONE, SCOPE: the performance image has no such test here.
    if(!std::strcmp(type, "LED") || !std::strcmp(type, "SDTEST") || !std::strcmp(type, "CVOUT")
       || !std::strcmp(type, "TONE") || !std::strcmp(type, "SCOPE")) {
        Error(type, "UNSUPPORTED");
        return;
    }
    Error(type, "UNKNOWN");
}

uint16_t Admin::Leds(uint32_t now) const {
    uint16_t mask = (now / 500) & 1 ? 0x8000 : 0;  // D16 slow blink: SD management mode
    switch(reader_.State()) {
    case diag::ReaderState::Local: mask |= 0x0001; break;
    case diag::ReaderState::Starting: if((now / 100) & 1) mask |= 0x0002; break;
    case diag::ReaderState::Shared: mask |= 0x0002; break;
    case diag::ReaderState::Ejected: mask |= 0x0008; break;
    case diag::ReaderState::Interrupted:
    case diag::ReaderState::Error: if((now / 250) & 1) mask |= 0x0010; break;
    }
    return mask;
}

void Admin::Step(uint32_t now) {
    if(!started_) { started_ = true; start_ = last_rx_ = last_state_ = last_sys_ = s4_since_ = now; }
    uint8_t byte;
    for(unsigned i = 0; i < 1024 && rx_.Pop(byte); ++i) {
        if(parser_.Feed(char(byte))) {
            last_rx_ = now;
            Handle(parser_.Body(), now);
        }
    }
    if(session_ && now - last_rx_ > kHostTimeoutMs) session_ = false;  // ownership unchanged

    const AdminInputs in = port_.Inputs();
    const uint16_t rise = in.buttons & ~last_buttons_;
    for(unsigned i = 0; i < 16; ++i) if(rise & (1u << i)) ++press_counts_[i];
    last_buttons_ = in.buttons;
    // Device-side return without the PC: S4 alone for 2 s, from LOCAL or EJECTED.
    if(in.buttons == kS4) {
        if(!s4_held_) { s4_held_ = true; s4_since_ = now; }
        else if(now - s4_since_ >= kReturnHoldMs && pending_ == Pending::None) {
            if(reader_.State() == diag::ReaderState::Ejected && port_.ReaderIdle()) reader_.Return();
            if(BackToPerformanceAllowed()) {
                if(session_) SendNeb("PERFORM");
                pending_ = Pending::Perform;
                pending_since_ = now;
            }
            s4_since_ = now;
        }
    } else {
        s4_held_ = false;
    }

    if(session_) {
        if(now - last_state_ >= 1000 / rate_hz_) { last_state_ = now; SendState(now); }
        if(now - last_sys_ >= 1000) {
            last_sys_ = now;
            SendSys(now); SendReader("STATUS"); SendNeb("STATUS");
        }
    }
    Pump();
    // The ACK was handed to USB; give it kSettleMs to leave (kFlushMs if still queued).
    if(pending_ != Pending::None
       && now - pending_since_ >= (tx_fill_ ? kFlushMs : kSettleMs)) {
        const Pending p = pending_;
        pending_ = Pending::None;
        if(p == Pending::Perform) port_.RestartPerformance();
        else port_.EnterBootloader();
    }
    const uint16_t leds = Leds(now);
    if(leds != last_leds_) { last_leds_ = leds; port_.ApplyLeds(leds); }
}

} // namespace neb
