#pragma once
// Product patch of Acid Seq (ARBEITSAUFTRAG 4.9, R16): one versioned JSON file (.acidpatch)
// with the pattern and every parameter of every component (sequencer incl. mode, transport,
// clock and MIDI settings; voice; drive; delay), each with its component and schema
// version. Calibration belongs to the kit and is not part of a song (stored per UID by
// sq_probe.py / the panel's calibration page). A patch loads without a device; missing or
// incompatible components are listed and skipped when it is sent. Sending happens only on
// an explicit action (D13). Parameters are stored by stable key, values in their units.
//
// The same structure serves every SQ product: Streichwurst writes "streichwurst-patch"
// (.swpatch) with its instrument, FX and product parameters and, optionally, the Mod on
// Daisy matrix as an embedded .modpatch object ("matrix"). Acid Seq files are unchanged.
#include "SqSession.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace editor {

constexpr int kAcidPatchFormat = 1;

struct AcidPatchComponent {
    std::string id, version;
    int schema = 0;
    std::map<std::string, double> params;
};

struct AcidPatch {
    std::string product;
    std::vector<AcidPatchComponent> components;
    bool has_pattern = false;
    step_seq::Pattern pattern{};
    std::string matrix; // embedded .modpatch JSON (Streichwurst), empty = none
};

// File format name and extension by product.
inline const char* PatchFormat(const std::string& product) {
    return product == "streichwurst" ? "streichwurst-patch" : "acid-seq-patch";
}
inline const char* PatchExtension(const std::string& product) {
    return product == "streichwurst" ? "swpatch" : "acidpatch";
}

// ---- a small JSON reader for this format (objects, arrays, strings, numbers, bools) ----
namespace acid_json {
struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::vector<std::pair<std::string, Value>> o;
    const Value* Get(const std::string& key) const {
        for(const auto& kv : o)
            if(kv.first == key)
                return &kv.second;
        return nullptr;
    }
};
class Parser {
  public:
    explicit Parser(const std::string& text) : t_(text) {}
    bool Parse(Value& v) {
        if(!ParseValue(v, 0))
            return false;
        Space();
        return p_ == t_.size();
    }

  private:
    void Space() {
        while(p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\n' || t_[p_] == '\r' || t_[p_] == '\t'))
            ++p_;
    }
    bool Lit(const char* word) {
        std::size_t n = 0;
        while(word[n])
            ++n;
        if(t_.compare(p_, n, word) != 0)
            return false;
        p_ += n;
        return true;
    }
    bool ParseString(std::string& out) {
        if(p_ >= t_.size() || t_[p_] != '"')
            return false;
        ++p_;
        while(p_ < t_.size() && t_[p_] != '"') {
            char c = t_[p_++];
            if(c == '\\' && p_ < t_.size()) {
                const char e = t_[p_++];
                c = e == 'n' ? '\n' : (e == 't' ? '\t' : e); // \" \\ \/ and the common ones
            }
            out.push_back(c);
        }
        if(p_ >= t_.size())
            return false;
        ++p_;
        return true;
    }
    bool ParseValue(Value& v, int depth) {
        if(depth > 16)
            return false;
        Space();
        if(p_ >= t_.size())
            return false;
        const char c = t_[p_];
        if(c == '{') {
            v.kind = Value::Kind::Object;
            ++p_;
            Space();
            if(p_ < t_.size() && t_[p_] == '}') {
                ++p_;
                return true;
            }
            while(true) {
                Space();
                std::string key;
                if(!ParseString(key))
                    return false;
                Space();
                if(p_ >= t_.size() || t_[p_++] != ':')
                    return false;
                Value child;
                if(!ParseValue(child, depth + 1))
                    return false;
                v.o.emplace_back(std::move(key), std::move(child));
                Space();
                if(p_ < t_.size() && t_[p_] == ',') {
                    ++p_;
                    continue;
                }
                if(p_ < t_.size() && t_[p_] == '}') {
                    ++p_;
                    return true;
                }
                return false;
            }
        }
        if(c == '[') {
            v.kind = Value::Kind::Array;
            ++p_;
            Space();
            if(p_ < t_.size() && t_[p_] == ']') {
                ++p_;
                return true;
            }
            while(true) {
                Value child;
                if(!ParseValue(child, depth + 1))
                    return false;
                v.a.push_back(std::move(child));
                Space();
                if(p_ < t_.size() && t_[p_] == ',') {
                    ++p_;
                    continue;
                }
                if(p_ < t_.size() && t_[p_] == ']') {
                    ++p_;
                    return true;
                }
                return false;
            }
        }
        if(c == '"') {
            v.kind = Value::Kind::String;
            return ParseString(v.s);
        }
        if(Lit("true")) {
            v.kind = Value::Kind::Bool;
            v.b = true;
            return true;
        }
        if(Lit("false")) {
            v.kind = Value::Kind::Bool;
            return true;
        }
        if(Lit("null"))
            return true;
        char* end = nullptr;
        v.n = std::strtod(t_.c_str() + p_, &end);
        if(end == t_.c_str() + p_)
            return false;
        v.kind = Value::Kind::Number;
        p_ = std::size_t(end - t_.c_str());
        return true;
    }
    const std::string& t_;
    std::size_t p_ = 0;
};
inline std::string Quote(const std::string& s) {
    std::string out = "\"";
    for(char c : s) {
        if(c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out + "\"";
}
// Writes a parsed value back as JSON (the embedded matrix goes to the .modpatch reader).
inline std::string Write(const Value& v) {
    switch(v.kind) {
        case Value::Kind::Null: return "null";
        case Value::Kind::Bool: return v.b ? "true" : "false";
        case Value::Kind::Number: {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.17g", v.n);
            return buffer;
        }
        case Value::Kind::String: return Quote(v.s);
        case Value::Kind::Array: {
            std::string out = "[";
            for(std::size_t i = 0; i < v.a.size(); ++i)
                out += (i ? "," : "") + Write(v.a[i]);
            return out + "]";
        }
        case Value::Kind::Object: {
            std::string out = "{";
            for(std::size_t i = 0; i < v.o.size(); ++i)
                out += (i ? "," : "") + Quote(v.o[i].first) + ":" + Write(v.o[i].second);
            return out + "}";
        }
    }
    return "null";
}
inline std::string Number(double v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", v);
    std::string s = buffer;
    while(s.size() > 1 && s.back() == '0')
        s.pop_back();
    if(s.back() == '.')
        s.pop_back();
    return s == "-0" ? "0" : s;
}
} // namespace acid_json

// The device state as a patch (what "Speichern" writes, and the VST3 song state).
inline AcidPatch PatchFromSession(const SqSession& s) {
    AcidPatch p;
    p.product = s.Identity().product;
    for(const auto& c : s.Components()) {
        AcidPatchComponent pc;
        pc.id = c.id;
        pc.version = c.version;
        pc.schema = c.schema;
        for(const auto& q : c.parameters)
            if(!q.Readonly())
                pc.params[q.key] = q.Value();
        p.components.push_back(pc);
    }
    p.has_pattern = s.HasComponent("step-seq");
    p.pattern = s.Pattern();
    return p;
}

inline std::string SerializeAcidPatch(const AcidPatch& p) {
    std::string out = "{\n  \"format\": \"" + std::string(PatchFormat(p.product)) +
                      "\",\n  \"format_version\": " + std::to_string(kAcidPatchFormat) +
                      ",\n  \"product\": " + acid_json::Quote(p.product) + ",\n  \"components\": [";
    for(std::size_t i = 0; i < p.components.size(); ++i) {
        const auto& c = p.components[i];
        out += std::string(i ? "," : "") + "\n    {\"id\": " + acid_json::Quote(c.id) + ", \"version\": " + acid_json::Quote(c.version) +
               ", \"schema\": " + std::to_string(c.schema) + ", \"params\": {";
        bool first = true;
        for(const auto& kv : c.params) {
            out += std::string(first ? "" : ", ") + acid_json::Quote(kv.first) + ": " + acid_json::Number(kv.second);
            first = false;
        }
        out += "}}";
    }
    out += "\n  ]";
    if(p.has_pattern) {
        out += ",\n  \"pattern\": [";
        for(std::size_t i = 0; i < p.pattern.size(); ++i) {
            const auto& s = p.pattern[i];
            out += std::string(i ? "," : "") + "\n    {\"note\": " + std::to_string(s.note) +
                   ", \"gate_length\": " + std::to_string(s.gate_percent) + ", \"gate\": " + (s.gate ? "true" : "false") +
                   ", \"accent\": " + (s.accent ? "true" : "false") + ", \"slide\": " + (s.slide ? "true" : "false") + "}";
        }
        out += "\n  ]";
    }
    if(!p.matrix.empty())
        out += ",\n  \"matrix\": " + p.matrix;
    return out + "\n}\n";
}

// A whole number within [lo, hi], checked before any cast (Codex F07: 1e999 or 3.5).
inline bool AcidInt(const acid_json::Value* v, double lo, double hi, int& out) {
    if(!v || v->kind != acid_json::Value::Kind::Number || !std::isfinite(v->n) || v->n != std::floor(v->n) ||
       v->n < lo || v->n > hi)
        return false;
    out = int(v->n);
    return true;
}

// nullopt with `error` set for anything that is not a valid patch.
inline std::optional<AcidPatch> ParseAcidPatch(const std::string& text, std::string& error) {
    acid_json::Value root;
    if(!acid_json::Parser(text).Parse(root) || root.kind != acid_json::Value::Kind::Object) {
        error = "not a valid JSON file";
        return std::nullopt;
    }
    const acid_json::Value* format = root.Get("format");
    const acid_json::Value* version = root.Get("format_version");
    int format_version = 0;
    if(!format || (format->s != "acid-seq-patch" && format->s != "streichwurst-patch") ||
       !AcidInt(version, 1, 1e6, format_version)) {
        error = "not an Acid Seq or Streichwurst patch";
        return std::nullopt;
    }
    if(format_version > kAcidPatchFormat) {
        error = "Patch format " + std::to_string(format_version) + " is newer than this editor";
        return std::nullopt;
    }
    AcidPatch p;
    if(const acid_json::Value* product = root.Get("product"))
        p.product = product->s;
    if(const acid_json::Value* comps = root.Get("components"))
        for(const auto& c : comps->a) {
            AcidPatchComponent pc;
            if(const acid_json::Value* id = c.Get("id"))
                pc.id = id->s;
            if(const acid_json::Value* v = c.Get("version"))
                pc.version = v->s;
            if(const acid_json::Value* s = c.Get("schema"))
                if(!AcidInt(s, 0, 127, pc.schema)) {
                    error = "invalid schema version";
                    return std::nullopt;
                }
            if(const acid_json::Value* params = c.Get("params"))
                for(const auto& kv : params->o) {
                    if(kv.second.kind == acid_json::Value::Kind::Bool) {
                        pc.params[kv.first] = kv.second.b ? 1 : 0;
                    } else if(kv.second.kind == acid_json::Value::Kind::Number) {
                        // Any finite value fits: the device's range clamps it when sent.
                        if(!std::isfinite(kv.second.n) || std::fabs(kv.second.n) > 2147483.0) {
                            error = "invalid value for " + kv.first;
                            return std::nullopt;
                        }
                        pc.params[kv.first] = kv.second.n;
                    }
                }
            if(pc.id.empty()) {
                error = "component without ID";
                return std::nullopt;
            }
            p.components.push_back(pc);
        }
    if(const acid_json::Value* pattern = root.Get("pattern")) {
        if(pattern->a.size() != step_seq::kSteps) {
            error = "pattern needs 16 steps";
            return std::nullopt;
        }
        for(std::size_t i = 0; i < pattern->a.size(); ++i) {
            const acid_json::Value& s = pattern->a[i];
            step_seq::Step& st = p.pattern[i];
            const acid_json::Value* note = s.Get("note");
            const acid_json::Value* length = s.Get("gate_length");
            int note_value = 0, length_value = 50;
            if((note && !AcidInt(note, 0, 127, note_value)) || (length && !AcidInt(length, 0, 100, length_value))) {
                error = "invalid step " + std::to_string(i + 1);
                return std::nullopt;
            }
            st.note = uint8_t(note_value);
            st.gate_percent = uint8_t(length_value);
            st.gate = s.Get("gate") && s.Get("gate")->b;
            st.accent = s.Get("accent") && s.Get("accent")->b;
            st.slide = s.Get("slide") && s.Get("slide")->b;
            if(!st.Valid()) {
                error = "invalid step " + std::to_string(i + 1);
                return std::nullopt;
            }
        }
        p.has_pattern = true;
    }
    if(const acid_json::Value* matrix = root.Get("matrix")) {
        if(matrix->kind != acid_json::Value::Kind::Object) {
            error = "invalid matrix";
            return std::nullopt;
        }
        p.matrix = acid_json::Write(*matrix);
    }
    if(format->s == "streichwurst-patch" && p.product != "streichwurst") {
        error = "Streichwurst patch for another product";
        return std::nullopt;
    }
    return p;
}

// What sending a patch to the connected device would do (shown before Upload).
struct AcidCompat {
    std::vector<std::string> missing;      // in the patch, not on the device: skipped
    std::vector<std::string> incompatible; // different schema version: skipped
    std::vector<std::string> unknown;      // component.key the device does not know: skipped
    bool pattern_skipped = false;          // pattern in the patch, no sequencer on the device
    bool Clean() const { return missing.empty() && incompatible.empty() && unknown.empty() && !pattern_skipped; }
};

inline AcidCompat CheckAcidPatch(const AcidPatch& p, const SqSession& s) {
    AcidCompat c;
    for(const auto& pc : p.components) {
        const int index = s.ComponentIndex(pc.id);
        if(index < 0) {
            c.missing.push_back(pc.id);
            continue;
        }
        const SqComponent& dev = s.Components()[std::size_t(index)];
        if(dev.schema != pc.schema) {
            c.incompatible.push_back(pc.id + " (Schema " + std::to_string(pc.schema) + " statt " + std::to_string(dev.schema) + ")");
            continue;
        }
        for(const auto& kv : pc.params) {
            const SqParam* q = dev.Find(kv.first);
            if(!q || q->Readonly())
                c.unknown.push_back(pc.id + "." + kv.first);
        }
    }
    c.pattern_skipped = p.has_pattern && !s.HasComponent("step-seq");
    return c;
}

// Explicit action only: sends every compatible value and the pattern. Returns the number
// of queued requests; the session confirms each one.
inline int UploadAcidPatch(const AcidPatch& p, SqSession& s, SqSession::Done done = {}) {
    int queued = 0;
    for(const auto& pc : p.components) {
        const int index = s.ComponentIndex(pc.id);
        if(index < 0 || s.Components()[std::size_t(index)].schema != pc.schema)
            continue;
        for(const auto& kv : pc.params)
            if(s.SetParam(std::size_t(index), kv.first, kv.second, done))
                ++queued;
    }
    if(p.has_pattern && s.HasComponent("step-seq")) {
        s.SetPattern(p.pattern, done);
        ++queued;
    }
    return queued;
}

} // namespace editor
