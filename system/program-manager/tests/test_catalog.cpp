// catalog.json parser: accepted documents, every rejection, limits and traversal.
#include "check.h"
#include "pm_catalog.h"
#include <cstring>
#include <string>

using namespace pm;

namespace {

const char* kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string Entry(int slot, const char* id, const std::string& extra = "") {
    std::string s = "{\"slot\":" + std::to_string(slot) + ",\"id\":\"" + id + "\",\"name\":\"Name " + id
                    + "\",\"version\":\"0.1.0\",\"path\":\"PROGRAMS/" + id + "/" + id + "-v0.1.0.dsy\","
                      "\"payload_sha256\":\"" + kHash + "\"" + extra + "}";
    return s;
}

std::string Doc(const std::string& programs, const char* head = "\"format\":1,\"generation\":7,\"hardware\":\"seed3-desktop\",") {
    return std::string("{") + head + "\"programs\":[" + programs + "]}";
}

CatalogError Parse(const std::string& text, Catalog* out = nullptr) {
    Catalog local;
    return ParseCatalog(text.data(), text.size(), out ? *out : local);
}

} // namespace

int main() {
    Catalog cat;
    const std::string good = Doc(Entry(1, "toy-piano", ",\"category\":\"instrument\"") + "," + Entry(3, "reverb-fx", ",\"category\":\"effect\",\"profile\":\"axi-sram-480k-v1\",\"file_sha256\":\"" + std::string(kHash) + "\",\"comment\":{\"x\":[1,2,{\"y\":null}]}"));
    CHECK(Parse(good, &cat) == CatalogError::None);
    CHECK_EQ(cat.generation, 7u);
    CHECK_EQ(cat.count, 2);
    CHECK_EQ(cat.entries[0].slot, 1);
    CHECK_STREQ(cat.entries[0].id, "toy-piano");
    CHECK_STREQ(cat.entries[0].name, "Name toy-piano");
    CHECK_STREQ(cat.entries[0].category, "instrument");
    CHECK_STREQ(cat.entries[0].path, "PROGRAMS/toy-piano/toy-piano-v0.1.0.dsy");
    CHECK_EQ(cat.entries[0].payload_sha256[0], 0x01);
    CHECK_EQ(cat.entries[0].payload_sha256[31], 0xef);
    CHECK_STREQ(cat.entries[1].category, "effect");
    CHECK(cat.FindSlot(3) == &cat.entries[1]);
    CHECK(cat.FindSlot(2) == nullptr);
    CHECK(cat.FindId("reverb-fx") == &cat.entries[1]);
    CHECK(cat.FindId("nope") == nullptr);

    // Whitespace, BOM, default category, escapes and UTF-8 names.
    const std::string spaced = "\xEF\xBB\xBF \n{ \"format\" : 1 , \"generation\" : 0 , \"hardware\" : \"seed3-desktop\" ,\n \"programs\" : [ "
                               + Entry(15, "a") + " ] }\n";
    CHECK(Parse(spaced, &cat) == CatalogError::None);
    CHECK_STREQ(cat.entries[0].category, "other");
    CHECK_EQ(cat.entries[0].slot, 15);
    std::string esc = Doc("{\"slot\":2,\"id\":\"x\",\"name\":\"Tr\\u00e4ume \\\"S\\\" \\/ \\\\\",\"version\":\"1\",\"path\":\"PROGRAMS/x/y.dsy\",\"payload_sha256\":\"" + std::string(kHash) + "\"}");
    CHECK(Parse(esc, &cat) == CatalogError::None);
    CHECK_STREQ(cat.entries[0].name, "Tr\xC3\xA4ume \"S\" / \\");
    CHECK(Parse(Doc("")) == CatalogError::None);  // no programs is a valid (empty) card

    // Structure errors.
    CHECK(Parse("") == CatalogError::Syntax);
    CHECK(Parse("[]") == CatalogError::Syntax);
    CHECK(Parse(good + "x") == CatalogError::Syntax);
    CHECK(Parse(good.substr(0, good.size() - 1)) == CatalogError::Syntax);  // truncated
    CHECK(Parse(good.substr(0, 40)) == CatalogError::Syntax);
    CHECK(Parse(Doc(Entry(1, "a") + ",")) == CatalogError::Syntax);          // trailing comma
    CHECK(Parse(Doc(Entry(1, "a"), "\"format\":1,\"format\":1,\"generation\":1,\"hardware\":\"seed3-desktop\",")) == CatalogError::Duplicate);
    CHECK(Parse(Doc(Entry(1, "a"), "\"generation\":1,\"hardware\":\"seed3-desktop\",")) == CatalogError::Missing);
    CHECK(Parse(Doc(Entry(1, "a"), "\"format\":2,\"generation\":1,\"hardware\":\"seed3-desktop\",")) == CatalogError::Format);
    CHECK(Parse(Doc(Entry(1, "a"), "\"format\":1,\"generation\":-1,\"hardware\":\"seed3-desktop\",")) == CatalogError::Generation);
    CHECK(Parse(Doc(Entry(1, "a"), "\"format\":1,\"generation\":4294967296,\"hardware\":\"seed3-desktop\",")) == CatalogError::Generation);
    CHECK(Parse(Doc(Entry(1, "a"), "\"format\":1,\"generation\":007,\"hardware\":\"seed3-desktop\",")) == CatalogError::Generation);
    CHECK(Parse(Doc(Entry(1, "a"), "\"format\":1,\"generation\":1.5,\"hardware\":\"seed3-desktop\",")) == CatalogError::Generation);
    CHECK(Parse(Doc(Entry(1, "a"), "\"format\":1,\"generation\":1,\"hardware\":\"seed2\",")) == CatalogError::Hardware);
    CHECK(Parse("{\"format\":1,\"generation\":1,\"hardware\":\"seed3-desktop\",\"programs\":{}}") == CatalogError::Programs);
    CHECK(Parse(Doc("1")) == CatalogError::Programs);
    CHECK(Parse(Doc("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":1}}}}}}}")) == CatalogError::Depth);
    CHECK(Parse(Doc("{\"a\":{\"b\":{\"c\":{\"d\":1}}}}")) == CatalogError::Missing);  // depth ok, fields missing
    CHECK(Parse("{\"format\":1,\"generation\":1,\"hardware\":\"seed3-desktop\",\"programs\":[],"
                "\"x\":[[[[[[[1]]]]]]]}") == CatalogError::Depth);

    // Entry errors.
    CHECK(Parse(Doc(Entry(0, "a"))) == CatalogError::Slot);
    CHECK(Parse(Doc(Entry(16, "a"))) == CatalogError::Slot);
    CHECK(Parse(Doc(Entry(1, "a") + "," + Entry(1, "b"))) == CatalogError::DuplicateSlot);
    CHECK(Parse(Doc(Entry(1, "a") + "," + Entry(2, "a"))) == CatalogError::DuplicateId);
    CHECK(Parse(Doc(Entry(1, "A"))) == CatalogError::Id);
    CHECK(Parse(Doc(Entry(1, ""))) == CatalogError::Id);
    CHECK(Parse(Doc(Entry(1, std::string(33, 'a').c_str()))) == CatalogError::Id);
    CHECK(Parse(Doc("{\"slot\":1,\"slot\":1,\"id\":\"a\"}")) == CatalogError::Duplicate);
    CHECK(Parse(Doc("{\"slot\":1,\"id\":\"a\"}")) == CatalogError::Missing);
    // Replace one string-valued key of a good entry with `value` (quoted JSON text).
    auto with = [&](const char* key, const std::string& value) {
        std::string s = Entry(1, "a", ",\"category\":\"other\"");
        const std::string k = std::string("\"") + key + "\":";
        const size_t p = s.find(k);
        const size_t end = s.find('"', s.find(':', p) + 2);  // closing quote of the value
        return Doc(s.substr(0, p) + k + value + s.substr(end + 1));
    };
    CHECK(Parse(with("name", "\"\"")) == CatalogError::Name);
    CHECK(Parse(with("name", "\"" + std::string(65, 'n') + "\"")) == CatalogError::Name);
    CHECK(Parse(with("name", "\"" + std::string(64, 'n') + "\"")) == CatalogError::None);
    CHECK(Parse(with("name", "\"bad\\u0007bell\"")) == CatalogError::Name);
    CHECK(Parse(with("name", "\"raw\tTab\"")) == CatalogError::Syntax);   // raw control character
    CHECK(Parse(with("name", "\"\xC3\x28\"")) == CatalogError::Name);     // invalid UTF-8
    CHECK(Parse(with("category", "\"synth\"")) == CatalogError::Category);
    CHECK(Parse(with("version", "\"\"")) == CatalogError::Version);
    CHECK(Parse(with("version", "\"1 beta\"")) == CatalogError::Version);
    CHECK(Parse(with("path", "\"/PROGRAMS/a/b.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS/../b.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS/a/../b.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS//b.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS/a/b.bin\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS/a/b.dsy/\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"SAMPLES/a/b.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS/a b/c.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS\\\\a\\\\c.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS/./c.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS/" + std::string(150, 'p') + "/c.dsy\"")) == CatalogError::Path);
    CHECK(Parse(with("path", "\"PROGRAMS/" + std::string(140, 'p') + "/c.dsy\"")) == CatalogError::None);
    CHECK(Parse(with("payload_sha256", "\"" + std::string(63, 'a') + "\"")) == CatalogError::Hash);
    CHECK(Parse(with("payload_sha256", "\"" + std::string(64, 'g') + "\"")) == CatalogError::Hash);
    CHECK(Parse(with("payload_sha256", "\"" + std::string(65, 'a') + "\"")) == CatalogError::Hash);
    CHECK(Parse(Doc(Entry(1, "a", ",\"profile\":\"qspi-xip-v1\""))) == CatalogError::Profile);
    CHECK(Parse(Doc(Entry(1, "a", ",\"file_sha256\":\"zz\""))) == CatalogError::Hash);
    CHECK(Parse(Doc(Entry(1, "a", ",\"slot\":1"))) == CatalogError::Duplicate);

    // Limits: 15 entries ok, 16 rejected, 16 KiB file size.
    std::string many;
    for(int i = 1; i <= 15; ++i)
        many += (i > 1 ? "," : "") + Entry(i, ("p" + std::to_string(i)).c_str());
    CHECK(Parse(Doc(many), &cat) == CatalogError::None);
    CHECK_EQ(cat.count, 15);
    CHECK(Parse(Doc(many + "," + Entry(1, "p16"))) == CatalogError::TooMany);  // too many before slot check
    std::string big = good;
    big.insert(1, "\"pad\":\"" + std::string(kCatalogMaxBytes, 'x') + "\",");
    CHECK(Parse(big) == CatalogError::TooLarge);
    std::string padded = good;
    padded.insert(1, "\"pad\":\"" + std::string(kCatalogMaxBytes - good.size() - 10, 'x') + "\",");
    CHECK(padded.size() <= kCatalogMaxBytes);
    CHECK(Parse(padded) == CatalogError::None);

    // Parse info points at the failing entry.
    CatalogParseInfo info;
    Catalog scratch;
    const std::string bad = Doc(Entry(1, "a") + "," + Entry(2, "B"));
    CHECK(ParseCatalog(bad.data(), bad.size(), scratch, &info) == CatalogError::Id);
    CHECK_EQ(info.entry, 1);
    CHECK(info.offset > 0);
    CHECK_EQ(scratch.count, 0);

    // Helpers.
    CHECK(ValidPath("PROGRAMS/x/y.dsy"));
    CHECK(!ValidPath("PROGRAMS/x/.dsy"));
    CHECK(!ValidPath(nullptr));
    CHECK(ValidName("Toy Piano v0.4 \xE2\x80\x93 Ambient"));
    CHECK(!ValidName("\x7F"));
    CHECK(ValidCategory("sequencer"));
    uint8_t h[32];
    CHECK(ParseHex256("ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789", h));
    CHECK_EQ(h[0], 0xAB);
    CHECK_STREQ(CatalogErrorName(CatalogError::DuplicateSlot), "DUPSLOT");
    return check::Finish("test_catalog");
}
