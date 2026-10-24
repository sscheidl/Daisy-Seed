#pragma once
// Conversation with one Mod on Daisy device (GUI.md § 2.4, PROTOCOL.md): one request
// open at a time, 250 ms timeout, three retries, edits coalesced per cell and per
// source parameter, every change confirmed by the device's patch CRC.
//
// Single-threaded: OnMessage(), Tick() and all edit calls run on the GUI thread. The
// transport only sends; received MIDI is handed to OnMessage() by the caller.
#include "Model.h"
#include "mod_on_daisy/protocol.h"
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace editor {
namespace proto = mod::proto;

class Transport {
  public:
    virtual ~Transport() = default;
    // One complete message F0 … F7. false: the port refused it.
    virtual bool Send(const uint8_t* bytes, std::size_t size) = 0;
};

enum class LogKind { Info, Ok, Warn, Error };
enum class LinkState { Offline, Connecting, Connected };
// What a connected session can still rely on (review finding F5).
enum class Health {
    Ok,         // replies and live data arrive
    NoLiveData, // no monitor frame for kStaleMs, replies may still work
    NoReplies,  // nothing from the device for kLostMs: changes stay unconfirmed
};

struct MonitorValues {
    float source[mod::kSources]{}, offset[mod::kTargets]{}, cv[2]{};
    // Column base values (pot/CC position before modulation), 0..1; -1 = not reported.
    // Only instruments with IDENTIFY capability bit 4 send them (PROTOCOL.md § 3.3).
    float base[mod::kTargets]{-1, -1, -1, -1, -1, -1, -1, -1};

    // Extended monitor (IDENTIFY capability bit 5, 89 bytes): the range each source swept
    // in the monitor window, clock activity and the last received aftertouch.
    bool has_range = false;
    float min[mod::kSources]{}, max[mod::kSources]{};
    uint8_t clocks = 0;   // bit 0 MIDI clock, bit 1 gate 1, bit 2 gate 2 running
    uint8_t at_kind = 0;  // 0 none, 1 channel, 2 poly
    uint8_t at_note = 0, at_value = 0, at_age = 127; // age in 20 ms, 127 = old

    bool HasBase(std::size_t t) const { return base[t] >= 0.0f; }
    // Where the column ends up: base plus the global offset, clamped like Engine::Apply.
    float Result(std::size_t t) const {
        const float v = base[t] + offset[t];
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
    // The range a source swept in the last monitor window; without the extended monitor
    // only the sampled value is known.
    float Low(int s) const { return has_range ? min[s] : source[s]; }
    float High(int s) const { return has_range ? max[s] : source[s]; }
    // Whether the clock a synced source follows is running: 1 yes, 0 no, -1 unknown.
    int ClockState(unsigned sync, unsigned gate) const {
        if(!has_range || sync == mod::Free)
            return -1;
        const uint8_t bit = sync == mod::MidiClock ? 1 : (gate == 0 ? 2 : 4);
        return (clocks & bit) ? 1 : 0;
    }
};

// The global goal of a column over the last monitor window (review F3/F4): interval
// arithmetic over the active connections with gain = depth/100 * span as in
// Program::Compile, around the base value and clamped like Engine::Apply. The window
// ranges of several sources need not coincide, so with more than one connection this is
// the reachable range, not a measured one. Poly AT on a per-voice column acts on each
// voice on top of this and is left out; `voice_part` says it exists.
struct GoalSpan {
    float lo = 0, hi = 0;          // base + offset range (0..1) or, without base, the offset range
    bool has_base = false;
    bool clipped_low = false, clipped_high = false;
    bool voice_part = false;
};
inline GoalSpan GoalSpanOf(const Model& model, const MonitorValues& m, std::size_t t) {
    GoalSpan g;
    const Column& col = model.columns[t];
    float lo = 0, hi = 0;
    for(int s = 0; s < int(mod::kSources); ++s) {
        if(!model.Active(s * int(mod::kTargets) + int(t)))
            continue;
        if(s == mod::PolyAt && col.per_voice) {
            g.voice_part = true;
            continue;
        }
        const float gain = float(model.Depth(s * int(mod::kTargets) + int(t))) / float(mod::kMaxDepth) * col.span;
        const float a = gain * m.Low(s), b = gain * m.High(s);
        lo += a < b ? a : b;
        hi += a < b ? b : a;
    }
    if(!m.has_range)
        lo = hi = m.offset[t]; // the engine's own sum; the sampled sources may be older
    g.has_base = m.HasBase(t);
    if(g.has_base) {
        lo += m.base[t];
        hi += m.base[t];
        g.clipped_low = lo < 0.0f;
        g.clipped_high = hi > 1.0f;
        lo = lo < 0.0f ? 0.0f : (lo > 1.0f ? 1.0f : lo);
        hi = hi < 0.0f ? 0.0f : (hi > 1.0f ? 1.0f : hi);
    }
    g.lo = lo;
    g.hi = hi;
    return g;
}

inline std::string ErrorText(unsigned code) {
    switch(code) {
        case proto::ErrChecksum: return "Prüfsumme";
        case proto::ErrLength: return "Länge";
        case proto::ErrUnknown: return "unbekanntes Kommando";
        case proto::ErrRange: return "Wert außerhalb des Bereichs";
        case proto::ErrBusy: return "beschäftigt";
        case proto::ErrVersion: return "Protokollversion";
        case proto::ErrLimit: return "zu viele aktive Verbindungen";
        case proto::ErrNotAllowed: return "Zelle nicht erlaubt";
        case proto::ErrNotImplemented: return "nicht implementiert";
        case proto::ErrStorage: return "Speicherfehler";
        case proto::ErrSchema: return "Schema";
        default: return "Fehler " + std::to_string(code);
    }
}

class Session {
  public:
    static constexpr uint32_t kTimeoutMs = 250, kEditGapMs = 20, kMonitorRenewMs = 1000;
    // No monitor frame for 1.5 s: live data stale. Nothing at all for 5 s: no replies.
    static constexpr uint32_t kStaleMs = 1500, kLostMs = 5000, kLogRepeatMs = 3000;
    static constexpr int kRetries = 3;
    static constexpr unsigned kMonitorRate = 20;

    // All optional. `changed` fires after anything the GUI shows has changed.
    std::function<void()> changed;
    std::function<void(LogKind, const std::string&)> log;
    std::function<void(const MonitorValues&)> monitor;

    explicit Session(Model& model, uint32_t seed = 1) : model_(model), seq_(uint8_t(seed & 127)) {}

    LinkState State() const { return state_; }
    bool Connected() const { return state_ == LinkState::Connected; }
    const DeviceInfo& Device() const { return device_info_; }
    Health LinkHealth() const { return Connected() ? health_ : Health::Ok; }
    // The device line plus what is degraded; never a plain success while nothing arrives.
    std::string StatusText() const {
        if(!Connected() || health_ == Health::Ok)
            return status_;
        return status_ + (health_ == Health::NoReplies ? " · keine Antwort" : " · keine Live-Daten");
    }
    bool Pending(int cell) const { return pending_cells_.count(cell) || in_flight_cell_ == cell; }
    // Nothing waiting, nothing open: what the tests wait for.
    bool Idle() const { return !current_ && queue_.empty() && !HasPendingEdits() && changes_in_flight_ == 0; }
    uint32_t Retries() const { return retries_; }

    // ------------------------------------------------------------------ connection
    void Connect(Transport& transport, uint32_t now_ms) {
        Disconnect("");
        transport_ = &transport;
        now_ = now_ms;
        state_ = LinkState::Connecting;
        device_ = proto::kBroadcast;
        SetStatus("verbinde …");
        Send(proto::Identify, {}, proto::kBroadcast, [this](const Reply& r) {
            proto::Reader rd(r.payload.data(), r.payload.size());
            DeviceInfo d;
            d.protocol_major = int(rd.U7());
            d.protocol_minor = int(rd.U7());
            d.module_major = int(rd.U7());
            d.module_minor = int(rd.U7());
            d.module_patch = int(rd.U7());
            d.key = ReadString(rd, mod::kMaxKeyLength);
            d.version = ReadString(rd, 12);
            d.max_active = int(rd.U7());
            d.tick_hz = int(rd.U14());
            d.slots = int(rd.U7());
            d.schema = rd.U32();
            d.caps = int(rd.U7());
            d.device = r.device;
            if(!rd.Ok() || d.protocol_major != proto::kVersion) {
                Fail("Protokollversion " + std::to_string(d.protocol_major) + "." + std::to_string(d.protocol_minor)
                     + " wird nicht unterstützt.");
                return;
            }
            device_ = uint8_t(d.device);
            device_info_ = d;
            model_.max_active = d.max_active;
            Pull(true, [this] {
                state_ = LinkState::Connected;
                health_ = Health::Ok;
                last_frame_ms_ = last_live_ms_ = now_; // grace until the first monitor frame
                SetStatus(device_info_.key + " " + device_info_.version + " · ID " + std::to_string(device_));
                last_monitor_ = now_ - kMonitorRenewMs; // start at once
                Log(LogKind::Ok, "Verbunden. Der Zustand des Geräts wurde geladen.");
            });
        }, [this](const std::string& message) { Fail(message); });
    }

    // Drops every open and queued request without running its handlers.
    void Disconnect(const std::string& reason) {
        ++generation_;
        current_.reset();
        queue_.clear();
        pending_cells_.clear();
        pending_sources_.clear();
        pending_name_ = false;
        in_flight_cell_ = kNoCell;
        edit_in_flight_ = false;
        changes_in_flight_ = 0;
        transport_ = nullptr;
        health_ = Health::Ok;
        unconfirmed_ = false;
        const bool was = state_ != LinkState::Offline;
        state_ = LinkState::Offline;
        if(was || !reason.empty())
            SetStatus(reason.empty() ? "offline" : reason);
    }

    // Everything the device sends, F0 … F7.
    void OnMessage(const uint8_t* bytes, std::size_t size) {
        if(!transport_ || size < 2 || bytes[0] != 0xF0 || bytes[size - 1] != 0xF7)
            return; // after Disconnect nothing reaches the handlers, not even monitor frames
        const uint8_t* body = bytes + 1;
        const std::size_t length = size - 2;
        proto::Frame f;
        if(proto::ParseHeader(body, length, f) != proto::Parse::Ok || proto::Verify(body, length, f) != 0)
            return; // damaged or foreign: the retry repairs a damaged reply
        if(device_ != proto::kBroadcast && f.device != device_)
            return; // another device on the same port
        last_frame_ms_ = now_;
        if(unconfirmed_ && Connected()) {
            unconfirmed_ = false;
            Pull();
        }
        if(f.command == proto::MonitorFrame) {
            last_live_ms_ = now_;
            UpdateHealth();
            if(monitor && Connected())
                monitor(DecodeMonitor(f.payload, f.length));
            return;
        }
        UpdateHealth();
        if(!current_ || f.seq != current_->seq)
            return;
        Reply reply{f.command, f.device, std::vector<uint8_t>(f.payload, f.payload + f.length)};
        if(f.command == proto::kNak) {
            const unsigned code = f.length > 1 ? f.payload[1] : 0;
            Finish(nullptr, "Gerät lehnt ab: " + ErrorText(code));
        } else if(f.command == (current_->command | proto::kReplyBit)) {
            Finish(&reply, {});
        }
    }

    // Timeouts, retries, coalesced edits and the monitor subscription.
    void Tick(uint32_t now_ms) {
        now_ = now_ms;
        if(current_ && now_ - current_->sent_ms >= kTimeoutMs) {
            if(current_->tries <= kRetries) {
                ++retries_;
                Transmit();
            } else {
                // The device may have applied it without its reply arriving (review F7):
                // once it is heard again, its state is reloaded.
                unconfirmed_ = Connected();
                Finish(nullptr, "keine Antwort vom Gerät – ist der MIDI-Eingang von einer anderen Anwendung belegt "
                                "(z. B. Studio One)?");
            }
        }
        FlushEdits();
        if(Connected() && now_ - last_monitor_ >= kMonitorRenewMs) {
            last_monitor_ = now_;
            // A lost renewal is not logged on its own: the health state reports silence.
            Send(proto::Monitor, {uint8_t(kMonitorRate)}, device_, nullptr, [](const std::string&) {});
        }
        UpdateHealth();
    }

    // ------------------------------------------------------------------ edits
    Model::Edit EditDepth(int cell, int depth, std::optional<bool> enable = std::nullopt) {
        const Model::Edit e = model_.SetDepth(cell, depth, enable);
        if(e == Model::Edit::Limited)
            Log(LogKind::Warn, "Höchstens " + std::to_string(model_.max_active)
                                   + " aktive Verbindungen – diese Zelle bleibt ausgeschaltet.");
        if(e != Model::Edit::None) {
            if(transport_)
                pending_cells_.insert(cell);
            FlushEdits();
            Changed();
        }
        return e;
    }

    void EditSource(int src, int param, unsigned value) {
        model_.SetSourceParam(src, param, value);
        if(transport_)
            pending_sources_.insert({src, param});
        FlushEdits();
        Changed();
    }

    void EditName(const std::string& name) {
        model_.SetName(name);
        if(transport_)
            pending_name_ = true;
        FlushEdits();
        Changed();
    }

    void ClearMatrix() {
        model_.ClearMatrix();
        pending_cells_.clear();
        Changed();
        if(transport_)
            Change(proto::ClearMatrix, {}, [this](const Reply& r) { ConfirmCrc(r); }, Resync());
    }

    // A whole patch, e.g. from a file: SET_MATRIX at once, then the source parameters
    // and the name. If the device refuses any step, the editor reloads what it has.
    void Upload(const mod::Patch& patch_in) {
        mod::Patch patch = patch_in;
        if(transport_) { // the device's format; unsupported cross modulation never reaches it
            patch.format = device_info_.LfoFm() ? 2 : 1;
            if(patch.format == 1)
                patch.lfo[0].fm = patch.lfo[1].fm = 0;
        }
        model_.patch = patch;
        pending_cells_.clear();
        pending_sources_.clear();
        pending_name_ = false;
        Changed();
        if(!transport_) {
            Log(LogKind::Ok, "Patch geladen (offline).");
            return;
        }
        auto job = std::make_shared<Job>();
        job->failed = [this](const std::string& message) {
            Pull(false, [this, message] {
                Log(LogKind::Error, "Datei nur teilweise übertragen (" + message + ") – Zustand neu vom Gerät geladen.");
            });
        };
        uint8_t depth[mod::kCells];
        for(int c = 0; c < kCells; ++c)
            depth[c] = uint8_t(model_.Depth(c));
        Payload w;
        w.Packed(depth, mod::kCells);
        w.Mask64(patch.EnabledMask());
        Change(proto::SetMatrix, w.Bytes(), nullptr, nullptr, job);
        for(int src = 0; src <= mod::Random; ++src)
            for(int param = 0; param < mod::kSourceParamCount; ++param) {
                if(src == mod::Random && param == mod::ParamRetrigger)
                    continue;
                Change(proto::SetSource, SourcePayload(src, param), nullptr, nullptr, job);
            }
        if(device_info_.LfoFm())
            for(int src = 0; src < mod::Random; ++src)
                Change(proto::SetSource, SourcePayload(src, mod::ParamFm), nullptr, nullptr, job);
        Change(proto::SetName, NamePayload(), [this](const Reply& r) {
            ConfirmCrc(r);
        }, nullptr, job);
    }

    // Read the device state; the columns only once per connection.
    void Pull(bool with_columns = false, std::function<void()> done = nullptr) {
        if(!transport_)
            return;
        pending_cells_.clear();
        pending_sources_.clear();
        pending_name_ = false;
        auto job = std::make_shared<Job>();
        job->failed = [this](const std::string& message) {
            if(state_ == LinkState::Connecting)
                Fail(message);
            else
                Log(LogKind::Error, message);
        };
        auto fetched = std::make_shared<Fetched>();
        fetched->columns = model_.columns;
        fetched->allowed = model_.allowed;
        fetched->patch.format = device_info_.LfoFm() ? 2 : 1; // the CRC covers the format
        if(with_columns) {
            for(int t = 0; t < int(mod::kTargets); ++t)
                Send(proto::GetTarget, {uint8_t(t)}, device_, [fetched, t](const Reply& r) {
                    proto::Reader rd(r.payload.data(), r.payload.size());
                    rd.U7();
                    const unsigned flags = rd.U7();
                    Column& c = fetched->columns[std::size_t(t)];
                    c.present = flags & 1;
                    c.per_voice = flags & 2;
                    c.latched = flags & 4;
                    c.span = float(rd.U14()) / 16383.0f;
                    c.name = ReadString(rd, 12);
                    c.span_text = ReadString(rd, 8);
                }, nullptr, job);
            Send(proto::GetAllowed, {}, device_, [fetched](const Reply& r) {
                proto::Reader rd(r.payload.data(), r.payload.size());
                fetched->allowed = rd.Mask64();
            }, nullptr, job);
        }
        Send(proto::GetMatrix, {}, device_, [fetched](const Reply& r) {
            proto::Reader rd(r.payload.data(), r.payload.size());
            uint8_t depth[mod::kCells];
            rd.Packed(depth, mod::kCells);
            for(int c = 0; c < kCells; ++c)
                fetched->patch.depth[SourceOf(c)][TargetOf(c)] = int8_t(depth[c]);
            fetched->patch.SetEnabledMask(rd.Mask64());
        }, nullptr, job);
        for(int src = 0; src <= mod::Random; ++src)
            Send(proto::GetSource, {uint8_t(src)}, device_, [fetched, src](const Reply& r) {
                proto::Reader rd(r.payload.data(), r.payload.size());
                rd.U7();
                const uint16_t rate = uint16_t(rd.U14());
                const uint8_t shape = uint8_t(rd.U7()), sync = uint8_t(rd.U7()), gate = uint8_t(rd.U7()),
                              division = uint8_t(rd.U7()), retrigger = uint8_t(rd.U7());
                if(src == mod::Random) {
                    fetched->patch.random = {rate, shape, sync, gate, division};
                } else {
                    mod::LfoParams& l = fetched->patch.lfo[src];
                    l.rate = rate; l.shape = shape; l.sync = sync; l.gate = gate; l.division = division;
                    l.retrigger = retrigger;
                    if(r.payload.size() >= 10) // LFO cross modulation appended (fm + 100)
                        l.fm = int8_t(int(rd.U14()) - mod::kMaxFm);
                }
            }, nullptr, job);
        Send(proto::GetName, {}, device_, [this, fetched, done](const Reply& r) {
            proto::Reader rd(r.payload.data(), r.payload.size());
            rd.Str(fetched->patch.name, sizeof(fetched->patch.name));
            model_.patch = fetched->patch;
            model_.columns = fetched->columns;
            model_.allowed = fetched->allowed;
            model_.origin = FileOrigin{}; // the device state replaces whatever a file said
            Changed();
            if(done)
                done();
        }, nullptr, job);
    }

  private:
    struct Reply {
        uint8_t command = 0, device = 0;
        std::vector<uint8_t> payload;
    };
    using OkHandler = std::function<void(const Reply&)>;
    using FailHandler = std::function<void(const std::string&)>;
    // Requests of one job run in order; after the first failure the rest are dropped.
    struct Job {
        bool dead = false;
        FailHandler failed;
    };
    struct Request {
        uint8_t command = 0, device = 0, seq = 0;
        std::vector<uint8_t> payload, frame;
        int tries = 0;
        uint32_t sent_ms = 0;
        bool change = false;
        OkHandler ok;
        FailHandler fail;
        std::shared_ptr<Job> job;
    };
    struct Fetched {
        mod::Patch patch{};
        std::array<Column, mod::kTargets> columns{};
        uint64_t allowed = 0;
    };
    class Payload {
      public:
        Payload() : w_(buffer_, sizeof(buffer_)) {}
        void U7(unsigned v) { w_.U7(v); }
        void U14(unsigned v) { w_.U14(v); }
        void Str(const char* s, std::size_t max) { w_.Str(s, max); }
        void Packed(const uint8_t* d, std::size_t n) { w_.Packed(d, n); }
        void Mask64(uint64_t m) { w_.Mask64(m); }
        std::vector<uint8_t> Bytes() const { return {buffer_, buffer_ + w_.Size()}; }
      private:
        uint8_t buffer_[proto::kMaxPayload]{};
        proto::Writer w_;
    };

    static std::string ReadString(proto::Reader& rd, std::size_t max) {
        char text[32]{};
        rd.Str(text, max < sizeof(text) - 1 ? max : sizeof(text) - 1);
        return text;
    }

    static MonitorValues DecodeMonitor(const uint8_t* payload, std::size_t size) {
        proto::Reader rd(payload, size);
        MonitorValues m;
        for(float& v : m.source)
            v = proto::DecodeSigned(rd.U14());
        for(float& v : m.offset)
            v = proto::DecodeSigned(rd.U14());
        for(float& v : m.cv)
            v = proto::DecodeSigned(rd.U14());
        if(size >= 52)
            for(float& v : m.base)
                v = proto::DecodeSigned(rd.U14());
        if(size >= 89) {
            m.has_range = true;
            for(float& v : m.min)
                v = proto::DecodeSigned(rd.U14());
            for(float& v : m.max)
                v = proto::DecodeSigned(rd.U14());
            m.clocks = uint8_t(rd.U7());
            m.at_kind = uint8_t(rd.U7());
            m.at_note = uint8_t(rd.U7());
            m.at_value = uint8_t(rd.U7());
            m.at_age = uint8_t(rd.U7());
        }
        return m;
    }

    std::vector<uint8_t> SourcePayload(int src, int param) const {
        Payload w;
        w.U7(unsigned(src));
        w.U7(unsigned(param));
        w.U14(model_.SourceParam(src, param));
        return w.Bytes();
    }
    std::vector<uint8_t> NamePayload() const {
        Payload w;
        w.Str(model_.patch.name, sizeof(model_.patch.name));
        return w.Bytes();
    }

    void Send(uint8_t command, std::vector<uint8_t> payload, uint8_t device, OkHandler ok, FailHandler fail,
              std::shared_ptr<Job> job = nullptr, bool change = false) {
        if(!transport_)
            return;
        auto r = std::make_unique<Request>();
        r->command = command;
        r->device = device;
        r->payload = std::move(payload);
        r->ok = std::move(ok);
        r->fail = std::move(fail);
        r->job = std::move(job);
        r->change = change;
        if(change)
            ++changes_in_flight_;
        queue_.push_back(std::move(r));
        Pump();
    }
    void Change(uint8_t command, std::vector<uint8_t> payload, OkHandler ok, FailHandler fail,
                std::shared_ptr<Job> job = nullptr) {
        Send(command, std::move(payload), device_, std::move(ok), std::move(fail), std::move(job), true);
    }

    void Pump() {
        while(!current_ && !queue_.empty()) {
            std::unique_ptr<Request> r = std::move(queue_.front());
            queue_.pop_front();
            if(r->job && r->job->dead) {
                if(r->change)
                    --changes_in_flight_;
                continue;
            }
            seq_ = uint8_t((seq_ + 1) & 127);
            r->seq = seq_;
            uint8_t frame[proto::kMaxFrame];
            const std::size_t n = proto::BuildFrame(frame, r->device, r->command, r->seq, r->payload.data(),
                                                    r->payload.size());
            r->frame.assign(frame, frame + n);
            current_ = std::move(r);
            Transmit();
        }
    }

    void Transmit() {
        ++current_->tries;
        current_->sent_ms = now_;
        if(!transport_->Send(current_->frame.data(), current_->frame.size()))
            Fail("Der MIDI-Ausgang verweigert das Senden – Verbindung getrennt.");
    }

    // Completes the open request; handlers run after the link state is consistent.
    void Finish(const Reply* reply, const std::string& error) {
        std::unique_ptr<Request> r = std::move(current_);
        if(!r)
            return;
        if(r->change)
            --changes_in_flight_;
        const uint64_t generation = generation_;
        if(reply) {
            if(r->ok)
                r->ok(*reply);
        } else if(r->job) {
            if(!r->job->dead) {
                r->job->dead = true;
                if(r->job->failed)
                    r->job->failed(error);
            }
        } else if(r->fail) {
            r->fail(error);
        } else {
            Log(LogKind::Error, error);
        }
        if(generation == generation_)
            Pump();
    }

    bool HasPendingEdits() const {
        return !pending_cells_.empty() || !pending_sources_.empty() || pending_name_ || edit_in_flight_;
    }

    // One edit at a time, at most every kEditGapMs: while dragging, the device gets the
    // latest value of a cell, not every intermediate one.
    void FlushEdits() {
        if(!transport_ || state_ != LinkState::Connected || edit_in_flight_ || now_ - last_edit_ < kEditGapMs)
            return;
        OkHandler ok = [this](const Reply& reply) {
            EditDone();
            ConfirmCrc(reply);
        };
        FailHandler fail = [this](const std::string& message) {
            EditDone();
            Log(LogKind::Error, message);
            Pull();
        };
        uint8_t command;
        std::vector<uint8_t> payload;
        int cell = kNoCell;
        if(!pending_cells_.empty()) {
            cell = *pending_cells_.begin();
            pending_cells_.erase(pending_cells_.begin());
            const int d = model_.Depth(cell);
            command = proto::SetCell;
            payload = {uint8_t(SourceOf(cell)), uint8_t(TargetOf(cell)), uint8_t(d < 0 ? -d : d),
                       uint8_t((d < 0 ? 1 : 0) | (model_.Enabled(cell) ? 2 : 0))};
        } else if(!pending_sources_.empty()) {
            const auto [src, param] = *pending_sources_.begin();
            pending_sources_.erase(pending_sources_.begin());
            command = proto::SetSource;
            payload = SourcePayload(src, param);
        } else if(pending_name_) {
            pending_name_ = false;
            command = proto::SetName;
            payload = NamePayload();
        } else {
            return;
        }
        edit_in_flight_ = true;
        in_flight_cell_ = cell;
        Change(command, std::move(payload), std::move(ok), std::move(fail));
        Changed();
    }
    void EditDone() {
        edit_in_flight_ = false;
        in_flight_cell_ = kNoCell;
        last_edit_ = now_;
        Changed();
    }

    // The reply to every change carries the device's patch CRC.
    void ConfirmCrc(const Reply& r) {
        proto::Reader rd(r.payload.data(), r.payload.size());
        rd.U7();
        rd.U7();
        const uint32_t crc = rd.U32();
        if(HasPendingEdits() || changes_in_flight_ > 0)
            return; // more is on its way
        if(crc != model_.Crc()) {
            Log(LogKind::Warn, "Editor und Gerät waren nicht gleich – Zustand neu vom Gerät geladen.");
            Pull();
            return;
        }
        Log(LogKind::Ok, "Gerät bestätigt (Patch-CRC " + Hex32(crc) + ").");
    }

    FailHandler Resync() {
        return [this](const std::string& message) {
            Log(LogKind::Error, message);
            Pull();
        };
    }

    void Fail(const std::string& message) {
        Disconnect("Fehler");
        Log(LogKind::Error, message);
    }
    void SetStatus(const std::string& text) {
        status_ = text;
        Changed();
    }
    // The same message within kLogRepeatMs is shown once; the first one always.
    void Log(LogKind kind, const std::string& text) {
        if(text == last_log_ && now_ - last_log_ms_ < kLogRepeatMs)
            return;
        last_log_ = text;
        last_log_ms_ = now_;
        if(log)
            log(kind, text);
    }

    void UpdateHealth() {
        if(!Connected())
            return;
        const Health next = now_ - last_frame_ms_ >= kLostMs ? Health::NoReplies
            : now_ - last_live_ms_ >= kStaleMs                ? Health::NoLiveData
                                                               : Health::Ok;
        if(next == health_)
            return;
        const Health before = health_;
        health_ = next;
        if(next == Health::NoReplies) {
            unconfirmed_ = true;
            Log(LogKind::Error, "Keine Antwort vom Gerät seit 5 s – Port von einer anderen Anwendung belegt (z. B. "
                                "Studio One) oder Kabel getrennt? Änderungen bleiben unbestätigt.");
        } else if(before == Health::NoReplies) {
            Log(LogKind::Warn, "Das Gerät antwortet wieder – Zustand wird neu geladen.");
        }
        Changed();
    }
    void Changed() {
        if(changed)
            changed();
    }

    Model& model_;
    Transport* transport_ = nullptr;
    LinkState state_ = LinkState::Offline;
    DeviceInfo device_info_;
    std::string status_ = "offline";
    uint8_t device_ = proto::kBroadcast, seq_ = 0;
    uint32_t now_ = 0, last_edit_ = 0, last_monitor_ = 0, retries_ = 0;
    uint32_t last_frame_ms_ = 0, last_live_ms_ = 0, last_log_ms_ = 0;
    Health health_ = Health::Ok;
    bool unconfirmed_ = false; // a change timed out: reload when the device is heard again
    std::string last_log_;
    uint64_t generation_ = 0;
    std::unique_ptr<Request> current_;
    std::deque<std::unique_ptr<Request>> queue_;
    std::set<int> pending_cells_;
    std::set<std::pair<int, int>> pending_sources_;
    bool pending_name_ = false, edit_in_flight_ = false;
    int in_flight_cell_ = kNoCell, changes_in_flight_ = 0;
};
}
