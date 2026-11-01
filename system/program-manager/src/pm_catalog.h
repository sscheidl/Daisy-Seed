// /DAISY/catalog.json: strict, allocation-free parser for the program catalog.
// Format documented in docs/PACKAGE.md. Limits (docs/CONTRACT.md, section 6):
// file <= 16 KiB, <= 15 programs, slots 1..15 unique, ids unique, display names
// <= 64 UTF-8 bytes, relative paths <= 160 bytes below PROGRAMS/.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pm {

constexpr size_t kCatalogMaxBytes = 16 * 1024;
constexpr uint8_t kCatalogMaxPrograms = 15;  // slot 16 is the resident diagnostics
constexpr uint32_t kCatalogFormat = 1;
constexpr size_t kIdMax = 32, kNameMax = 64, kCategoryMax = 16, kVersionMax = 16, kPathMax = 160;

struct CatalogEntry {
    uint8_t slot = 0;                  // 1..15
    char id[kIdMax + 1] = {};          // [a-z0-9-]
    char name[kNameMax + 1] = {};      // UTF-8, no control characters
    char category[kCategoryMax + 1] = {};  // instrument | effect | sequencer | other
    char version[kVersionMax + 1] = {};
    char path[kPathMax + 1] = {};      // "PROGRAMS/<dir>/<file>.dsy", relative to /DAISY
    uint8_t payload_sha256[32] = {};
};

struct Catalog {
    uint32_t generation = 0;  // the file's own generation number
    uint8_t count = 0;
    CatalogEntry entries[kCatalogMaxPrograms];

    const CatalogEntry* FindSlot(uint8_t slot) const;
    const CatalogEntry* FindId(const char* id) const;
};

enum class CatalogError : uint8_t {
    None = 0,
    TooLarge,       // more than kCatalogMaxBytes
    Syntax,         // not well-formed JSON within the supported subset
    Depth,          // nesting deeper than supported
    Duplicate,      // a key repeated in one object
    Missing,        // a required key is absent
    Format,         // "format" != 1
    Generation,     // not an unsigned 32-bit integer
    Hardware,       // not "seed3-desktop"
    Programs,       // "programs" is not an array of objects
    TooMany,        // more than kCatalogMaxPrograms entries
    Slot,           // slot outside 1..15
    DuplicateSlot,
    Id,             // empty, too long or bad characters
    DuplicateId,
    Name,           // empty, too long, control characters or invalid UTF-8
    Category,       // unknown category word
    Version,        // empty, too long or bad characters
    Path,           // not a safe relative path below PROGRAMS/ ending in .dsy
    Hash,           // payload_sha256 is not 64 hex digits
    Profile,        // "profile" present but not the supported profile
};

const char* CatalogErrorName(CatalogError error);

struct CatalogParseInfo {
    size_t offset = 0;      // byte offset in the text where the error was detected
    int entry = -1;         // index of the program entry concerned, -1 for the top level
};

// Parses `len` bytes of JSON text (a UTF-8 BOM is skipped). On error `out` is
// unspecified. `info` is optional.
CatalogError ParseCatalog(const char* text, size_t len, Catalog& out, CatalogParseInfo* info = nullptr);

// Validation helpers shared with the host tools' tests.
bool ValidPath(const char* path);       // charset, no "..", no "//", starts with PROGRAMS/, ends .dsy
bool ValidName(const char* name);       // UTF-8, no control characters, 1..kNameMax bytes
bool ValidCategory(const char* name);
bool ParseHex256(const char* text, uint8_t out[32]);  // exactly 64 hex digits

} // namespace pm
