#pragma once
// SQ protocol on the editor side (sequencers/step-seq/docs/PROTOCOL.md, ARBEITSAUFTRAG
// 4.9, R09/R10/R16): a dispatcher that keeps Mod on Daisy (7D 4D 44), SQ (7D 53 51) and the
// bootloader reply (7D 42 4C) apart, and the SQ client of the Acid Seq panel pages.
//
// Connect() reads the device first (D13: the device has priority): IDENTIFY, components,
// parameter descriptors and enum labels, values, pattern, state, calibration, then the
// monitor. Nothing is written to the device without an explicit call (an edit or
// Upload()). One request at a time, 200 ms timeout, three retries with the same request ID.
//
// Single-threaded: OnMessage(), Tick() and all calls on the GUI thread.
#include "Session.h"
#include "step_seq/pattern.h"
#include "step_seq/sq_protocol.h"
#include <climits>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace editor {

enum class Protocol { Unknown, ModOnDaisy, Sq, Bootloader };

// Which conversation a complete SysEx F0 ... F7 belongs to.
inline Protocol ClassifySysEx(const uint8_t* m, std::size_t n) {
    if(n < 5 || m[0] != 0xF0 || m[1] != 0x7D)
        return Protocol::Unknown;
    if(m[2] == 0x4D && m[3] == 0x44)
        return Protocol::ModOnDaisy;
    if(m[2] == 0x53 && m[3] == 0x51)
        return Protocol::Sq;
    if(m[2] == 0x42 && m[3] == 0x4C)
        return Protocol::Bootloader;
    return Protocol::Unknown;
}

struct SqParam {
    uint16_t id = 0;
    std::string key;
    uint8_t type = 0, unit = 0, flags = 0, cc = 127;
    int32_t min = 0, max = 0, def = 0, value = 0;
    std::vector<std::string> enum_keys;
    bool Readonly() const { return flags & 1; }
    bool Log() const { return flags & 2; }
    double Value() const { return value / 1000.0; }
};

struct SqComponent {
    std::string id, version;
    uint8_t kind = 0, schema = 0, params = 0;
    std::vector<SqParam> parameters;
    const SqParam* Find(const std::string& key) const {
        for(const auto& p : parameters)
            if(p.key == key)
                return &p;
        return nullptr;
    }
    SqParam* Find(const std::string& key) {
        for(auto& p : parameters)
            if(p.key == key)
                return &p;
        return nullptr;
    }
};

struct SqIdentity {
    uint8_t protocol_major = 0, protocol_minor = 0;
    std::string product, version;
    uint32_t boot_id = 0, uid[3] = {};
    uint16_t capabilities = 0;
    uint8_t component_count = 0;
};

struct SqState {
    uint8_t state = 0, mode = 0, sw17 = 0, sw18 = 1, source = 0;
    int step = -1;
    bool gate = false;
    double bpm = 120;
    uint16_t accepted = 0, applied = 0;
    bool cv_enabled = false, usb = false;
};

struct SqCal {
    bool valid = false;
    int32_t offset = 0, scale = 0;
    uint16_t gate_low = 0, gate_high = 0, clamp_min = 0, clamp_max = 4095;
};

// Calibration offset/scale in the panel: milli-codes shown as codes with up to three
// decimals and read back exactly (Codex F13: -409600 must not become -409000).
inline std::string CalText(int32_t milli) {
    const int64_t m = milli;
    const int64_t whole = (m < 0 ? -m : m) / 1000, frac = (m < 0 ? -m : m) % 1000;
    std::string text = (m < 0 ? "-" : "") + std::to_string(whole);
    if(frac) {
        std::string digits = std::to_string(1000 + frac).substr(1);
        while(digits.back() == '0')
            digits.pop_back();
        text += "." + digits;
    }
    return text;
}
inline int32_t CalMilli(double codes) {
    const double milli = codes * 1000.0;
    if(!(milli > -2147483648.0 && milli < 2147483647.0))
        return milli > 0 ? INT32_MAX : INT32_MIN; // refused by the device's range check
    return int32_t(milli < 0 ? milli - 0.5 : milli + 0.5);
}

inline const char* SqStateName(uint8_t s) {
    static const char* names[] = {"STOP", "WARTET", "LÄUFT", "PAUSE"};
    return s < 4 ? names[s] : "?";
}
inline std::string SqNoteName(int note) { // step note 0..59 relative to C1
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "H"};
    return std::string(names[note % 12]) + std::to_string(note / 12 + 1);
}
inline std::string SqErrorText(uint8_t code) {
    switch(code) {
        case 1: return "Länge";
        case 2: return "Prüfsumme";
        case 3: return "unbekannter Typ";
        case 4: return "Wert außerhalb des Bereichs";
        case 5: return "beschäftigt";
        case 6: return "Protokollversion";
        case 7: return "anderes Kit (UID)";
        case 8: return "nicht kalibriert";
        case 9: return "im aktuellen Zustand nicht erlaubt";
        default: return "Fehler " + std::to_string(code);
    }
}

class SqSession {
  public:
    static constexpr double kTimeoutMs = 200.0;
    static constexpr int kRetries = 3;
    static constexpr double kMonitorRenewMs = 1000.0;
    enum class Phase { Offline, Reading, Ready, Failed };
    using Done = std::function<void(bool ok, uint8_t error)>;

    explicit SqSession(editor::Transport& transport) : transport_(transport) {}
    // Any change of the model: the UI redraws.
    std::function<void()> changed;
    // Log lines for the UI (German, as the rest of the editor).
    std::function<void(LogKind, const std::string&)> log;

    // Starts reading the device (nothing is written).
    // identify_retries: fewer retries when probing a port that may hold an MD device.
    void Connect(double now_ms, int identify_retries = kRetries) {
        Reset();
        phase_ = Phase::Reading;
        now_ = now_ms;
        ReadAll();
        if(!queue_.empty())
            queue_.front().retries = identify_retries;
    }
    void Disconnect() {
        Reset();
        phase_ = Phase::Offline;
        Changed();
    }

    // ----- explicit edits (each one request, confirmed by the device) -----
    bool SetParam(std::size_t component, const std::string& key, double value, Done done = {}) {
        if(component >= components_.size())
            return false;
        const SqParam* p = components_[component].Find(key);
        if(!p || p->Readonly() || !std::isfinite(value))
            return false;
        // Into the descriptor's range before the cast (Codex F07): no overflow for any input.
        const double lo = p->min / 1000.0, hi = p->max / 1000.0;
        value = value < lo ? lo : (value > hi ? hi : value);
        const int32_t milli = int32_t(value * 1000.0 + (value < 0 ? -0.5 : 0.5));
        std::vector<uint8_t> w;
        U7(w, uint8_t(component));
        U14(w, p->id);
        I32(w, milli);
        Enqueue(sq::SetParam, w, [this, component, id = p->id, done](bool ok, uint8_t err, sq::Reader& r) {
            if(ok) {
                r.U7();
                r.U14();
                const int32_t v = r.I32();
                state_.accepted = r.U14();
                for(auto& q : components_[component].parameters)
                    if(q.id == id)
                        q.value = v;
            }
            Finish(done, ok, err);
        });
        return true;
    }
    void SetStep(int index, const step_seq::Step& s, Done done = {}) {
        std::vector<uint8_t> w;
        U7(w, uint8_t(index));
        Step(w, s);
        Enqueue(sq::SetStep, w, [this, index, s, done](bool ok, uint8_t err, sq::Reader& r) {
            if(ok) {
                r.U7();
                state_.accepted = r.U14();
                pattern_[std::size_t(index)] = s;
            }
            Finish(done, ok, err);
        });
    }
    void SetPattern(const step_seq::Pattern& p, Done done = {}) {
        std::vector<uint8_t> w;
        for(const auto& s : p)
            Step(w, s);
        Enqueue(sq::SetPattern, w, [this, p, done](bool ok, uint8_t err, sq::Reader& r) {
            if(ok) {
                r.U7();
                state_.accepted = r.U14();
                pattern_ = p;
            }
            Finish(done, ok, err);
        });
    }
    void StartStop(bool start, Done done = {}) {
        Enqueue(sq::TransportCmd, {uint8_t(start ? 1 : 0)}, [this, done](bool ok, uint8_t err, sq::Reader&) {
            if(!ok && log)
                log(LogKind::Warn, "Start/Stopp abgelehnt: " + SqErrorText(err) + " (SW17 auf Run?)");
            Finish(done, ok, err);
        });
    }
    void SetCal(int output, const SqCal& c, Done done = {}) {
        std::vector<uint8_t> w;
        Uid(w);
        U7(w, uint8_t(output));
        I32(w, c.offset);
        I32(w, c.scale);
        U14(w, c.gate_low);
        U14(w, c.gate_high);
        U14(w, c.clamp_min);
        U14(w, c.clamp_max);
        Enqueue(sq::SetCal, w, [this, output, c, done](bool ok, uint8_t err, sq::Reader&) {
            if(ok) {
                cal_[output & 1] = c;
                cal_[output & 1].valid = true;
                state_.cv_enabled = false; // new calibration withdraws the enable
            }
            Finish(done, ok, err);
        });
    }
    void CvEnable(bool on, Done done = {}) {
        std::vector<uint8_t> w;
        Uid(w);
        U7(w, on ? 1 : 0);
        Enqueue(sq::CvEnable, w, [this, on, done](bool ok, uint8_t err, sq::Reader&) {
            if(ok)
                state_.cv_enabled = on;
            Finish(done, ok, err);
        });
    }
    void ReadStats(Done done = {}) {
        Enqueue(sq::GetStats, {}, [this, done](bool ok, uint8_t err, sq::Reader& r) {
            if(ok) {
                stats_.clear();
                const uint8_t n = r.U7();
                for(uint8_t i = 0; i < n; ++i)
                    stats_.push_back(r.U32());
            }
            Finish(done, ok, err);
        });
    }
    // Re-reads everything from the device (button "Vom Gerät lesen").
    void Pull() { ReadAll(); }

    void OnMessage(const uint8_t* m, std::size_t n, double now_ms) {
        now_ = now_ms;
        if(ClassifySysEx(m, n) != Protocol::Sq)
            return;
        if(n < sq::kOverhead || !(m[5] & sq::kResponse) || m[n - 1] != 0xF7 || m[7] != n - sq::kOverhead) {
            ++ignored_;
            return;
        }
        const uint8_t crc = sq::Crc8(m + 4, n - 7);
        if(m[n - 3] != (crc >> 7) || m[n - 2] != (crc & 0x7F)) {
            ++ignored_;
            return;
        }
        sq::Reader r(m + 8, n - sq::kOverhead);
        if(m[5] == sq::kMonitorEvent) {
            ++monitor_frames_;
            Monitor(r);
            return;
        }
        if(queue_.empty() || !queue_.front().sent || m[6] != queue_.front().rid) {
            ++ignored_; // stale, duplicate or unexpected reply
            return;
        }
        Request req = std::move(queue_.front());
        queue_.pop_front();
        last_answer_ = now_ms;
        if(m[5] == sq::kNak) {
            r.U7();
            const uint8_t error = r.U7();
            last_error_ = error;
            req.handle(false, error, r);
        } else if(m[5] == (req.type | sq::kResponse)) {
            req.handle(true, 0, r);
        } else {
            req.handle(false, 0, r);
        }
        FinishReading();
        SendNext();
    }

    void Tick(double now_ms) {
        now_ = now_ms;
        if(!queue_.empty() && queue_.front().sent && now_ms - queue_.front().sent_at >= kTimeoutMs) {
            if(queue_.front().tries > queue_.front().retries) {
                Request req = std::move(queue_.front());
                queue_.pop_front();
                ++lost_;
                sq::Reader none(nullptr, 0);
                req.handle(false, 0, none);
                if(phase_ == Phase::Reading) {
                    phase_ = Phase::Failed;
                    queue_.clear();
                    if(log)
                        log(LogKind::Error, "SQ-Gerät antwortet nicht");
                    Changed();
                }
                SendNext();
            } else {
                Transmit(queue_.front());
            }
        }
        if(phase_ == Phase::Ready && now_ms - monitor_renewed_ >= kMonitorRenewMs) {
            monitor_renewed_ = now_ms;
            Enqueue(sq::Monitor, {1}, [](bool, uint8_t, sq::Reader&) {});
        }
    }

    Phase GetPhase() const { return phase_; }
    bool Ready() const { return phase_ == Phase::Ready; }
    bool Idle() const { return queue_.empty(); }
    const SqIdentity& Identity() const { return identity_; }
    const std::vector<SqComponent>& Components() const { return components_; }
    std::vector<SqComponent>& MutableComponents() { return components_; }
    int ComponentIndex(const std::string& id) const {
        for(std::size_t i = 0; i < components_.size(); ++i)
            if(components_[i].id == id)
                return int(i);
        return -1;
    }
    bool HasComponent(const std::string& id) const { return ComponentIndex(id) >= 0; }
    const step_seq::Pattern& Pattern() const { return pattern_; }
    const SqState& State() const { return state_; }
    const SqCal& Cal(int output) const { return cal_[output & 1]; }
    const std::vector<uint32_t>& Stats() const { return stats_; }
    unsigned Lost() const { return lost_; }
    unsigned Ignored() const { return ignored_; }
    unsigned MonitorFrames() const { return monitor_frames_; }
    uint8_t LastError() const { return last_error_; }
    // The device restarted since the last read (boot ID changed): re-read.
    bool Restarted() const { return restarted_; }

  private:
    struct Request {
        uint8_t type = 0, rid = 0;
        std::vector<uint8_t> frame;
        std::function<void(bool, uint8_t, sq::Reader&)> handle;
        bool sent = false;
        int tries = 0, retries = kRetries;
        double sent_at = 0;
    };
    static void U7(std::vector<uint8_t>& w, uint8_t v) { w.push_back(v & 0x7F); }
    static void U14(std::vector<uint8_t>& w, uint16_t v) {
        w.push_back(v & 0x7F);
        w.push_back(uint8_t((v >> 7) & 0x7F));
    }
    static void U32(std::vector<uint8_t>& w, uint32_t v) {
        for(int i = 0; i < 5; ++i)
            w.push_back(uint8_t((v >> (7 * i)) & 0x7F));
    }
    static void I32(std::vector<uint8_t>& w, int32_t v) { U32(w, uint32_t(v)); }
    static void Step(std::vector<uint8_t>& w, const step_seq::Step& s) {
        U7(w, s.note);
        U7(w, s.gate_percent);
        U7(w, s.Bits());
    }
    void Uid(std::vector<uint8_t>& w) const {
        for(uint32_t word : identity_.uid)
            U32(w, word);
    }
    static std::string Text(sq::Reader& r) {
        char buffer[40];
        r.Str(buffer, sizeof(buffer));
        return buffer;
    }
    static step_seq::Step ReadStep(sq::Reader& r) {
        step_seq::Step s;
        s.note = r.U7();
        s.gate_percent = r.U7();
        const uint8_t bits = r.U7();
        s.gate = bits & 1;
        s.accent = bits & 2;
        s.slide = bits & 4;
        return s;
    }
    void Reset() {
        queue_.clear();
        components_.clear();
        identity_ = SqIdentity{};
        state_ = SqState{};
        stats_.clear();
        restarted_ = false;
        monitor_started_ = false;
    }
    void Changed() {
        if(changed)
            changed();
    }
    void Finish(const Done& done, bool ok, uint8_t err) {
        if(!ok && err && log)
            log(LogKind::Warn, "Gerät lehnt ab: " + SqErrorText(err));
        if(done)
            done(ok, err);
        Changed();
    }
    void Enqueue(uint8_t type, const std::vector<uint8_t>& payload, std::function<void(bool, uint8_t, sq::Reader&)> h) {
        Request req;
        req.type = type;
        rid_ = uint8_t((rid_ + 1) & 0x7F);
        req.rid = rid_;
        uint8_t buffer[sq::kMaxFrame];
        sq::Writer w(buffer, sizeof(buffer));
        w.Begin(type, req.rid);
        for(uint8_t b : payload)
            w.U7(b);
        req.frame.assign(buffer, buffer + w.End());
        req.handle = std::move(h);
        queue_.push_back(std::move(req));
        if(queue_.size() == 1)
            SendNext();
    }
    void SendNext() {
        if(!queue_.empty() && !queue_.front().sent)
            Transmit(queue_.front());
    }
    void Transmit(Request& req) {
        req.sent = true;
        ++req.tries;
        req.sent_at = now_;
        transport_.Send(req.frame.data(), req.frame.size());
    }

    // The read sequence of Connect()/Pull(); each step queues the next.
    void ReadAll() {
        const uint32_t old_boot = identity_.boot_id;
        Enqueue(sq::Identify, {}, [this, old_boot](bool ok, uint8_t, sq::Reader& r) {
            if(!ok)
                return Fail("IDENTIFY");
            SqIdentity id;
            id.protocol_major = r.U7();
            id.protocol_minor = r.U7();
            id.product = Text(r);
            id.version = Text(r);
            id.boot_id = r.U32();
            for(auto& w : id.uid)
                w = r.U32();
            id.capabilities = r.U14();
            id.component_count = r.U7();
            if(!r.Ok() || id.protocol_major != sq::kVersion)
                return Fail("IDENTIFY (Protokollversion)");
            restarted_ = old_boot && old_boot != id.boot_id;
            identity_ = id;
            components_.assign(id.component_count, SqComponent{});
            for(uint8_t c = 0; c < id.component_count; ++c)
                ReadComponent(c);
            ReadPattern();
            ReadState();
            if(id.capabilities & 32) {
                ReadCal(0);
                ReadCal(1);
            }
            Enqueue(sq::Monitor, {1}, [this](bool, uint8_t, sq::Reader&) {
                monitor_started_ = true;
                monitor_renewed_ = now_;
            });
        });
    }
    // Ready only after the last read (Codex F05): descriptor, value and label requests are
    // queued behind the monitor request by the component replies.
    void FinishReading() {
        if(phase_ != Phase::Reading || !monitor_started_ || !queue_.empty())
            return;
        phase_ = Phase::Ready;
        if(log)
            log(LogKind::Ok, "Verbunden: " + identity_.product + " " + identity_.version + " (SQ)");
        Changed();
    }
    void Fail(const std::string& what) {
        phase_ = Phase::Failed;
        queue_.clear();
        if(log)
            log(LogKind::Error, "SQ: ungültige Antwort auf " + what);
        Changed();
    }
    void ReadComponent(uint8_t c) {
        Enqueue(sq::GetComponent, {c}, [this, c](bool ok, uint8_t, sq::Reader& r) {
            if(!ok)
                return;
            SqComponent& comp = components_[c];
            r.U7();
            r.U7();
            comp.id = Text(r);
            comp.kind = r.U7();
            comp.version = Text(r);
            comp.schema = r.U7();
            comp.params = r.U7();
            comp.parameters.assign(comp.params, SqParam{});
            for(uint8_t i = 0; i < comp.params; ++i)
                ReadDescriptor(c, i);
            ReadValues(c, 0);
        });
    }
    void ReadDescriptor(uint8_t c, uint8_t i) {
        Enqueue(sq::GetParamDesc, {c, i}, [this, c, i](bool ok, uint8_t, sq::Reader& r) {
            if(!ok)
                return;
            SqParam& p = components_[c].parameters[i];
            r.U7();
            r.U7();
            r.U7();
            p.id = r.U14();
            p.type = r.U7();
            p.unit = r.U7();
            p.flags = r.U7();
            p.min = r.I32();
            p.max = r.I32();
            p.def = r.I32();
            p.cc = r.U7();
            p.key = Text(r);
            if(p.type == 2) // enum: the keys of its values
                for(int v = 0; v <= p.max / 1000; ++v)
                    ReadEnumLabel(c, i, uint8_t(v));
        });
    }
    void ReadEnumLabel(uint8_t c, uint8_t i, uint8_t value) {
        const uint16_t id = components_[c].parameters[i].id;
        std::vector<uint8_t> w = {c};
        U14(w, id);
        U7(w, value);
        Enqueue(sq::GetEnumLabel, w, [this, c, i, value](bool ok, uint8_t, sq::Reader& r) {
            if(!ok)
                return;
            r.U7();
            r.U14();
            r.U7();
            auto& keys = components_[c].parameters[i].enum_keys;
            if(keys.size() <= value)
                keys.resize(value + 1u);
            keys[value] = Text(r);
        });
    }
    void ReadValues(uint8_t c, uint8_t first) {
        Enqueue(sq::GetParams, {c, first}, [this, c](bool ok, uint8_t, sq::Reader& r) {
            if(!ok)
                return;
            r.U7();
            const uint8_t start = r.U7(), total = r.U7(), n = r.U7();
            for(uint8_t k = 0; k < n; ++k) {
                const uint16_t id = r.U14();
                const int32_t v = r.I32();
                for(auto& p : components_[c].parameters)
                    if(p.id == id)
                        p.value = v;
            }
            if(start + n < total) // fragments: index and total in every reply
                ReadValues(c, uint8_t(start + n));
        });
    }
    void ReadPattern() {
        Enqueue(sq::GetPattern, {}, [this](bool ok, uint8_t, sq::Reader& r) {
            if(!ok)
                return;
            state_.accepted = r.U14();
            state_.applied = r.U14();
            for(auto& s : pattern_)
                s = ReadStep(r);
        });
    }
    void ReadState() {
        Enqueue(sq::GetState, {}, [this](bool ok, uint8_t, sq::Reader& r) {
            if(!ok)
                return;
            state_.state = r.U7();
            state_.mode = r.U7();
            state_.sw17 = r.U7();
            state_.sw18 = r.U7();
            state_.source = r.U7();
            const uint8_t step = r.U7();
            state_.step = step > 15 ? -1 : step;
            state_.bpm = r.I32() / 1000.0;
            state_.accepted = r.U14();
            state_.applied = r.U14();
            state_.cv_enabled = r.U7();
            state_.usb = r.U7();
        });
    }
    void ReadCal(uint8_t output) {
        Enqueue(sq::GetCal, {output}, [this, output](bool ok, uint8_t, sq::Reader& r) {
            if(!ok)
                return;
            r.U7();
            SqCal& c = cal_[output];
            c.valid = r.U7();
            c.offset = r.I32();
            c.scale = r.I32();
            c.gate_low = r.U14();
            c.gate_high = r.U14();
            c.clamp_min = r.U14();
            c.clamp_max = r.U14();
        });
    }
    void Monitor(sq::Reader& r) {
        switch(r.U7()) {
            case sq::EvStep: {
                const uint8_t step = r.U7();
                state_.step = step > 15 ? -1 : step;
                state_.gate = r.U7();
                break;
            }
            case sq::EvState:
                state_.state = r.U7();
                state_.mode = r.U7();
                state_.sw17 = r.U7();
                state_.sw18 = r.U7();
                state_.source = r.U7();
                break;
            case sq::EvTempo: state_.bpm = r.I32() / 1000.0; break;
            case sq::EvRevision:
                state_.accepted = r.U14();
                state_.applied = r.U14();
                break;
            case sq::EvParam: {
                const uint8_t c = r.U7();
                const uint16_t id = r.U14();
                const int32_t v = r.I32();
                if(c < components_.size())
                    for(auto& p : components_[c].parameters)
                        if(p.id == id)
                            p.value = v;
                break;
            }
            case sq::EvStepEdit: {
                const uint8_t i = r.U7();
                const step_seq::Step s = ReadStep(r);
                if(i < 16 && r.Ok())
                    pattern_[i] = s;
                break;
            }
            default: break;
        }
        Changed();
    }

    editor::Transport& transport_;
    Phase phase_ = Phase::Offline;
    bool monitor_started_ = false;
    std::deque<Request> queue_;
    uint8_t rid_ = 0, last_error_ = 0;
    double now_ = 0, last_answer_ = 0, monitor_renewed_ = 0;
    SqIdentity identity_;
    std::vector<SqComponent> components_;
    step_seq::Pattern pattern_{};
    SqState state_;
    SqCal cal_[2];
    std::vector<uint32_t> stats_;
    unsigned lost_ = 0, ignored_ = 0, monitor_frames_ = 0;
    bool restarted_ = false;
};

} // namespace editor
