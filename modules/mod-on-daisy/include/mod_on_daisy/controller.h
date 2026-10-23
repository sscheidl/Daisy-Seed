#pragma once
// Main-loop side: live MIDI, SysEx protocol, validation, compiling and
// publishing (ARCHITECTURE.md § 2, PROTOCOL.md). Never call from the audio IRQ.
#include "engine.h"
#include "protocol.h"

namespace mod {
// libDaisy-free MIDI message. For SysEx, `sysex` points to the bytes between F0 and F7.
struct MidiMessage {
    uint8_t status = 0, data0 = 0, data1 = 0;
    const uint8_t* sysex = nullptr;
    std::size_t sysex_size = 0;
};

class MidiSender {
  public:
    virtual ~MidiSender() = default;
    // false: nothing was sent (e.g. no USB host); the reply is dropped.
    virtual bool Send(const uint8_t* bytes, std::size_t size) = 0;
};

struct Stats {
    uint32_t ticks = 0, cycles_min = 0, cycles_avg = 0, cycles_max = 0;
    uint32_t load_avg = 0, load_max = 0; // callback load in 1/10000 (0.01 %)
    uint32_t overruns = 0, midi_lost = 0;
    uint32_t dispatch_max = 0, service_max = 0; // main-loop cycles: HandleMidi batch, Service
};
// Optional diagnostics, implemented by the bench firmware (GET_STATS etc.).
class Diagnostics {
  public:
    virtual ~Diagnostics() = default;
    virtual Stats GetStats() = 0;
    virtual void ResetStats() = 0;
    virtual bool SetScenario(uint8_t scenario) = 0;
};

class Controller {
  public:
    void Init(Engine& engine, LiveState& live, const InstrumentInfo& info, float sample_rate,
              std::size_t block_size, uint8_t device_id = 0, Diagnostics* diagnostics = nullptr) {
        engine_ = &engine;
        live_ = &live;
        info_ = &info;
        sample_rate_ = sample_rate;
        block_size_ = block_size;
        device_ = device_id & 15;
        diagnostics_ = diagnostics;
        allowed_ = AllowedCells(info);
        schema_ = SchemaHash(info);
        Commit(Patch{});
    }

    // Returns true if Mod on Daisy consumed the message. Notes are only observed.
    bool HandleMidi(const MidiMessage& m) {
        switch(m.status & 0xF0) {
            case 0x80: live_->NoteOff(m.data0); return false;
            case 0x90:
                if(m.data1)
                    live_->NoteOn(m.data0);
                else
                    live_->NoteOff(m.data0);
                return false;
            case 0xA0: live_->PolyPressure(m.data0, m.data1); return true;
            case 0xB0:
                if(m.data0 == 1) {
                    live_->ModWheel(m.data1);
                    return true;
                }
                if(m.data0 == 120 || m.data0 == 123)
                    live_->AllNotesOff(); // All Sound/Notes Off: observed, passed on
                return false;
            case 0xD0: live_->ChannelPressure(m.data0); return true;
            default: break;
        }
        switch(m.status) {
            case 0xF0: return m.sysex && HandleSysEx(m.sysex, m.sysex_size);
            case 0xF8: live_->Clock(); return false;
            case 0xFA: live_->Start(); return false;
            default: return false;
        }
    }

    // Sends at most one queued reply per millisecond, then monitor frames.
    void Service(uint32_t now_ms, MidiSender& out) {
        if(monitor_renewed_) {
            monitor_renewed_ = false;
            monitor_since_ = now_ms;
        }
        if(monitor_rate_ && now_ms - monitor_since_ > 3000) {
            monitor_rate_ = 0;
            engine_->EnableMonitor(false);
        }
        if(sent_once_ && now_ms == last_send_ms_)
            return;
        if(tx_count_) {
            const TxFrame& f = tx_[tx_head_];
            if(!out.Send(f.bytes, f.size))
                ++dropped_replies_;
            tx_head_ = (tx_head_ + 1) % kTxQueue;
            --tx_count_;
            MarkSent(now_ms);
            return;
        }
        if(monitor_rate_ && now_ms - last_monitor_ms_ >= 1000u / monitor_rate_) {
            last_monitor_ms_ = now_ms;
            engine_->Monitor().Fetch();
            const MonitorSnapshot& m = engine_->Monitor().Front();
            uint8_t payload[36];
            proto::Writer w(payload, sizeof(payload));
            for(float v : m.source)
                w.U14(proto::EncodeSigned(v));
            for(float v : m.offset)
                w.U14(proto::EncodeSigned(v));
            for(float v : m.cv_raw)
                w.U14(proto::EncodeSigned(v));
            uint8_t frame[proto::kMaxFrame];
            const std::size_t size = proto::BuildFrame(frame, device_, proto::MonitorFrame, 0, payload, w.Size());
            out.Send(frame, size);
            MarkSent(now_ms);
        }
    }

    // Local API for the bench firmware, tests and later hardware shortcuts.
    Status LoadPatch(const Patch& patch) {
        const Status s = Validate(patch, allowed_);
        if(s == Status::Ok)
            Commit(patch);
        return s;
    }
    void SetCalibration(float cv1, float cv2) {
        cv_offset_[0] = cv1;
        cv_offset_[1] = cv2;
        Commit(patch_);
    }
    const Patch& CurrentPatch() const { return patch_; }
    uint32_t CurrentCrc() const { return crc_; }
    uint64_t Allowed() const { return allowed_; }
    uint32_t Schema() const { return schema_; }
    uint8_t DeviceId() const { return device_; }
    uint32_t RejectedFrames() const { return rejected_frames_; }
    uint32_t DroppedReplies() const { return dropped_replies_; }
    std::size_t PendingReplies() const { return tx_count_; }

  private:
    static constexpr std::size_t kTxQueue = 2; // the editor keeps one request open
    struct TxFrame {
        uint8_t bytes[proto::kMaxFrame];
        std::size_t size = 0;
    };

    void MarkSent(uint32_t now_ms) {
        last_send_ms_ = now_ms;
        sent_once_ = true;
    }

    void Commit(const Patch& patch) {
        have_last_ = false; // a cached reply no longer describes the current state
        patch_ = patch;
        crc_ = PatchCrc(patch_);
        engine_->Programs().Back() = Compile(patch_, *info_, sample_rate_, cv_offset_);
        engine_->Programs().Publish();
    }

    bool HandleSysEx(const uint8_t* body, std::size_t size) {
        proto::Frame f;
        const proto::Parse parsed = proto::ParseHeader(body, size, f);
        if(parsed == proto::Parse::NotOurs)
            return false;
        if(parsed == proto::Parse::Short) {
            ++rejected_frames_;
            return true; // ours, but not even a header to answer
        }
        if(f.device != device_ && !(f.device == proto::kBroadcast && f.command == proto::Identify))
            return true; // other device: consumed silently
        if(have_last_ && f.seq == last_seq_ && size == last_size_ && Same(body, size)) {
            Queue(last_reply_, last_reply_size_); // retry: same answer, not executed twice
            return true;
        }
        reply_size_ = 0;
        if(const uint8_t error = proto::Verify(body, size, f)) {
            ++rejected_frames_;
            Nak(f, error);
        } else {
            Execute(f);
        }
        if(reply_size_) {
            Queue(reply_, reply_size_);
            if(Changes(f.command))
                Remember(body, size, f.seq); // reads are always answered fresh
        }
        return true;
    }

    void Execute(const proto::Frame& f) {
        using namespace proto;
        Reader r(f.payload, f.length);
        uint8_t out[kMaxPayload];
        Writer w(out, sizeof(out));
        switch(f.command) {
            case proto::Identify:
                w.U7(kVersion);
                w.U7(proto::kVersionMinor);
                w.U7(kVersionMajor);
                w.U7(mod::kVersionMinor);
                w.U7(kVersionPatch);
                w.Str(info_->key, kMaxKeyLength);
                w.Str(info_->version, 12);
                w.U7(kMaxActive);
                w.U14(unsigned(sample_rate_ / float(block_size_) + 0.5f));
                w.U7(0); // preset slots: storage not implemented (D8)
                w.U32(schema_);
                w.U7(0x02 | (diagnostics_ ? 0x04 : 0) | 0x08); // monitor, diagnostics, Poly AT last note
                return Ack(f, w);
            case proto::GetTarget: {
                const unsigned t = r.U7();
                if(!r.Ok() || !r.AtEnd() || t >= kTargets)
                    return Nak(f, ErrRange);
                const TargetInfo& target = info_->targets[t];
                w.U7(t);
                w.U7((target.name ? 1 : 0) | (target.per_voice ? 2 : 0) | (target.latched ? 4 : 0));
                w.U14(unsigned(target.span * 16383.0f + 0.5f));
                w.Str(target.name, 12);
                w.Str(target.span_text, 8);
                return Ack(f, w);
            }
            case proto::GetAllowed:
                w.Mask64(allowed_);
                return Ack(f, w);
            case proto::GetMatrix: {
                uint8_t depth[kCells];
                for(std::size_t c = 0; c < kCells; ++c)
                    depth[c] = uint8_t(patch_.depth[c / kTargets][c % kTargets]);
                w.Packed(depth, kCells);
                w.Mask64(patch_.EnabledMask());
                w.U32(crc_);
                return Ack(f, w);
            }
            case proto::SetMatrix: {
                uint8_t depth[kCells];
                r.Packed(depth, kCells);
                const uint64_t enabled = r.Mask64();
                if(!r.Ok() || !r.AtEnd())
                    return Nak(f, ErrLength);
                Patch p = patch_;
                for(std::size_t c = 0; c < kCells; ++c)
                    p.depth[c / kTargets][c % kTargets] = int8_t(depth[c]);
                p.SetEnabledMask(enabled);
                return Change(f, p);
            }
            case proto::SetCell: {
                const unsigned s = r.U7(), t = r.U7(), magnitude = r.U7(), bits = r.U7();
                if(!r.Ok() || !r.AtEnd() || s >= kSources || t >= kTargets || magnitude > unsigned(kMaxDepth)
                   || bits > 3)
                    return Nak(f, ErrRange);
                if(!(allowed_ & CellBit(s, t)))
                    return Nak(f, ErrNotAllowed);
                Patch p = patch_;
                p.depth[s][t] = int8_t((bits & 1) ? -int(magnitude) : int(magnitude));
                p.SetEnabled(s, t, bits & 2);
                return Change(f, p);
            }
            case proto::ClearMatrix: {
                if(!r.AtEnd())
                    return Nak(f, ErrLength);
                Patch p = patch_;
                for(auto& row : p.depth)
                    for(int8_t& d : row)
                        d = 0;
                p.SetEnabledMask(0);
                return Change(f, p);
            }
            case proto::GetSource: {
                const unsigned s = r.U7();
                if(!r.Ok() || !r.AtEnd() || s > Random)
                    return Nak(f, ErrRange);
                w.U7(s);
                if(s == Random) {
                    const RandomParams& p = patch_.random;
                    w.U14(p.rate); w.U7(p.mode); w.U7(p.sync); w.U7(p.gate); w.U7(p.division); w.U7(0);
                } else {
                    const LfoParams& p = patch_.lfo[s];
                    w.U14(p.rate); w.U7(p.shape); w.U7(p.sync); w.U7(p.gate); w.U7(p.division); w.U7(p.retrigger);
                }
                return Ack(f, w);
            }
            case proto::SetSource: {
                const unsigned s = r.U7(), param = r.U7(), value = r.U14();
                if(!r.Ok() || !r.AtEnd() || s > Random || param >= kSourceParamCount)
                    return Nak(f, ErrRange);
                Patch p = patch_;
                if(!SetSourceParam(p, s, param, value))
                    return Nak(f, ErrRange);
                return Change(f, p);
            }
            case proto::GetName:
                w.Str(patch_.name, sizeof(patch_.name));
                return Ack(f, w);
            case proto::SetName: {
                Patch p = patch_;
                r.Str(p.name, sizeof(p.name));
                if(!r.Ok() || !r.AtEnd())
                    return Nak(f, ErrRange);
                return Change(f, p);
            }
            case proto::GetCalibration:
                w.U14(EncodeSigned(cv_offset_[0]));
                w.U14(EncodeSigned(cv_offset_[1]));
                w.U7(device_);
                return Ack(f, w);
            case proto::SetCalibration: {
                const unsigned cv1 = r.U14(), cv2 = r.U14(), device = r.U7();
                if(!r.Ok() || !r.AtEnd() || device > 15)
                    return Nak(f, ErrRange);
                SetCalibration(DecodeSigned(cv1), DecodeSigned(cv2)); // RAM only in v0.1
                device_ = uint8_t(device);
                w.U7(0);
                return Ack(f, w);
            }
            case proto::Monitor: {
                const unsigned rate = r.U7();
                if(!r.Ok() || !r.AtEnd() || rate > 25)
                    return Nak(f, ErrRange);
                monitor_rate_ = uint8_t(rate);
                monitor_renewed_ = true;
                engine_->EnableMonitor(rate != 0);
                w.U7(rate);
                return Ack(f, w);
            }
            case proto::GetStats: {
                if(!diagnostics_)
                    return Nak(f, ErrNotImplemented);
                const Stats s = diagnostics_->GetStats();
                for(uint32_t v : {s.ticks, s.cycles_min, s.cycles_avg, s.cycles_max, s.load_avg, s.load_max,
                                  s.overruns, s.midi_lost, s.dispatch_max, s.service_max})
                    w.U32(v);
                return Ack(f, w);
            }
            case proto::ResetStats:
                if(!diagnostics_)
                    return Nak(f, ErrNotImplemented);
                diagnostics_->ResetStats();
                return Ack(f, w);
            case proto::SetScenario: {
                const unsigned scenario = r.U7();
                if(!diagnostics_)
                    return Nak(f, ErrNotImplemented);
                if(!r.Ok() || !r.AtEnd() || !diagnostics_->SetScenario(uint8_t(scenario)))
                    return Nak(f, ErrRange);
                w.U7(scenario);
                return Ack(f, w);
            }
            case proto::Store:
            case proto::Recall:
            case proto::GetSlotInfo:
            case proto::SetPowerOn: return Nak(f, ErrNotImplemented); // STORAGE.md: specified only
            default: return Nak(f, ErrUnknown);
        }
    }

    static bool SetSourceParam(Patch& p, unsigned s, unsigned param, unsigned value) {
        uint16_t* rate;
        uint8_t *shape, *sync, *gate, *division, *retrigger = nullptr;
        if(s == Random) {
            rate = &p.random.rate; shape = &p.random.mode; sync = &p.random.sync;
            gate = &p.random.gate; division = &p.random.division;
        } else {
            LfoParams& l = p.lfo[s];
            rate = &l.rate; shape = &l.shape; sync = &l.sync;
            gate = &l.gate; division = &l.division; retrigger = &l.retrigger;
        }
        switch(param) {
            case ParamRate: *rate = uint16_t(value); break;
            case ParamShape: *shape = uint8_t(value > 255 ? 255 : value); break;
            case ParamSync: {
                const uint8_t next = uint8_t(value > 255 ? 255 : value);
                // The division index means something else per clock source (MIDI 1/4 vs.
                // gate 4/1): a change of source starts at that source's default.
                if(next != *sync)
                    *division = next == GateClock ? kDefaultGateDivision : kDefaultMidiDivision;
                *sync = next;
                break;
            }
            case ParamGate: *gate = uint8_t(value > 255 ? 255 : value); break;
            case ParamDivision: *division = uint8_t(value > 255 ? 255 : value); break;
            case ParamRetrigger:
                if(!retrigger)
                    return false;
                *retrigger = uint8_t(value > 255 ? 255 : value);
                break;
            default: return false;
        }
        return s == Random ? ValidateSource(p.random) == Status::Ok : ValidateSource(p.lfo[s]) == Status::Ok;
    }

    void Change(const proto::Frame& f, const Patch& p) {
        const Status s = Validate(p, allowed_);
        if(s != Status::Ok)
            return Nak(f, uint8_t(s));
        Commit(p);
        uint8_t out[7];
        proto::Writer w(out, sizeof(out));
        w.U7(0);
        w.U7(PopCount(patch_.ActiveMask()));
        w.U32(crc_);
        Ack(f, w);
    }
    void Ack(const proto::Frame& f, const proto::Writer& w) {
        reply_size_ = proto::BuildFrame(reply_, device_, uint8_t(f.command | proto::kReplyBit), f.seq, w.Data(),
                                        w.Size());
    }
    void Nak(const proto::Frame& f, uint8_t code) {
        const uint8_t payload[2] = {uint8_t(f.command & 127), code};
        reply_size_ = proto::BuildFrame(reply_, device_, proto::kNak, f.seq, payload, 2);
    }
    void Queue(const uint8_t* bytes, std::size_t size) {
        if(tx_count_ == kTxQueue) {
            ++dropped_replies_; // the editor retries; the duplicate cache answers it
            return;
        }
        TxFrame& slot = tx_[(tx_head_ + tx_count_) % kTxQueue];
        for(std::size_t i = 0; i < size; ++i)
            slot.bytes[i] = bytes[i];
        slot.size = size;
        ++tx_count_;
    }
    // Only state-changing commands are answered from the retry cache.
    static bool Changes(uint8_t command) {
        switch(command) {
            case proto::SetMatrix:
            case proto::SetCell:
            case proto::ClearMatrix:
            case proto::SetSource:
            case proto::SetName:
            case proto::SetCalibration:
            case proto::SetScenario: return true;
            default: return false;
        }
    }
    bool Same(const uint8_t* body, std::size_t size) const {
        for(std::size_t i = 0; i < size; ++i)
            if(last_request_[i] != body[i])
                return false;
        return true;
    }
    void Remember(const uint8_t* body, std::size_t size, uint8_t seq) {
        if(size > sizeof(last_request_))
            return;
        for(std::size_t i = 0; i < size; ++i)
            last_request_[i] = body[i];
        last_size_ = size;
        last_seq_ = seq;
        for(std::size_t i = 0; i < reply_size_; ++i)
            last_reply_[i] = reply_[i];
        last_reply_size_ = reply_size_;
        have_last_ = true;
    }

    Engine* engine_ = nullptr;
    LiveState* live_ = nullptr;
    const InstrumentInfo* info_ = nullptr;
    Diagnostics* diagnostics_ = nullptr;
    float sample_rate_ = 48000.0f;
    std::size_t block_size_ = 48;
    uint8_t device_ = 0;
    uint64_t allowed_ = 0;
    uint32_t schema_ = 0, crc_ = 0;
    Patch patch_{};
    float cv_offset_[2]{};
    uint8_t reply_[proto::kMaxFrame]{};
    std::size_t reply_size_ = 0;
    uint8_t last_request_[proto::kMaxFrame]{}, last_reply_[proto::kMaxFrame]{};
    std::size_t last_size_ = 0, last_reply_size_ = 0;
    uint8_t last_seq_ = 0;
    bool have_last_ = false;
    TxFrame tx_[kTxQueue]{};
    std::size_t tx_head_ = 0, tx_count_ = 0;
    uint32_t last_send_ms_ = 0, last_monitor_ms_ = 0, monitor_since_ = 0;
    bool sent_once_ = false, monitor_renewed_ = false;
    uint8_t monitor_rate_ = 0;
    uint32_t rejected_frames_ = 0, dropped_replies_ = 0;
};
}
