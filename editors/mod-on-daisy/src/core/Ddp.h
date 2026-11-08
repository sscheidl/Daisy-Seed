#pragma once
// Daisy Diagnostic Protocol (DDP) v1 on the host side, the part the SD share needs:
// framing with CRC-16, the byte-wise receiver, key=value messages, HELLO, READER, NEB,
// LOOPER and the card flag of ST. Port of diagnostics/windows/daisy_diag/protocol.py
// (specification: diagnostics/protocol/PROTOCOL.md). Header-only, no JUCE.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::ddp {
constexpr int kProtocolVersion = 1;
constexpr std::size_t kMaxBody = 400;
constexpr int kUsbVid = 0x0483, kUsbPid = 0x5740;

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
inline uint16_t Crc16(const uint8_t* data, std::size_t n) {
    uint16_t crc = 0xFFFF;
    for(std::size_t i = 0; i < n; ++i) {
        crc = uint16_t(crc ^ (uint16_t(data[i]) << 8));
        for(int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

inline bool IsBodyChar(uint8_t b) { return b >= 0x20 && b <= 0x7E && b != 0x24 && b != 0x2A; } // '$', '*'

// "$" body "*" CRC as four upper-case hex digits "\n". false: the body is not a valid DDP body.
inline bool BuildFrame(const std::string& body, std::vector<uint8_t>& out) {
    if(body.empty() || body.size() > kMaxBody)
        return false;
    for(char c : body)
        if(!IsBodyChar(uint8_t(c)))
            return false;
    const uint16_t crc = Crc16(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    static const char hex[] = "0123456789ABCDEF";
    out.clear();
    out.reserve(body.size() + 7);
    out.push_back('$');
    out.insert(out.end(), body.begin(), body.end());
    out.push_back('*');
    out.push_back(uint8_t(hex[(crc >> 12) & 15]));
    out.push_back(uint8_t(hex[(crc >> 8) & 15]));
    out.push_back(uint8_t(hex[(crc >> 4) & 15]));
    out.push_back(uint8_t(hex[crc & 15]));
    out.push_back('\n');
    return true;
}

// Byte-wise receiver with resynchronisation on every '$' (PROTOCOL.md, receiver rules).
class FrameParser {
  public:
    int ok = 0, bad_crc = 0, bad_format = 0, overflow = 0;
    int Errors() const { return bad_crc + bad_format + overflow; }

    // Every complete, CRC-checked body in `data`.
    std::vector<std::string> Feed(const uint8_t* data, std::size_t n) {
        std::vector<std::string> bodies;
        for(std::size_t i = 0; i < n; ++i) {
            std::string body;
            if(FeedByte(data[i], body))
                bodies.push_back(std::move(body));
        }
        return bodies;
    }

  private:
    enum class State { Idle, Body, Crc, Cr } state_ = State::Idle;
    std::string body_, crc_;

    static bool IsHex(uint8_t b) {
        return (b >= '0' && b <= '9') || (b >= 'a' && b <= 'f') || (b >= 'A' && b <= 'F');
    }
    bool FeedByte(uint8_t byte, std::string& out) {
        if(byte == 0x24) { // '$'
            if(state_ != State::Idle)
                ++bad_format;
            state_ = State::Body;
            body_.clear();
            return false;
        }
        switch(state_) {
            case State::Idle: return false;
            case State::Body:
                if(byte == 0x2A) { // '*'
                    state_ = State::Crc;
                    crc_.clear();
                } else if(!IsBodyChar(byte)) {
                    ++bad_format;
                    state_ = State::Idle;
                } else if(body_.size() >= kMaxBody) {
                    ++overflow;
                    state_ = State::Idle;
                } else {
                    body_.push_back(char(byte));
                }
                return false;
            case State::Crc:
                if(crc_.size() < 4) {
                    if(IsHex(byte))
                        crc_.push_back(char(byte));
                    else {
                        ++bad_format;
                        state_ = State::Idle;
                    }
                    return false;
                }
                if(byte == 0x0D) {
                    state_ = State::Cr;
                    return false;
                }
                if(byte == 0x0A)
                    return Finish(out);
                ++bad_format;
                state_ = State::Idle;
                return false;
            case State::Cr:
                if(byte == 0x0A)
                    return Finish(out);
                ++bad_format;
                state_ = State::Idle;
                return false;
        }
        return false;
    }
    bool Finish(std::string& out) {
        state_ = State::Idle;
        if(body_.empty()) {
            ++bad_format;
            return false;
        }
        const unsigned crc = unsigned(std::stoul(crc_, nullptr, 16));
        if(crc != Crc16(reinterpret_cast<const uint8_t*>(body_.data()), body_.size())) {
            ++bad_crc;
            return false;
        }
        ++ok;
        out = body_;
        return true;
    }
};

struct Message {
    std::string type;
    std::vector<std::pair<std::string, std::string>> fields;
    bool duplicate_keys = false; // a key appeared twice: the ownership parsers reject such a message

    // The last occurrence wins, as the Python reference's dict does.
    const std::string* Get(const std::string& key) const {
        const std::string* found = nullptr;
        for(const auto& [k, v] : fields)
            if(k == key)
                found = &v;
        return found;
    }
    bool Has(const std::string& key) const { return Get(key) != nullptr; }
    // Strict decimal field: 1–10 digits only (the protocol has no sign, space or '_'),
    // within low..high. false: missing or invalid.
    bool Int(const std::string& key, long long& out, long long low = 0, long long high = 0xFFFFFFFFLL) const {
        const std::string* raw = Get(key);
        if(!raw || raw->empty() || raw->size() > 10)
            return false;
        for(char c : *raw)
            if(c < '0' || c > '9')
                return false;
        out = std::stoll(*raw);
        return out >= low && out <= high;
    }
};

inline Message ParseMessage(const std::string& body) {
    Message m;
    std::size_t i = 0;
    bool first = true;
    while(i < body.size()) {
        while(i < body.size() && body[i] == ' ')
            ++i;
        if(i >= body.size())
            break;
        const std::size_t start = i;
        while(i < body.size() && body[i] != ' ')
            ++i;
        const std::string token = body.substr(start, i - start);
        if(first) {
            m.type = token;
            first = false;
            continue;
        }
        const std::size_t eq = token.find('=');
        if(eq != std::string::npos) {
            const std::string key = token.substr(0, eq);
            if(m.Has(key))
                m.duplicate_keys = true;
            m.fields.emplace_back(key, token.substr(eq + 1));
        }
    }
    return m;
}

struct DeviceInfo {
    int protocol = 0;
    std::string firmware = "?", app = "?", device = "?", boot = "?", bootloader = "?", libdaisy = "?";
    std::vector<std::string> capabilities;
    bool simulated = false;
    bool Has(const std::string& capability) const {
        for(const std::string& c : capabilities)
            if(c == capability)
                return true;
        return false;
    }
};

inline bool ParseHello(const Message& m, DeviceInfo& out, std::string& error) {
    long long proto = 0;
    if(!m.Int("proto", proto)) {
        error = "HELLO: field 'proto' missing or invalid";
        return false;
    }
    out = DeviceInfo{};
    out.protocol = int(proto);
    auto field = [&](const char* key, std::string& into) {
        if(const std::string* v = m.Get(key))
            into = *v;
    };
    field("fw", out.firmware);
    field("app", out.app);
    field("dev", out.device);
    field("boot", out.boot);
    field("bl", out.bootloader);
    field("libdaisy", out.libdaisy);
    if(const std::string* caps = m.Get("caps")) {
        std::size_t start = 0;
        while(start <= caps->size()) {
            std::size_t comma = caps->find(',', start);
            if(comma == std::string::npos)
                comma = caps->size();
            if(comma > start)
                out.capabilities.push_back(caps->substr(start, comma - start));
            start = comma + 1;
        }
    }
    if(const std::string* sim = m.Get("sim"))
        out.simulated = *sim == "1";
    return true;
}

enum class ReaderState { Local, Starting, Shared, Ejected, Interrupted, Error };

inline const char* ReaderStateName(ReaderState s) {
    switch(s) {
        case ReaderState::Local: return "LOCAL";
        case ReaderState::Starting: return "STARTING";
        case ReaderState::Shared: return "SHARED";
        case ReaderState::Ejected: return "EJECTED";
        case ReaderState::Interrupted: return "INTERRUPTED";
        case ReaderState::Error: return "ERROR";
    }
    return "?";
}

inline bool ReaderStateFromName(const std::string& name, ReaderState& out) {
    static const ReaderState all[] = {ReaderState::Local, ReaderState::Starting, ReaderState::Shared,
                                      ReaderState::Ejected, ReaderState::Interrupted, ReaderState::Error};
    for(ReaderState s : all)
        if(name == ReaderStateName(s)) {
            out = s;
            return true;
        }
    return false;
}

struct ReaderInfo {
    ReaderState state = ReaderState::Local;
    uint32_t sectors = 0;
    bool busy = false;
    std::string action; // STATUS / SHARE / RETURN
    // Optional I/O diagnostics (all five present or none).
    bool has_diagnostics = false;
    std::string io;
    int fault = 0;
    uint32_t he = 0, lba = 0, ms = 0;
};

inline bool ParseReader(const Message& m, ReaderInfo& out, std::string& error) {
    out = ReaderInfo{};
    if(m.duplicate_keys) { // a contradictory ownership message is not interpreted at all
        error = "READER: duplicate field";
        return false;
    }
    const std::string* state = m.Get("state");
    if(!state || !ReaderStateFromName(*state, out.state)) {
        error = "READER: invalid state";
        return false;
    }
    const std::string* action = m.Get("a");
    if(!action || (*action != "STATUS" && *action != "SHARE" && *action != "RETURN")) {
        error = "READER: invalid or missing action";
        return false;
    }
    out.action = *action;
    long long sectors = 0, busy = 0;
    if(!m.Int("sectors", sectors) || !m.Int("busy", busy, 0, 1)) {
        error = "READER: invalid sectors or busy";
        return false;
    }
    out.sectors = uint32_t(sectors);
    out.busy = busy == 1;
    const bool present[] = {m.Has("io"), m.Has("fault"), m.Has("he"), m.Has("lba"), m.Has("ms")};
    bool any = false, all = true;
    for(bool p : present) {
        any = any || p;
        all = all && p;
    }
    if(any) {
        const std::string* io = m.Get("io");
        long long fault = 0, he = 0, lba = 0, ms = 0;
        if(!all || !io || (*io != "NONE" && *io != "READ" && *io != "WRITE" && *io != "SYNC")
           || !m.Int("fault", fault, 0, 7) || !m.Int("he", he) || !m.Int("lba", lba) || !m.Int("ms", ms)) {
            error = "READER: invalid or incomplete I/O diagnostics";
            return false;
        }
        out.has_diagnostics = true;
        out.io = *io;
        out.fault = int(fault);
        out.he = uint32_t(he);
        out.lba = uint32_t(lba);
        out.ms = uint32_t(ms);
    }
    return true;
}

// NEB (Nebulae) or LOOPER status line of the SD management personality.
struct MgmtInfo {
    std::string mode, firmware = "?";
    bool card = false, can_return = false;
    int bank = 0, slot = 0; // Nebulae only
    std::string action;     // STATUS / PERFORM
};

inline bool ParseMgmt(const Message& m, bool nebulae, MgmtInfo& out, std::string& error) {
    out = MgmtInfo{};
    if(m.duplicate_keys) {
        error = m.type + ": duplicate field";
        return false;
    }
    const std::string* mode = m.Get("mode");
    if(!mode || *mode != "SDMGMT") {
        error = m.type + ": unknown mode";
        return false;
    }
    const std::string* action = m.Get("a");
    if(!action || (*action != "STATUS" && *action != "PERFORM")) {
        error = m.type + ": invalid or missing action";
        return false;
    }
    long long card = 0, ret = 0, bank = 0, slot = 0;
    if(!m.Int("card", card, 0, 1) || !m.Int("ret", ret, 0, 1)) {
        error = m.type + ": invalid card or ret";
        return false;
    }
    if(nebulae && (!m.Int("bank", bank, 0, 16) || !m.Int("slot", slot, 0, 16))) {
        error = "NEB: invalid bank or slot";
        return false;
    }
    out.mode = *mode;
    if(const std::string* fw = m.Get("fw"))
        out.firmware = *fw;
    out.card = card == 1;
    out.can_return = ret == 1;
    out.bank = int(bank);
    out.slot = int(slot);
    out.action = *action;
    return true;
}

// The card-detect flag of an ST message; false if the field is missing or invalid.
inline bool ParseStSdCard(const Message& m, bool& sd) {
    long long v = 0;
    if(!m.Int("sd", v, 0, 1))
        return false;
    sd = v == 1;
    return true;
}

inline std::string CmdHello() { return "HELLO proto=" + std::to_string(kProtocolVersion); }
inline std::string CmdPing(uint32_t n) { return "PING n=" + std::to_string(n); }
}
