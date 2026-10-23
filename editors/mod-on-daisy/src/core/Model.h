#pragma once
// Editor state without any GUI or MIDI dependency: the patch, the columns the device
// reported and the editing rules (GUI.md § 2). Patch, validation and CRC come
// straight from the firmware headers, so editor and device can never disagree on them.
#include "mod_on_daisy/patch.h"
#include "mod_on_daisy/sources.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

namespace editor {
constexpr int kCells = int(mod::kCells);
constexpr int kNoCell = -1;
inline const char* const kSourceNames[mod::kSources] = {"LFO 1", "LFO 2", "Random", "CV 1",
                                                        "CV 2", "Modwheel", "Channel AT", "Poly AT"};
inline const char* const kShapeNames[mod::kShapeCount] = {"Sinus", "Dreieck", "Sägezahn ↑",
                                                          "Sägezahn ↓", "Rechteck", "S&H"};
inline const char* const kRandomModeNames[mod::kRandomModeCount] = {"Stufe", "Geglättet"};
inline const char* const kSyncNames[mod::kSyncCount] = {"Frei", "MIDI-Clock", "Gate"};

// One grid column as GET_TARGET describes it.
struct Column {
    bool present = false, per_voice = false, latched = false;
    float span = 0.0f;
    std::string name, span_text;

    // "+-2 st" as the firmware sends it (7-bit ASCII), "±2 st" for the screen.
    std::string SpanLabel() const {
        std::string s = span_text;
        if(s.rfind("+-", 0) == 0)
            s = "\xC2\xB1" + s.substr(2);
        return s;
    }
};

// IDENTIFY reply.
struct DeviceInfo {
    int protocol_major = 0, protocol_minor = 0;
    int module_major = 0, module_minor = 0, module_patch = 0;
    std::string key, version;
    int max_active = int(mod::kMaxActive), tick_hz = 0, slots = 0, caps = 0, device = 0;
    uint32_t schema = 0;
};

inline std::string Hex32(uint32_t v) {
    char text[9];
    std::snprintf(text, sizeof(text), "%08X", unsigned(v));
    return text;
}

inline int SourceOf(int cell) { return cell / int(mod::kTargets); }
inline int TargetOf(int cell) { return cell % int(mod::kTargets); }
inline uint64_t Bit(int cell) { return uint64_t{1} << cell; }
inline float RateToHz(unsigned rate) { return mod::RateToHz(uint16_t(rate)); }
inline unsigned HzToRate(float hz) {
    const float r = std::log(hz / 0.02f) / std::log(1000.0f) * float(mod::kRateMax);
    return unsigned(std::lround(r < 0 ? 0.0f : (r > float(mod::kRateMax) ? float(mod::kRateMax) : r)));
}
inline const char* DivisionName(unsigned sync, unsigned division) {
    if(sync == mod::GateClock)
        return division < mod::kGateDivisionCount ? mod::kGateDivisionNames[division] : "?";
    return division < mod::kMidiDivisionCount ? mod::kMidiDivisionNames[division] : "?";
}
inline unsigned DivisionCount(unsigned sync) {
    return sync == mod::GateClock ? mod::kGateDivisionCount : mod::kMidiDivisionCount;
}

class Model {
  public:
    mod::Patch patch{};
    std::array<Column, mod::kTargets> columns{};
    uint64_t allowed = ~uint64_t{0};
    int max_active = int(mod::kMaxActive);
    int selected = 0;

    Model() { Reset(); }

    // Offline start: eight generic columns, every cell allowed.
    void Reset() {
        patch = mod::Patch{};
        for(std::size_t t = 0; t < columns.size(); ++t)
            columns[t] = Column{true, false, false, 1.0f, "Ziel " + std::to_string(t + 1), ""};
        allowed = ~uint64_t{0};
        max_active = int(mod::kMaxActive);
        selected = 0;
    }

    int Depth(int cell) const { return patch.depth[SourceOf(cell)][TargetOf(cell)]; }
    bool Enabled(int cell) const { return patch.Enabled(std::size_t(SourceOf(cell)), std::size_t(TargetOf(cell))); }
    bool Active(int cell) const { return Enabled(cell) && Depth(cell) != 0; }
    int ActiveCount() const { return int(mod::PopCount(patch.ActiveMask())); }
    bool Allowed(int cell) const {
        return cell >= 0 && cell < kCells && (allowed & Bit(cell)) && columns[std::size_t(TargetOf(cell))].present;
    }
    uint32_t Crc() const { return mod::PatchCrc(patch); }

    std::string BlockedReason(int cell) const {
        const Column& c = columns[std::size_t(TargetOf(cell))];
        if(!c.present)
            return "Das Instrument hat hier kein Ziel.";
        if(Allowed(cell))
            return {};
        if(SourceOf(cell) == mod::PolyAt && c.latched)
            return "Poly AT wirkt hier nicht: der Wert wird beim Anschlag übernommen.";
        return "Diese Quelle ist im Instrument belegt.";
    }

    enum class Edit { None, Changed, Limited };

    // The browser editor's rules: a new connection switches itself on, but never as
    // the (max+1)th; an existing cell keeps its on/off state unless `enable` says otherwise.
    Edit SetDepth(int cell, int depth, std::optional<bool> enable = std::nullopt) {
        if(!Allowed(cell))
            return Edit::None;
        depth = depth < -mod::kMaxDepth ? -mod::kMaxDepth : (depth > mod::kMaxDepth ? mod::kMaxDepth : depth);
        const std::size_t s = std::size_t(SourceOf(cell)), t = std::size_t(TargetOf(cell));
        const bool was_active = Active(cell);
        const int old_depth = Depth(cell);
        const bool old_on = Enabled(cell);
        patch.depth[s][t] = int8_t(depth);
        bool on = old_on;
        if(enable)
            on = *enable;
        else if(depth != 0 && !on && !was_active)
            on = true;
        Edit result = Edit::Changed;
        const int others = ActiveCount() - (Active(cell) ? 1 : 0); // without this cell
        if(on && depth != 0 && !was_active && others >= max_active) {
            on = false;
            result = Edit::Limited;
        }
        patch.SetEnabled(s, t, on);
        if(result == Edit::Changed && depth == old_depth && on == old_on)
            return Edit::None;
        return result;
    }

    unsigned SourceParam(int src, int param) const {
        if(src == mod::Random) {
            const mod::RandomParams& r = patch.random;
            const unsigned values[] = {r.rate, r.mode, r.sync, r.gate, r.division, 0};
            return values[param];
        }
        const mod::LfoParams& l = patch.lfo[src];
        const unsigned values[] = {l.rate, l.shape, l.sync, l.gate, l.division, l.retrigger};
        return values[param];
    }

    // Same rule as the device: a change of sync source starts at that source's default division.
    void SetSourceParam(int src, int param, unsigned value) {
        uint16_t* rate;
        uint8_t *shape, *sync, *gate, *division, *retrigger = nullptr;
        if(src == mod::Random) {
            mod::RandomParams& r = patch.random;
            rate = &r.rate; shape = &r.mode; sync = &r.sync; gate = &r.gate; division = &r.division;
        } else {
            mod::LfoParams& l = patch.lfo[src];
            rate = &l.rate; shape = &l.shape; sync = &l.sync; gate = &l.gate; division = &l.division;
            retrigger = &l.retrigger;
        }
        switch(param) {
            case mod::ParamRate: *rate = uint16_t(value > mod::kRateMax ? mod::kRateMax : value); break;
            case mod::ParamShape: *shape = uint8_t(value); break;
            case mod::ParamSync:
                if(value != *sync)
                    *division = value == mod::GateClock ? mod::kDefaultGateDivision : mod::kDefaultMidiDivision;
                *sync = uint8_t(value);
                break;
            case mod::ParamGate: *gate = uint8_t(value); break;
            case mod::ParamDivision: *division = uint8_t(value); break;
            case mod::ParamRetrigger:
                if(retrigger)
                    *retrigger = uint8_t(value);
                break;
            default: break;
        }
    }

    std::string Name() const {
        std::string n(patch.name, sizeof(patch.name));
        return n.substr(0, n.find('\0'));
    }
    // 7-bit ASCII, at most 16 characters, as the device stores it.
    void SetName(const std::string& name) {
        std::fill(std::begin(patch.name), std::end(patch.name), '\0');
        std::size_t n = 0;
        for(char c : name)
            if(n < sizeof(patch.name) && static_cast<unsigned char>(c) < 128)
                patch.name[n++] = c;
    }

    void ClearMatrix() {
        for(auto& row : patch.depth)
            for(int8_t& d : row)
                d = 0;
        patch.SetEnabledMask(0);
    }
};
}
