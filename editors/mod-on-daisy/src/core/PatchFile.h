#pragma once
// .modpatch files (GUI.md § 2.5): the JSON format of the browser editor, so files move
// freely between both editors. A small JSON reader/writer keeps the core free of JUCE.
#include "Model.h"
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace editor {
namespace json {
struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    const Value* Get(const std::string& key) const {
        for(const auto& [k, v] : object)
            if(k == key)
                return &v;
        return nullptr;
    }
    // Numbers, booleans and null read as numbers, like JavaScript's `x | 0`.
    int Int(int fallback = 0) const {
        if(type == Number)
            return int(number);
        if(type == Bool)
            return boolean ? 1 : 0;
        return fallback;
    }
    bool Truthy() const {
        return (type == Bool && boolean) || (type == Number && std::abs(number) > 0.0) || (type == String && !string.empty())
            || type == Array || type == Object;
    }
};

class Parser {
  public:
    explicit Parser(const std::string& text) : s_(text) {}
    bool Parse(Value& out) {
        ok_ = true;
        out = ParseValue();
        Space();
        return ok_ && i_ == s_.size();
    }

  private:
    void Space() {
        while(i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t'))
            ++i_;
    }
    bool Eat(char c) {
        Space();
        if(i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }
    bool Word(const char* w) {
        const std::string word(w);
        if(s_.compare(i_, word.size(), word) == 0) {
            i_ += word.size();
            return true;
        }
        return false;
    }
    Value ParseValue() {
        Value v;
        if(++depth_ > 32) {
            ok_ = false;
            return v;
        }
        Space();
        if(i_ >= s_.size()) {
            ok_ = false;
        } else if(s_[i_] == '{') {
            ++i_;
            v.type = Value::Object;
            if(!Eat('}'))
                do {
                    Space();
                    std::string key = ParseString();
                    if(!Eat(':')) {
                        ok_ = false;
                        break;
                    }
                    v.object.emplace_back(std::move(key), ParseValue());
                } while(ok_ && Eat(','));
            if(ok_ && !v.object.empty() && !Eat('}'))
                ok_ = false;
        } else if(s_[i_] == '[') {
            ++i_;
            v.type = Value::Array;
            if(!Eat(']'))
                do
                    v.array.push_back(ParseValue());
                while(ok_ && Eat(','));
            if(ok_ && !v.array.empty() && !Eat(']'))
                ok_ = false;
        } else if(s_[i_] == '"') {
            v.type = Value::String;
            v.string = ParseString();
        } else if(Word("true")) {
            v.type = Value::Bool;
            v.boolean = true;
        } else if(Word("false")) {
            v.type = Value::Bool;
        } else if(Word("null")) {
        } else {
            const char* begin = s_.c_str() + i_;
            char* end = nullptr;
            v.number = std::strtod(begin, &end);
            if(end == begin)
                ok_ = false;
            v.type = Value::Number;
            i_ += std::size_t(end - begin);
        }
        --depth_;
        return v;
    }
    std::string ParseString() {
        std::string out;
        if(i_ >= s_.size() || s_[i_] != '"') {
            ok_ = false;
            return out;
        }
        ++i_;
        while(i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if(c != '\\') {
                out += c;
                continue;
            }
            if(i_ >= s_.size())
                break;
            c = s_[i_++];
            switch(c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    const unsigned cp = unsigned(std::strtoul(s_.substr(i_, 4).c_str(), nullptr, 16));
                    i_ += 4;
                    if(cp < 0x80) {
                        out += char(cp);
                    } else if(cp < 0x800) {
                        out += char(0xC0 | (cp >> 6));
                        out += char(0x80 | (cp & 63));
                    } else {
                        out += char(0xE0 | (cp >> 12));
                        out += char(0x80 | ((cp >> 6) & 63));
                        out += char(0x80 | (cp & 63));
                    }
                    break;
                }
                default: out += c; break; // \" \\ \/
            }
        }
        if(i_ >= s_.size())
            ok_ = false;
        else
            ++i_;
        return out;
    }
    const std::string& s_;
    std::size_t i_ = 0;
    int depth_ = 0;
    bool ok_ = true;
};

inline std::string Quote(const std::string& s) {
    std::string out = "\"";
    for(char c : s) {
        if(c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if(static_cast<unsigned char>(c) < 0x20) {
            char esc[8];
            std::snprintf(esc, sizeof(esc), "\\u%04x", unsigned(c));
            out += esc;
        } else {
            out += c;
        }
    }
    return out + "\"";
}
}

// Writes the browser editor's layout (JSON.stringify(doc, null, 2)).
inline std::string SavePatch(const Model& m, const DeviceInfo* device) {
    std::ostringstream o;
    auto list = [&](auto&& item, int n) {
        o << "[";
        for(int i = 0; i < n; ++i)
            o << (i ? ", " : "") << item(i);
        o << "]";
    };
    o << "{\n  \"format\": \"mod-on-daisy-patch\",\n  \"version\": 1,\n";
    o << "  \"instrument_key\": " << (device ? json::Quote(device->key) : "null") << ",\n";
    o << "  \"schema_hash\": " << (device ? json::Quote(Hex32(device->schema)) : "null") << ",\n";
    o << "  \"name\": " << json::Quote(m.Name()) << ",\n";
    o << "  \"columns\": ";
    list([&](int t) {
        const Column& c = m.columns[std::size_t(t)];
        return c.present ? json::Quote(c.name) : std::string("null");
    }, int(mod::kTargets));
    o << ",\n  \"depth\": [\n";
    for(int s = 0; s < int(mod::kSources); ++s) {
        o << "    ";
        list([&](int t) { return std::to_string(m.patch.depth[s][t]); }, int(mod::kTargets));
        o << (s + 1 < int(mod::kSources) ? ",\n" : "\n");
    }
    o << "  ],\n  \"enabled\": [\n";
    for(int s = 0; s < int(mod::kSources); ++s) {
        o << "    ";
        list([&](int t) { return std::string(m.patch.Enabled(std::size_t(s), std::size_t(t)) ? "true" : "false"); },
             int(mod::kTargets));
        o << (s + 1 < int(mod::kSources) ? ",\n" : "\n");
    }
    o << "  ],\n  \"lfo\": [\n";
    for(int i = 0; i < 2; ++i) {
        const mod::LfoParams& l = m.patch.lfo[i];
        o << "    {\"rate\": " << l.rate << ", \"shape\": " << int(l.shape) << ", \"sync\": " << int(l.sync)
          << ", \"gate\": " << int(l.gate) << ", \"division\": " << int(l.division)
          << ", \"retrigger\": " << int(l.retrigger) << "}" << (i ? "\n" : ",\n");
    }
    const mod::RandomParams& r = m.patch.random;
    o << "  ],\n  \"random\": {\"rate\": " << r.rate << ", \"mode\": " << int(r.mode) << ", \"sync\": "
      << int(r.sync) << ", \"gate\": " << int(r.gate) << ", \"division\": " << int(r.division) << "}\n}\n";
    return o.str();
}

struct LoadedPatch {
    bool ok = false;
    std::string error;          // why the file was refused
    std::string instrument_key; // empty: no key in the file
    mod::Patch patch{};
};

// Reads a file for the current columns: cells the instrument does not support are
// switched off, more than max_active connections or invalid source values refuse it.
inline LoadedPatch LoadPatch(const std::string& text, const Model& m) {
    LoadedPatch out;
    json::Value doc;
    if(!json::Parser(text).Parse(doc) || doc.type != json::Value::Object) {
        out.error = "Die Datei ist kein gültiges JSON.";
        return out;
    }
    const json::Value* format = doc.Get("format");
    const json::Value* version = doc.Get("version");
    if(!format || format->string != "mod-on-daisy-patch" || !version || version->Int() != 1) {
        out.error = "Keine Mod-on-Daisy-Patchdatei (Version 1).";
        return out;
    }
    if(const json::Value* key = doc.Get("instrument_key"); key && key->type == json::Value::String)
        out.instrument_key = key->string;
    // The grid and the LFOs must be there in full; the browser editor refuses them missing too.
    auto rows8x8 = [&](const char* name) {
        const json::Value* rows = doc.Get(name);
        if(!rows || rows->type != json::Value::Array || rows->array.size() != mod::kSources)
            return false;
        for(const json::Value& row : rows->array)
            if(row.type != json::Value::Array || row.array.size() != mod::kTargets)
                return false;
        return true;
    };
    const json::Value* lfo_list = doc.Get("lfo");
    if(!rows8x8("depth") || !rows8x8("enabled") || !lfo_list || lfo_list->type != json::Value::Array
       || lfo_list->array.size() != 2 || lfo_list->array[0].type != json::Value::Object
       || lfo_list->array[1].type != json::Value::Object) {
        out.error = "Die Datei ist unvollständig (Raster 8 × 8 und zwei LFOs erwartet).";
        return out;
    }
    mod::Patch& p = out.patch;
    auto grid = [&](const char* name, auto&& apply) {
        const json::Value& rows = *doc.Get(name);
        for(std::size_t s = 0; s < mod::kSources; ++s)
            for(std::size_t t = 0; t < mod::kTargets; ++t)
                apply(s, t, rows.array[s].array[t]);
    };
    grid("depth", [&](std::size_t s, std::size_t t, const json::Value& v) {
        const int d = v.Int();
        p.depth[s][t] = int8_t(d < -mod::kMaxDepth ? -mod::kMaxDepth : (d > mod::kMaxDepth ? mod::kMaxDepth : d));
    });
    grid("enabled", [&](std::size_t s, std::size_t t, const json::Value& v) { p.SetEnabled(s, t, v.Truthy()); });
    // Out-of-range values refuse the file instead of wrapping (256 must not become 0).
    bool in_range = true;
    auto num = [&in_range](const json::Value& obj, const char* key, unsigned fallback, unsigned max = 255) {
        const json::Value* v = obj.Get(key);
        if(!v)
            return fallback;
        const int i = v->Int(-1);
        if(i < 0 || unsigned(i) > max) {
            in_range = false;
            return fallback;
        }
        return unsigned(i);
    };
    if(const json::Value* lfo = doc.Get("lfo"))
        for(std::size_t i = 0; i < 2 && i < lfo->array.size(); ++i) {
            const json::Value& l = lfo->array[i];
            mod::LfoParams& d = p.lfo[i];
            d.rate = uint16_t(num(l, "rate", d.rate, mod::kRateMax));
            d.shape = uint8_t(num(l, "shape", d.shape));
            d.sync = uint8_t(num(l, "sync", d.sync));
            d.gate = uint8_t(num(l, "gate", d.gate));
            d.division = uint8_t(num(l, "division", d.division));
            d.retrigger = uint8_t(num(l, "retrigger", d.retrigger));
        }
    if(const json::Value* r = doc.Get("random")) {
        mod::RandomParams& d = p.random;
        d.rate = uint16_t(num(*r, "rate", d.rate, mod::kRateMax));
        d.mode = uint8_t(num(*r, "mode", d.mode));
        d.sync = uint8_t(num(*r, "sync", d.sync));
        d.gate = uint8_t(num(*r, "gate", d.gate));
        d.division = uint8_t(num(*r, "division", d.division));
    }
    Model tmp;
    if(const json::Value* name = doc.Get("name"))
        tmp.SetName(name->string);
    std::copy(std::begin(tmp.patch.name), std::end(tmp.patch.name), std::begin(p.name));
    for(int c = 0; c < kCells; ++c)
        if(p.Active(std::size_t(SourceOf(c)), std::size_t(TargetOf(c))) && !m.Allowed(c))
            p.SetEnabled(std::size_t(SourceOf(c)), std::size_t(TargetOf(c)), false);
    if(!in_range || ValidateSource(p.lfo[0]) != mod::Status::Ok || ValidateSource(p.lfo[1]) != mod::Status::Ok
       || ValidateSource(p.random) != mod::Status::Ok) {
        out.error = "Die Datei enthält ungültige Werte für LFO oder Random.";
        return out;
    }
    if(int(mod::PopCount(p.ActiveMask())) > m.max_active) {
        out.error = "Die Datei hat mehr als " + std::to_string(m.max_active) + " aktive Verbindungen.";
        return out;
    }
    out.ok = true;
    return out;
}
}
