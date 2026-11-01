#include "pm_catalog.h"
#include "pm_image.h"
#include <cstring>

namespace pm {
namespace {

// Root object (1), programs array (2), entry object (3) and up to three levels of
// unknown nested values below an entry; anything deeper is rejected.
constexpr int kMaxDepth = 6;

const char* const kCategories[] = {"instrument", "effect", "sequencer", "other"};

bool IsPathChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'
           || c == '_' || c == '-' || c == '/';
}

// Recursive-descent reader over a bounded text. Every method returns false on a
// syntax error and leaves `error` set. Nothing is allocated; strings are copied
// into caller buffers with a hard capacity.
class Reader {
  public:
    Reader(const char* text, size_t len) : text_(text), len_(len) {}

    size_t Offset() const { return pos_; }
    CatalogError error = CatalogError::None;

    void SkipBom() {
        if(len_ >= 3 && static_cast<uint8_t>(text_[0]) == 0xEF
           && static_cast<uint8_t>(text_[1]) == 0xBB && static_cast<uint8_t>(text_[2]) == 0xBF)
            pos_ = 3;
    }

    void SkipSpace() {
        while(pos_ < len_
              && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool AtEnd() {
        SkipSpace();
        return pos_ >= len_;
    }

    int Peek() {
        SkipSpace();
        return pos_ < len_ ? static_cast<unsigned char>(text_[pos_]) : -1;
    }

    bool Expect(char c) {
        if(Peek() != c)
            return Fail(CatalogError::Syntax);
        ++pos_;
        return true;
    }

    bool Fail(CatalogError e) {
        if(error == CatalogError::None)
            error = e;
        return false;
    }

    // Reads a JSON string into out[cap] (NUL terminated). Escapes \" \\ \/ \b \f \n
    // \r \t and \uXXXX (BMP, encoded as UTF-8) are accepted; raw control characters
    // are not. Too long -> `too_long` error.
    bool String(char* out, size_t cap, CatalogError too_long) {
        if(!Expect('"'))
            return false;
        size_t n = 0;
        while(true) {
            if(pos_ >= len_)
                return Fail(CatalogError::Syntax);
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if(c == '"')
                break;
            if(c < 0x20)
                return Fail(CatalogError::Syntax);
            char buf[4];
            size_t count = 1;
            buf[0] = static_cast<char>(c);
            if(c == '\\') {
                if(pos_ >= len_)
                    return Fail(CatalogError::Syntax);
                const char e = text_[pos_++];
                switch(e) {
                    case '"': buf[0] = '"'; break;
                    case '\\': buf[0] = '\\'; break;
                    case '/': buf[0] = '/'; break;
                    case 'b': buf[0] = '\b'; break;
                    case 'f': buf[0] = '\f'; break;
                    case 'n': buf[0] = '\n'; break;
                    case 'r': buf[0] = '\r'; break;
                    case 't': buf[0] = '\t'; break;
                    case 'u': {
                        uint32_t code = 0;
                        for(int i = 0; i < 4; ++i) {
                            if(pos_ >= len_)
                                return Fail(CatalogError::Syntax);
                            const int d = HexDigit(text_[pos_++]);
                            if(d < 0)
                                return Fail(CatalogError::Syntax);
                            code = (code << 4) | static_cast<uint32_t>(d);
                        }
                        if(code >= 0xD800 && code <= 0xDFFF)
                            return Fail(CatalogError::Syntax);  // surrogates unsupported
                        count = Utf8Encode(code, buf);
                        break;
                    }
                    default: return Fail(CatalogError::Syntax);
                }
            }
            if(n + count > cap - 1)
                return Fail(too_long);
            std::memcpy(out + n, buf, count);
            n += count;
        }
        out[n] = 0;
        return true;
    }

    // Unsigned 32-bit integer: digits only, no sign, fraction, exponent or leading zero.
    bool U32(uint32_t& value, CatalogError bad) {
        SkipSpace();
        size_t start = pos_;
        uint64_t v = 0;
        while(pos_ < len_ && text_[pos_] >= '0' && text_[pos_] <= '9') {
            v = v * 10 + static_cast<uint64_t>(text_[pos_] - '0');
            if(v > 0xFFFFFFFFull)
                return Fail(bad);
            ++pos_;
        }
        if(pos_ == start)
            return Fail(bad);
        if(pos_ - start > 1 && text_[start] == '0')
            return Fail(bad);
        if(pos_ < len_ && (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E'))
            return Fail(bad);
        value = static_cast<uint32_t>(v);
        return true;
    }

    // Skips any value (for keys this parser does not know).
    bool SkipValue(int depth) {
        if(depth > kMaxDepth)
            return Fail(CatalogError::Depth);
        const int c = Peek();
        if(c == '"')  // a long unknown string is still fine: skip without storing
            return SkipString();
        if(c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++pos_;
            if(Peek() == close) {
                ++pos_;
                return true;
            }
            while(true) {
                if(c == '{') {
                    if(!SkipString())
                        return false;
                    if(!Expect(':'))
                        return false;
                }
                if(!SkipValue(depth + 1))
                    return false;
                const int sep = Peek();
                if(sep == ',') {
                    ++pos_;
                    continue;
                }
                if(sep == close) {
                    ++pos_;
                    return true;
                }
                return Fail(CatalogError::Syntax);
            }
        }
        if(c == 't') return Literal("true");
        if(c == 'f') return Literal("false");
        if(c == 'n') return Literal("null");
        if(c == '-' || (c >= '0' && c <= '9')) {
            // Any JSON number; its value is irrelevant.
            ++pos_;
            while(pos_ < len_
                  && ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.' || text_[pos_] == 'e'
                      || text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-'))
                ++pos_;
            return true;
        }
        return Fail(CatalogError::Syntax);
    }

    bool SkipString() {
        if(!Expect('"'))
            return false;
        while(true) {
            if(pos_ >= len_)
                return Fail(CatalogError::Syntax);
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if(c == '"')
                return true;
            if(c < 0x20)
                return Fail(CatalogError::Syntax);
            if(c == '\\') {
                if(pos_ >= len_)
                    return Fail(CatalogError::Syntax);
                const char e = text_[pos_++];
                if(e == 'u') {
                    for(int i = 0; i < 4; ++i) {
                        if(pos_ >= len_ || HexDigit(text_[pos_++]) < 0)
                            return Fail(CatalogError::Syntax);
                    }
                } else if(!std::strchr("\"\\/bfnrt", e)) {
                    return Fail(CatalogError::Syntax);
                }
            }
        }
    }

    bool Literal(const char* word) {
        SkipSpace();
        const size_t n = std::strlen(word);
        if(pos_ + n > len_ || std::memcmp(text_ + pos_, word, n) != 0)
            return Fail(CatalogError::Syntax);
        pos_ += n;
        return true;
    }

    static int HexDigit(char c) {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    static size_t Utf8Encode(uint32_t code, char out[4]) {
        if(code < 0x80) {
            out[0] = static_cast<char>(code);
            return 1;
        }
        if(code < 0x800) {
            out[0] = static_cast<char>(0xC0 | (code >> 6));
            out[1] = static_cast<char>(0x80 | (code & 0x3F));
            return 2;
        }
        out[0] = static_cast<char>(0xE0 | (code >> 12));
        out[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (code & 0x3F));
        return 3;
    }

  private:
    const char* text_;
    size_t len_;
    size_t pos_ = 0;
};

struct EntryFlags {
    bool slot = false, id = false, name = false, category = false, version = false, path = false,
         hash = false, profile = false, file_hash = false;
};

bool ParseEntry(Reader& r, CatalogEntry& e) {
    if(!r.Expect('{'))
        return false;
    EntryFlags seen;
    char key[24];
    bool first = true;
    while(true) {
        if(!first) {
            const int sep = r.Peek();
            if(sep == '}') {
                r.Expect('}');
                break;
            }
            if(!r.Expect(','))
                return false;
        } else if(r.Peek() == '}') {
            r.Expect('}');
            break;
        }
        first = false;
        if(!r.String(key, sizeof(key), CatalogError::Syntax))
            return false;
        if(!r.Expect(':'))
            return false;
        if(std::strcmp(key, "slot") == 0) {
            if(seen.slot) return r.Fail(CatalogError::Duplicate);
            seen.slot = true;
            uint32_t v;
            if(!r.U32(v, CatalogError::Slot)) return false;
            if(v < 1 || v > kCatalogMaxPrograms) return r.Fail(CatalogError::Slot);
            e.slot = static_cast<uint8_t>(v);
        } else if(std::strcmp(key, "id") == 0) {
            if(seen.id) return r.Fail(CatalogError::Duplicate);
            seen.id = true;
            if(!r.String(e.id, sizeof(e.id), CatalogError::Id)) return false;
            if(e.id[0] == 0) return r.Fail(CatalogError::Id);
            for(const char* p = e.id; *p; ++p)
                if(!IsProductIdChar(*p)) return r.Fail(CatalogError::Id);
        } else if(std::strcmp(key, "name") == 0) {
            if(seen.name) return r.Fail(CatalogError::Duplicate);
            seen.name = true;
            if(!r.String(e.name, sizeof(e.name), CatalogError::Name)) return false;
            if(!ValidName(e.name)) return r.Fail(CatalogError::Name);
        } else if(std::strcmp(key, "category") == 0) {
            if(seen.category) return r.Fail(CatalogError::Duplicate);
            seen.category = true;
            if(!r.String(e.category, sizeof(e.category), CatalogError::Category)) return false;
            if(!ValidCategory(e.category)) return r.Fail(CatalogError::Category);
        } else if(std::strcmp(key, "version") == 0) {
            if(seen.version) return r.Fail(CatalogError::Duplicate);
            seen.version = true;
            if(!r.String(e.version, sizeof(e.version), CatalogError::Version)) return false;
            if(e.version[0] == 0) return r.Fail(CatalogError::Version);
            for(const char* p = e.version; *p; ++p)
                if(!IsVersionChar(*p)) return r.Fail(CatalogError::Version);
        } else if(std::strcmp(key, "path") == 0) {
            if(seen.path) return r.Fail(CatalogError::Duplicate);
            seen.path = true;
            if(!r.String(e.path, sizeof(e.path), CatalogError::Path)) return false;
            if(!ValidPath(e.path)) return r.Fail(CatalogError::Path);
        } else if(std::strcmp(key, "payload_sha256") == 0) {
            if(seen.hash) return r.Fail(CatalogError::Duplicate);
            seen.hash = true;
            char hex[65];
            if(!r.String(hex, sizeof(hex), CatalogError::Hash)) return false;
            if(!ParseHex256(hex, e.payload_sha256)) return r.Fail(CatalogError::Hash);
        } else if(std::strcmp(key, "profile") == 0) {
            if(seen.profile) return r.Fail(CatalogError::Duplicate);
            seen.profile = true;
            char profile[kProfileField + 1];
            if(!r.String(profile, sizeof(profile), CatalogError::Profile)) return false;
            if(std::strcmp(profile, kProfileId) != 0) return r.Fail(CatalogError::Profile);
        } else if(std::strcmp(key, "file_sha256") == 0) {
            // Hash of the whole .dsy file, for host tools; validated for form only.
            if(seen.file_hash) return r.Fail(CatalogError::Duplicate);
            seen.file_hash = true;
            char hex[65];
            uint8_t scratch[32];
            if(!r.String(hex, sizeof(hex), CatalogError::Hash)) return false;
            if(!ParseHex256(hex, scratch)) return r.Fail(CatalogError::Hash);
        } else {
            if(!r.SkipValue(3))
                return false;
        }
    }
    if(!seen.slot || !seen.id || !seen.name || !seen.version || !seen.path || !seen.hash)
        return r.Fail(CatalogError::Missing);
    if(!seen.category)
        std::strcpy(e.category, "other");
    return true;
}

} // namespace

const CatalogEntry* Catalog::FindSlot(uint8_t slot) const {
    for(uint8_t i = 0; i < count; ++i)
        if(entries[i].slot == slot)
            return &entries[i];
    return nullptr;
}

const CatalogEntry* Catalog::FindId(const char* id) const {
    if(id == nullptr)
        return nullptr;
    for(uint8_t i = 0; i < count; ++i)
        if(std::strcmp(entries[i].id, id) == 0)
            return &entries[i];
    return nullptr;
}

const char* CatalogErrorName(CatalogError error) {
    switch(error) {
        case CatalogError::None: return "OK";
        case CatalogError::TooLarge: return "TOOLARGE";
        case CatalogError::Syntax: return "SYNTAX";
        case CatalogError::Depth: return "DEPTH";
        case CatalogError::Duplicate: return "DUPKEY";
        case CatalogError::Missing: return "MISSING";
        case CatalogError::Format: return "FORMAT";
        case CatalogError::Generation: return "GENERATION";
        case CatalogError::Hardware: return "HARDWARE";
        case CatalogError::Programs: return "PROGRAMS";
        case CatalogError::TooMany: return "TOOMANY";
        case CatalogError::Slot: return "SLOT";
        case CatalogError::DuplicateSlot: return "DUPSLOT";
        case CatalogError::Id: return "ID";
        case CatalogError::DuplicateId: return "DUPID";
        case CatalogError::Name: return "NAME";
        case CatalogError::Category: return "CATEGORY";
        case CatalogError::Version: return "VERSION";
        case CatalogError::Path: return "PATH";
        case CatalogError::Hash: return "HASH";
        case CatalogError::Profile: return "PROFILE";
    }
    return "?";
}

bool ValidPath(const char* path) {
    if(path == nullptr)
        return false;
    const size_t n = std::strlen(path);
    if(n == 0 || n > kPathMax)
        return false;
    if(std::strncmp(path, "PROGRAMS/", 9) != 0)
        return false;
    for(size_t i = 0; i < n; ++i)
        if(!IsPathChar(path[i]))
            return false;
    if(std::strstr(path, "//") || std::strstr(path, "/./") || std::strstr(path, "/../")
       || std::strncmp(path, "../", 3) == 0 || std::strcmp(path + n - 3, "/..") == 0
       || std::strcmp(path + n - 2, "/.") == 0 || path[n - 1] == '/')
        return false;
    if(n < 5 || std::strcmp(path + n - 4, ".dsy") != 0)
        return false;
    const char* slash = std::strrchr(path, '/');
    if(slash == nullptr || std::strlen(slash + 1) < 5)  // "<name>.dsy" needs a name
        return false;
    // Every component must have a character other than '.'.
    const char* start = path;
    for(const char* p = path; ; ++p) {
        if(*p == '/' || *p == 0) {
            bool other = false;
            for(const char* q = start; q < p; ++q)
                if(*q != '.')
                    other = true;
            if(!other)
                return false;
            if(*p == 0)
                break;
            start = p + 1;
        }
    }
    return true;
}

bool ValidName(const char* name) {
    if(name == nullptr)
        return false;
    const size_t n = std::strlen(name);
    if(n == 0 || n > kNameMax)
        return false;
    size_t i = 0;
    while(i < n) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if(c < 0x20 || c == 0x7F)
            return false;
        size_t extra = 0;
        if(c < 0x80) extra = 0;
        else if((c & 0xE0) == 0xC0 && c >= 0xC2) extra = 1;
        else if((c & 0xF0) == 0xE0) extra = 2;
        else if((c & 0xF8) == 0xF0 && c <= 0xF4) extra = 3;
        else return false;
        for(size_t k = 1; k <= extra; ++k) {
            if(i + k >= n || (static_cast<unsigned char>(name[i + k]) & 0xC0) != 0x80)
                return false;
        }
        i += extra + 1;
    }
    return true;
}

bool ValidCategory(const char* name) {
    if(name == nullptr)
        return false;
    for(const char* c : kCategories)
        if(std::strcmp(c, name) == 0)
            return true;
    return false;
}

bool ParseHex256(const char* text, uint8_t out[32]) {
    if(text == nullptr || std::strlen(text) != 64)
        return false;
    for(int i = 0; i < 32; ++i) {
        const int hi = Reader::HexDigit(text[2 * i]);
        const int lo = Reader::HexDigit(text[2 * i + 1]);
        if(hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

CatalogError ParseCatalog(const char* text, size_t len, Catalog& out, CatalogParseInfo* info) {
    out = Catalog();
    CatalogParseInfo local;
    CatalogParseInfo& inf = info ? *info : local;
    inf = CatalogParseInfo();
    if(text == nullptr)
        return CatalogError::Syntax;
    if(len > kCatalogMaxBytes)
        return CatalogError::TooLarge;

    Reader r(text, len);
    r.SkipBom();
    bool seen_format = false, seen_generation = false, seen_hardware = false, seen_programs = false;
    bool ok = r.Expect('{');
    bool first = true;
    while(ok) {
        if(!first) {
            const int sep = r.Peek();
            if(sep == '}') {
                r.Expect('}');
                break;
            }
            if(!(ok = r.Expect(',')))
                break;
        } else if(r.Peek() == '}') {
            r.Expect('}');
            break;
        }
        first = false;
        char key[24];
        if(!(ok = r.String(key, sizeof(key), CatalogError::Syntax)))
            break;
        if(!(ok = r.Expect(':')))
            break;
        if(std::strcmp(key, "format") == 0) {
            if(seen_format) { ok = r.Fail(CatalogError::Duplicate); break; }
            seen_format = true;
            uint32_t v;
            if(!(ok = r.U32(v, CatalogError::Format))) break;
            if(v != kCatalogFormat) { ok = r.Fail(CatalogError::Format); break; }
        } else if(std::strcmp(key, "generation") == 0) {
            if(seen_generation) { ok = r.Fail(CatalogError::Duplicate); break; }
            seen_generation = true;
            if(!(ok = r.U32(out.generation, CatalogError::Generation))) break;
        } else if(std::strcmp(key, "hardware") == 0) {
            if(seen_hardware) { ok = r.Fail(CatalogError::Duplicate); break; }
            seen_hardware = true;
            char hw[kHardwareField + 1];
            if(!(ok = r.String(hw, sizeof(hw), CatalogError::Hardware))) break;
            if(std::strcmp(hw, kHardwareId) != 0) { ok = r.Fail(CatalogError::Hardware); break; }
        } else if(std::strcmp(key, "programs") == 0) {
            if(seen_programs) { ok = r.Fail(CatalogError::Duplicate); break; }
            seen_programs = true;
            if(!(ok = r.Expect('['))) { r.error = CatalogError::Programs; break; }
            if(r.Peek() == ']') {
                r.Expect(']');
                continue;
            }
            while(true) {
                if(r.Peek() != '{') { ok = r.Fail(CatalogError::Programs); break; }
                if(out.count >= kCatalogMaxPrograms) { ok = r.Fail(CatalogError::TooMany); break; }
                inf.entry = out.count;
                CatalogEntry& e = out.entries[out.count];
                e = CatalogEntry();
                if(!(ok = ParseEntry(r, e))) break;
                for(uint8_t i = 0; i < out.count; ++i) {
                    if(out.entries[i].slot == e.slot) { ok = r.Fail(CatalogError::DuplicateSlot); break; }
                    if(std::strcmp(out.entries[i].id, e.id) == 0) { ok = r.Fail(CatalogError::DuplicateId); break; }
                }
                if(!ok) break;
                ++out.count;
                inf.entry = -1;
                const int sep = r.Peek();
                if(sep == ',') {
                    r.Expect(',');
                    if(r.Peek() == ']') { ok = r.Fail(CatalogError::Syntax); break; }  // trailing comma
                    continue;
                }
                if(sep == ']') { r.Expect(']'); break; }
                ok = r.Fail(CatalogError::Syntax);
                break;
            }
            if(!ok) break;
        } else {
            if(!(ok = r.SkipValue(1))) break;
        }
    }
    if(ok && !r.AtEnd())
        ok = r.Fail(CatalogError::Syntax);
    if(ok && (!seen_format || !seen_generation || !seen_hardware || !seen_programs))
        ok = r.Fail(CatalogError::Missing);
    inf.offset = r.Offset();
    if(!ok) {
        out.count = 0;
        return r.error == CatalogError::None ? CatalogError::Syntax : r.error;
    }
    return CatalogError::None;
}

} // namespace pm
