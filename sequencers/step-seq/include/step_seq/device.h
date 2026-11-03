#pragma once
// SPDX-License-Identifier: MIT
// SQ device side (PROTOCOL.md): answers requests in the main loop, hands changes to the
// audio callback through a Host and sends event-based monitor frames (<= 50 per second).
// Shared by every Acid Seq firmware; the components and their values come from the host.
#include "cv.h"
#include "param_table.h"
#include "pattern.h"
#include "sq_protocol.h"
#include <cstddef>
#include <cstring>

namespace sq {

constexpr uint32_t kMonitorHoldMs = 3000, kMonitorIntervalMs = 20; // 50 frames per second
enum Counter : uint8_t {
    CntBadLength, CntBadCrc, CntBadVersion, CntUnknownType, CntTooLong, CntStatusInFrame, CntDuplicate,
    CntEcho, CntBusy, CntMonitorDropped, CntNoteOnDropped, CntMidiQueueOverflow, CntClockIgnored,
    CntCallbackOverruns, CntCallbackPeak, CntQueueHighWater, CntStackFree, kCounters
};
// Bit 6 (SQ 1.1): the device also speaks Mod on Daisy (7D 4D 44) on the same port.
enum Capability : uint16_t {
    CapCv = 1, CapDin = 2, CapVoice = 4, CapPattern = 8, CapMonitor = 16, CapCalibration = 32, CapModMatrix = 64,
};
// Kind 4 (SQ 1.1): the firmware product's own parameters (mix, MIDI channel, kit state).
enum Kind : uint8_t { KindSequencer = 0, KindInstrument = 1, KindEffect = 2, KindDsp = 3, KindProduct = 4 };

struct Component {
    const char* id;
    uint8_t kind;
    const char* version;
    const ComponentParams* params;
    int32_t* values; // current values, one per parameter (host keeps them up to date)
};

// What the callback reported last (snapshot), as the device presents it.
struct View {
    uint8_t state = 0, mode = 0, sw17 = 0, sw18 = 1, active = 0;
    int8_t step = -1;
    bool gate = false;
    int32_t tempo_milli = 120000;
    uint16_t applied_revision = 0;
    bool cv_enabled = false, usb_connected = false;
    step_seq::Pattern pattern{};
};

class Host {
  public:
    virtual ~Host() = default;
    // Hand-over to the callback; false: queue full (NAK BUSY).
    virtual bool PushParam(uint8_t component, uint16_t id, int32_t value, uint16_t revision) = 0;
    virtual bool PushPattern(const step_seq::Pattern& pattern, uint16_t revision) = 0;
    virtual bool PushStep(uint8_t index, const step_seq::Step& step, uint16_t revision) = 0;
    // 0 stop, 1 start; false: not allowed in the current state.
    virtual bool PushTransport(uint8_t command) = 0;
    virtual bool PushCal(uint8_t output, const step_seq::OutputCal& cal) = 0;
    virtual bool PushCvEnable(bool on) = 0;
    virtual void Send(const uint8_t* bytes, std::size_t size) = 0;
};

class Device {
  public:
    const char* product = "step-seq";
    const char* version = "0.0.0";
    // Minor protocol version reported by IDENTIFY; products using SQ 1.1 fields set 1.
    uint8_t protocol_minor = kVersionMinor;
    uint32_t boot_id = 0;
    uint32_t uid[3] = {};
    uint16_t capabilities = CapCv | CapDin | CapPattern | CapMonitor | CapCalibration;
    Component* components = nullptr;
    uint8_t component_count = 0;
    View view;
    step_seq::OutputCal cal[2];
    uint32_t counters[kCounters] = {};

    explicit Device(Host& host) : host_(host) {}

    // One SysEx (F0 ... F7) from USB. Returns true if it was an SQ frame (consumed).
    bool Handle(const uint8_t* frame, std::size_t size, uint32_t now_ms) {
        const Parsed p = Classify(frame, size);
        switch(p.verdict) {
            case Verdict::Foreign: return false;
            case Verdict::Ignore: ++counters[CntEcho]; return true;
            case Verdict::Nak:
                Count(p.error, frame, size);
                Nak(p.type, p.rid, p.error);
                host_.Send(reply_, reply_size_);
                return true;
            case Verdict::Ok: break;
        }
        if(have_last_ && p.rid == last_rid_ && p.crc == last_crc_ && p.type == last_type_) {
            ++counters[CntDuplicate]; // a retry: the cached reply, nothing runs twice
            host_.Send(last_reply_, last_size_);
            return true;
        }
        now_ = now_ms;
        reply_size_ = 0;
        Execute(p);
        if(reply_size_) {
            std::memcpy(last_reply_, reply_, reply_size_);
            last_size_ = reply_size_;
            last_rid_ = p.rid;
            last_crc_ = p.crc;
            last_type_ = p.type;
            have_last_ = true;
            host_.Send(reply_, reply_size_);
        }
        return true;
    }

    // Main loop: event-based monitor frames from the latest view.
    void Service(uint32_t now_ms) {
        if(!monitor_ || now_ms - monitor_since_ > kMonitorHoldMs) {
            monitor_ = false;
            seen_ = view;
            return;
        }
        if(view.step != seen_.step || view.gate != seen_.gate)
            pending_ |= 1u << EvStep;
        if(view.state != seen_.state || view.mode != seen_.mode || view.sw17 != seen_.sw17 ||
           view.sw18 != seen_.sw18 || view.active != seen_.active)
            pending_ |= 1u << EvState;
        if(view.tempo_milli != seen_.tempo_milli)
            pending_ |= 1u << EvTempo;
        if(view.applied_revision != seen_.applied_revision)
            pending_ |= 1u << EvRevision;
        for(int i = 0; i < step_seq::kSteps; ++i)
            if(!(view.pattern[std::size_t(i)] == seen_.pattern[std::size_t(i)])) {
                edited_ |= 1u << i;
                pending_ |= 1u << EvStepEdit;
            }
        seen_ = view;
        if(now_ms - last_monitor_ < kMonitorIntervalMs || !pending_)
            return;
        static constexpr uint8_t kOrder[] = {EvStep, EvState, EvStepEdit, EvParam, EvTempo, EvRevision, EvStats};
        for(uint8_t code : kOrder) {
            if(!(pending_ & (1u << code)))
                continue;
            if(SendEvent(code)) {
                last_monitor_ = now_ms;
                return;
            }
        }
    }
    // A parameter changed on the kit (pot, switch, CC): report it in the monitor. Changes
    // coalesce per parameter (the latest value wins), so no parameter is ever left out.
    void ParamChanged(uint8_t component, uint16_t id) {
        const ParamDesc* d = component < component_count && components[component].params
                                 ? FindParam(*components[component].params, id)
                                 : nullptr;
        const std::ptrdiff_t index = d ? d - components[component].params->params : -1;
        if(component >= kMaxMonitorComponents || index < 0 || index >= 64) {
            ++counters[CntMonitorDropped]; // outside the table: cannot be reported
            return;
        }
        param_dirty_[component] |= uint64_t(1) << index;
        pending_ |= 1u << EvParam;
    }
    uint16_t AcceptedRevision() const { return accepted_; }

  private:
    void Count(uint8_t error, const uint8_t* frame, std::size_t size) {
        if(error == ErrCrc) {
            bool status = false;
            for(std::size_t i = 1; i + 1 < size; ++i)
                status |= (frame[i] & 0x80) != 0;
            ++counters[status ? CntStatusInFrame : CntBadCrc];
        } else if(error == ErrLength) {
            ++counters[size > kMaxFrame ? CntTooLong : CntBadLength];
        } else if(error == ErrVersion) {
            ++counters[CntBadVersion];
        }
    }
    Writer Reply(uint8_t type, uint8_t rid) {
        Writer w(reply_, sizeof(reply_));
        w.Begin(uint8_t(type | kResponse), rid);
        return w;
    }
    void Finish(Writer& w) { reply_size_ = w.End(); }
    void Nak(uint8_t type, uint8_t rid, uint8_t error) {
        Writer w(reply_, sizeof(reply_));
        w.Begin(kNak, rid);
        w.U7(type & 0x7F);
        w.U7(error);
        reply_size_ = w.End();
        if(error == ErrBusy)
            ++counters[CntBusy];
        if(error == ErrUnknownType)
            ++counters[CntUnknownType];
    }
    void NakNow(const Parsed& p, uint8_t error) { Nak(p.type, p.rid, error); }
    bool ReadStep(Reader& r, step_seq::Step& s) {
        s.note = r.U7();
        s.gate_percent = r.U7();
        const uint8_t bits = r.U7();
        s.gate = bits & 1;
        s.accent = bits & 2;
        s.slide = bits & 4;
        return r.Ok() && bits < 8 && s.Valid();
    }
    static void WriteStep(Writer& w, const step_seq::Step& s) {
        w.U7(s.note);
        w.U7(s.gate_percent);
        w.U7(s.Bits());
    }
    bool CheckUid(Reader& r) {
        const uint32_t a = r.U32(), b = r.U32(), c = r.U32();
        return a == uid[0] && b == uid[1] && c == uid[2];
    }
    static void WriteCal(Writer& w, const step_seq::OutputCal& c) {
        w.I32(c.offset);
        w.I32(c.scale);
        w.U14(c.gate_low);
        w.U14(c.gate_high);
        w.U14(c.clamp_min);
        w.U14(c.clamp_max);
    }
    uint16_t NextRevision() { return accepted_ = uint16_t((accepted_ + 1) & 0x3FFF); }

    void Execute(const Parsed& p) {
        Reader r(p.payload, p.size);
        switch(p.type) {
            case Identify: {
                if(!r.Done())
                    return NakNow(p, ErrBadArgument);
                Writer w = Reply(p.type, p.rid);
                w.U7(kVersion);
                w.U7(protocol_minor);
                w.Str(product, 16);
                w.Str(version, 12);
                w.U32(boot_id);
                w.U32(uid[0]);
                w.U32(uid[1]);
                w.U32(uid[2]);
                w.U14(capabilities);
                w.U7(component_count);
                return Finish(w);
            }
            case GetComponent: {
                const uint8_t index = r.U7();
                if(!r.Done() || index >= component_count)
                    return NakNow(p, ErrBadArgument);
                const Component& c = components[index];
                Writer w = Reply(p.type, p.rid);
                w.U7(index);
                w.U7(component_count);
                w.Str(c.id, 24); // SQ 1.1: component IDs up to 24 characters (registry IDs)
                w.U7(c.kind);
                w.Str(c.version, 12);
                w.U7(c.params ? c.params->schema : 0);
                w.U7(c.params ? c.params->count : 0);
                return Finish(w);
            }
            case GetParamDesc: {
                const uint8_t ci = r.U7(), pi = r.U7();
                if(!r.Done() || ci >= component_count || !components[ci].params || pi >= components[ci].params->count)
                    return NakNow(p, ErrBadArgument);
                const ParamDesc& d = components[ci].params->params[pi];
                Writer w = Reply(p.type, p.rid);
                w.U7(ci);
                w.U7(pi);
                w.U7(components[ci].params->count);
                w.U14(d.id);
                w.U7(d.type);
                w.U7(d.unit);
                w.U7(d.flags);
                w.I32(d.min);
                w.I32(d.max);
                w.I32(d.def);
                w.U7(d.cc);
                w.Str(d.key, 16);
                return Finish(w);
            }
            case GetEnumLabel: {
                const uint8_t ci = r.U7();
                const uint16_t id = r.U14();
                const uint8_t value = r.U7();
                const ParamDesc* d = r.Done() && ci < component_count && components[ci].params
                                         ? FindParam(*components[ci].params, id)
                                         : nullptr;
                if(!d || value >= d->enum_count)
                    return NakNow(p, ErrBadArgument);
                Writer w = Reply(p.type, p.rid);
                w.U7(ci);
                w.U14(id);
                w.U7(value);
                w.Str(d->enum_keys[value], 16);
                return Finish(w);
            }
            case GetParams: {
                const uint8_t ci = r.U7(), first = r.U7();
                if(!r.Done() || ci >= component_count || !components[ci].params ||
                   first > components[ci].params->count)
                    return NakNow(p, ErrBadArgument);
                const ComponentParams& cp = *components[ci].params;
                const uint8_t n = uint8_t(cp.count - first > 14 ? 14 : cp.count - first);
                Writer w = Reply(p.type, p.rid);
                w.U7(ci);
                w.U7(first);
                w.U7(cp.count);
                w.U7(n);
                for(uint8_t i = 0; i < n; ++i) {
                    w.U14(cp.params[first + i].id);
                    w.I32(components[ci].values[first + i]);
                }
                return Finish(w);
            }
            case SetParam: {
                const uint8_t ci = r.U7();
                const uint16_t id = r.U14();
                int32_t value = r.I32();
                const ParamDesc* d = r.Done() && ci < component_count && components[ci].params
                                         ? FindParam(*components[ci].params, id)
                                         : nullptr;
                if(!d || (d->flags & FlagReadonly))
                    return NakNow(p, ErrBadArgument);
                value = Constrain(*d, value);
                const uint16_t revision = uint16_t((accepted_ + 1) & 0x3FFF);
                if(!host_.PushParam(ci, id, value, revision))
                    return NakNow(p, ErrBusy);
                NextRevision();
                components[ci].values[d - components[ci].params->params] = value;
                Writer w = Reply(p.type, p.rid);
                w.U7(0);
                w.U14(id);
                w.I32(value);
                w.U14(accepted_);
                return Finish(w);
            }
            case GetPattern: {
                if(!r.Done())
                    return NakNow(p, ErrBadArgument);
                Writer w = Reply(p.type, p.rid);
                w.U14(accepted_);
                w.U14(view.applied_revision);
                for(const auto& s : view.pattern)
                    WriteStep(w, s);
                return Finish(w);
            }
            case SetPattern: {
                step_seq::Pattern pattern;
                for(auto& s : pattern)
                    if(!ReadStep(r, s))
                        return NakNow(p, ErrBadArgument);
                if(!r.Done())
                    return NakNow(p, ErrBadArgument);
                const uint16_t revision = uint16_t((accepted_ + 1) & 0x3FFF);
                if(!host_.PushPattern(pattern, revision))
                    return NakNow(p, ErrBusy);
                NextRevision();
                return Status(p, 0, true);
            }
            case SetStep: {
                const uint8_t index = r.U7();
                step_seq::Step s;
                if(!ReadStep(r, s) || !r.Done() || index >= step_seq::kSteps)
                    return NakNow(p, ErrBadArgument);
                const uint16_t revision = uint16_t((accepted_ + 1) & 0x3FFF);
                if(!host_.PushStep(index, s, revision))
                    return NakNow(p, ErrBusy);
                NextRevision();
                return Status(p, 0, true);
            }
            case TransportCmd: {
                const uint8_t command = r.U7();
                if(!r.Done() || command > 1)
                    return NakNow(p, ErrBadArgument);
                if(!host_.PushTransport(command))
                    return NakNow(p, ErrNotAllowed);
                Writer w = Reply(p.type, p.rid);
                w.U7(0);
                w.U7(view.state);
                return Finish(w);
            }
            case GetState: {
                if(!r.Done())
                    return NakNow(p, ErrBadArgument);
                Writer w = Reply(p.type, p.rid);
                w.U7(view.state);
                w.U7(view.mode);
                w.U7(view.sw17);
                w.U7(view.sw18);
                w.U7(view.active);
                w.U7(view.step < 0 ? 127 : uint8_t(view.step));
                w.I32(view.tempo_milli);
                w.U14(accepted_);
                w.U14(view.applied_revision);
                w.U7(view.cv_enabled);
                w.U7(view.usb_connected);
                return Finish(w);
            }
            case GetCal: {
                const uint8_t output = r.U7();
                if(!r.Done() || output > 1)
                    return NakNow(p, ErrBadArgument);
                Writer w = Reply(p.type, p.rid);
                w.U7(output);
                w.U7(cal[output].valid);
                WriteCal(w, cal[output]);
                return Finish(w);
            }
            case SetCal: {
                const bool same = CheckUid(r);
                const uint8_t output = r.U7();
                step_seq::OutputCal c;
                c.offset = r.I32();
                c.scale = r.I32();
                c.gate_low = r.U14();
                c.gate_high = r.U14();
                c.clamp_min = r.U14();
                c.clamp_max = r.U14();
                if(!r.Done() || output > 1)
                    return NakNow(p, ErrBadArgument);
                if(!same)
                    return NakNow(p, ErrUidMismatch);
                if(!step_seq::ValidCal(c))
                    return NakNow(p, ErrBadArgument);
                c.valid = true;
                if(!host_.PushCal(output, c))
                    return NakNow(p, ErrBusy);
                cal[output] = c;
                return Status(p, 0, false);
            }
            case CvEnable: {
                const bool same = CheckUid(r);
                const uint8_t on = r.U7();
                if(!r.Done() || on > 1)
                    return NakNow(p, ErrBadArgument);
                if(!same)
                    return NakNow(p, ErrUidMismatch);
                if(on && !(cal[0].valid && cal[1].valid))
                    return NakNow(p, ErrNotCalibrated);
                if(!host_.PushCvEnable(on))
                    return NakNow(p, ErrBusy);
                Writer w = Reply(p.type, p.rid);
                w.U7(0);
                w.U7(on);
                return Finish(w);
            }
            case Monitor: {
                const uint8_t on = r.U7();
                if(!r.Done() || on > 1)
                    return NakNow(p, ErrBadArgument);
                monitor_ = on;
                monitor_since_ = now_;
                if(on)
                    pending_ |= (1u << EvState) | (1u << EvStep) | (1u << EvTempo) | (1u << EvRevision);
                Writer w = Reply(p.type, p.rid);
                w.U7(on);
                return Finish(w);
            }
            case GetStats: {
                if(!r.Done())
                    return NakNow(p, ErrBadArgument);
                Writer w = Reply(p.type, p.rid);
                w.U7(kCounters);
                for(uint32_t c : counters)
                    w.U32(c);
                return Finish(w);
            }
            case ResetStats: {
                if(!r.Done())
                    return NakNow(p, ErrBadArgument);
                for(auto& c : counters)
                    c = 0;
                return Status(p, 0, false);
            }
            default: return NakNow(p, ErrUnknownType);
        }
    }
    void Status(const Parsed& p, uint8_t status, bool with_revision) {
        Writer w = Reply(p.type, p.rid);
        w.U7(status);
        if(with_revision)
            w.U14(accepted_);
        Finish(w);
    }
    bool SendEvent(uint8_t code) {
        uint8_t frame[kMaxFrame];
        Writer w(frame, sizeof(frame));
        w.Begin(kMonitorEvent, monitor_rid_);
        w.U7(code);
        switch(code) {
            case EvStep:
                w.U7(view.step < 0 ? 127 : uint8_t(view.step));
                w.U7(view.gate);
                break;
            case EvState:
                w.U7(view.state);
                w.U7(view.mode);
                w.U7(view.sw17);
                w.U7(view.sw18);
                w.U7(view.active);
                break;
            case EvTempo: w.I32(view.tempo_milli); break;
            case EvRevision:
                w.U14(accepted_);
                w.U14(view.applied_revision);
                break;
            case EvParam: {
                uint8_t component = 0;
                while(component < kMaxMonitorComponents && !param_dirty_[component])
                    ++component;
                if(component == kMaxMonitorComponents) {
                    pending_ &= ~(1u << code);
                    return false;
                }
                int index = 0;
                while(!(param_dirty_[component] & (uint64_t(1) << index)))
                    ++index;
                param_dirty_[component] &= ~(uint64_t(1) << index);
                const Component& c = components[component];
                w.U7(component);
                w.U14(c.params->params[index].id);
                w.I32(c.values[index]);
                const std::size_t n = w.End();
                bool more = false;
                for(uint64_t bits : param_dirty_)
                    more = more || bits;
                if(!more)
                    pending_ &= ~(1u << code); // more changed parameters follow otherwise
                monitor_rid_ = uint8_t((monitor_rid_ + 1) & 0x7F);
                host_.Send(frame, n);
                return true;
            }
            case EvStepEdit: {
                int index = 0;
                while(index < step_seq::kSteps && !(edited_ & (1u << index)))
                    ++index;
                if(index == step_seq::kSteps) {
                    pending_ &= ~(1u << code);
                    return false;
                }
                edited_ &= ~(1u << index);
                w.U7(uint8_t(index));
                WriteStep(w, view.pattern[std::size_t(index)]);
                if(edited_)
                    pending_ |= 1u << code; // more edited steps follow in the next frames
                const std::size_t n = w.End();
                if(!edited_)
                    pending_ &= ~(1u << code);
                monitor_rid_ = uint8_t((monitor_rid_ + 1) & 0x7F);
                host_.Send(frame, n);
                return true;
            }
            default: break;
        }
        pending_ &= ~(1u << code);
        const std::size_t n = w.End();
        if(!n)
            return false;
        monitor_rid_ = uint8_t((monitor_rid_ + 1) & 0x7F);
        host_.Send(frame, n);
        return true;
    }
    Host& host_;
    uint8_t reply_[kMaxFrame] = {}, last_reply_[kMaxFrame] = {};
    std::size_t reply_size_ = 0, last_size_ = 0;
    uint8_t last_rid_ = 0, last_crc_ = 0, last_type_ = 0;
    bool have_last_ = false;
    uint16_t accepted_ = 0;
    uint32_t now_ = 0;
    bool monitor_ = false;
    uint32_t monitor_since_ = 0, last_monitor_ = 0;
    uint32_t pending_ = 0, edited_ = 0;
    uint8_t monitor_rid_ = 0;
    static constexpr uint8_t kMaxMonitorComponents = 8; // up to 64 parameters each
    uint64_t param_dirty_[kMaxMonitorComponents] = {};
    View seen_;
};

} // namespace sq
