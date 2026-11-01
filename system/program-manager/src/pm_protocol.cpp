#include "pm_protocol.h"
#include <cstring>

namespace pm {
namespace {

// Diagnostic commands that act on outputs or the card: only in the Diagnostics mode.
const char* const kDiagOnly[] = {"LED", "CVOUT", "TONE", "SCOPE", "SDTEST", "READER"};

void Error(ddp::Writer& reply, const char* type, const char* code) {
    reply.Str("ERR c=").Str(type).Str(" e=").Str(code);
}

int HexValue(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool SafeNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'
           || c == '_' || c == '-';
}

} // namespace

const char* UiErrorName(UiError error) {
    switch(error) {
        case UiError::None: return "OK";
        case UiError::EmptySlot: return "EMPTY";
        case UiError::NoCard: return "NOCARD";
        case UiError::Catalog: return "CATALOG";
        case UiError::Load: return "LOAD";
    }
    return "?";
}

Protocol::Protocol(Manager& manager, const ProtocolConfig& config)
    : manager_(manager), config_(config) {}

void Protocol::EncodeName(const char* name, ddp::Writer& out) {
    for(const char* p = name; *p; ++p) {
        if(SafeNameChar(*p))
            out.Char(*p);
        else
            out.Char('%').Hex(static_cast<uint8_t>(*p), 2);
    }
}

bool Protocol::ParseHashPrefix(const char* text, uint8_t out[32], size_t& len) {
    const size_t n = std::strlen(text);
    if(n < 16 || n > 64 || (n & 1u))
        return false;
    for(size_t i = 0; i < n / 2; ++i) {
        const int hi = HexValue(text[2 * i]);
        const int lo = HexValue(text[2 * i + 1]);
        if(hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    len = n / 2;
    return true;
}

void Protocol::WriteInfo(ddp::Writer& out) const {
    out.Str("PMINFO app=").Str(config_.app)
        .Str(" fw=").Str(config_.fw)
        .Str(" profile=").Str(config_.profile)
        .Str(" build=").Str(config_.build)
        .Str(" mode=").Str(ModeName(manager_.mode()))
        .Str(" gen=").U32(manager_.generation())
        .Str(" cat=").Str(CatalogStateName(manager_.catalog_state()))
        .Str(" cgen=").U32(manager_.catalog().generation)
        .Str(" n=").U32(manager_.catalog().count)
        .Str(" sd=").Char(manager_.card_present() ? '1' : '0')
        .Str(" rootbin=").Char(manager_.root_bin_warning() ? '1' : '0')
        .Str(" slots=").U32(kCatalogMaxPrograms);
}

void Protocol::WriteStatus(ddp::Writer& out) const {
    const LoadStatus& ls = manager_.loader().Status();
    out.Str("PMST mode=").Str(ModeName(manager_.mode()))
        .Str(" state=").Str(LoadStateName(ls.state))
        .Str(" e=").Str(LoadErrorName(ls.error))
        .Str(" ie=").Str(ImageErrorName(ls.image_error))
        .Str(" ve=").Str(VectorErrorName(ls.vector_error))
        .Str(" slot=").U32(manager_.last_slot())
        .Str(" rid=").U32(manager_.has_last_request() ? manager_.last_request_id() : 0u)
        .Str(" pct=").U32(manager_.loader().Percent())
        .Str(" gen=").U32(manager_.generation())
        .Str(" cat=").Str(CatalogStateName(manager_.catalog_state()))
        .Str(" ce=").Str(CatalogErrorName(manager_.catalog_error()))
        .Str(" cd=").U32(static_cast<uint32_t>(manager_.catalog_detail() < 0 ? 0 : manager_.catalog_detail()))
        .Str(" sd=").Char(manager_.card_present() ? '1' : '0')
        .Str(" rootbin=").Char(manager_.root_bin_warning() ? '1' : '0')
        .Str(" uerr=").Str(UiErrorName(manager_.ui_error()));
}

void Protocol::WriteEntry(const CatalogEntry& entry, ddp::Writer& out) const {
    out.Str("PMENT gen=").U32(manager_.generation())
        .Str(" slot=").U32(entry.slot)
        .Str(" id=").Str(entry.id)
        .Str(" name=");
    EncodeName(entry.name, out);
    out.Str(" cat=").Str(entry.category)
        .Str(" ver=").Str(entry.version)
        .Str(" hash=");
    for(int i = 0; i < 32; ++i)
        out.Hex(entry.payload_sha256[i], 2);
}

void Protocol::HandleStart(const char* body, uint32_t now_ms, ddp::Writer& reply) {
    char text[70];
    uint32_t slot = 0, gen = 0, rid = 0;
    char id[kIdMax + 1];
    uint8_t hash[32];
    size_t hash_len = 0;
    if(!ddp::FindField(body, "slot", text, 12) || !ddp::ParseU32(text, slot) || slot < 1
       || slot > kCatalogMaxPrograms || !ddp::FindField(body, "id", id, sizeof(id))
       || !ddp::FindField(body, "hash", text, sizeof(text)) || !ParseHashPrefix(text, hash, hash_len)
       || !ddp::FindField(body, "gen", text, 12) || !ddp::ParseU32(text, gen)
       || !ddp::FindField(body, "rid", text, 12) || !ddp::ParseU32(text, rid)) {
        Error(reply, "PMSTART", "ARG");
        return;
    }
    const StartResult r = manager_.StartRequest(static_cast<uint8_t>(slot), id, hash, hash_len, gen, rid, now_ms);
    switch(r) {
        case StartResult::Accepted:
            reply.Str("ACK c=PMSTART rid=").U32(rid).Str(" slot=").U32(slot);
            return;
        case StartResult::Duplicate: {
            const LoadStatus& ls = manager_.loader().Status();
            reply.Str("ACK c=PMSTART rid=").U32(rid).Str(" dup=1 state=").Str(LoadStateName(ls.state))
                .Str(" e=").Str(LoadErrorName(ls.error));
            return;
        }
        default: Error(reply, "PMSTART", StartResultName(r)); return;
    }
}

bool Protocol::Handle(const char* body, const char* type, bool session_active, uint32_t now_ms,
                      ddp::Writer& reply, uint32_t& requests) {
    (void)requests;
    // Mode gate for the diagnostic commands.
    for(const char* name : kDiagOnly) {
        if(std::strcmp(type, name) == 0) {
            if(manager_.mode() == Mode::Diagnostics)
                return false;  // the diagnostics layer handles it
            Error(reply, type, "MODE");
            return true;
        }
    }
    if(std::strcmp(type, "BOOT") == 0) {
        if(manager_.mode() == Mode::Loading) {
            Error(reply, type, "BUSY");
            return true;
        }
        return false;  // Session/App: ACK, flush, Board::EnterBootloader
    }
    if(std::strncmp(type, "PM", 2) != 0)
        return false;
    if(std::strcmp(type, "PMINFO") == 0) {
        WriteInfo(reply);
        return true;
    }
    if(std::strcmp(type, "PMSTATUS") == 0) {
        WriteStatus(reply);
        return true;
    }
    if(std::strcmp(type, "PMLIST") == 0) {
        char text[12];
        const ddp::Lookup found = ddp::LookupField(body, "s", text, sizeof(text));
        if(found == ddp::Lookup::Missing) {
            uint16_t mask = 0;
            const Catalog& cat = manager_.catalog();
            if(manager_.catalog_state() == CatalogState::Ok)
                for(uint8_t i = 0; i < cat.count; ++i)
                    mask |= static_cast<uint16_t>(1u << (cat.entries[i].slot - 1));
            reply.Str("PMLIST gen=").U32(manager_.generation())
                .Str(" cgen=").U32(cat.generation)
                .Str(" n=").U32(cat.count)
                .Str(" mask=").Hex(mask, 4)
                .Str(" cat=").Str(CatalogStateName(manager_.catalog_state()));
            return true;
        }
        uint32_t slot = 0;
        if(found == ddp::Lookup::TooLong || !ddp::ParseU32(text, slot) || slot < 1 || slot > kCatalogMaxPrograms) {
            Error(reply, type, "ARG");
            return true;
        }
        const CatalogEntry* entry = manager_.catalog_state() == CatalogState::Ok
                                        ? manager_.catalog().FindSlot(static_cast<uint8_t>(slot))
                                        : nullptr;
        if(entry == nullptr) {
            Error(reply, type, "NOENTRY");
            return true;
        }
        WriteEntry(*entry, reply);
        return true;
    }
    // Everything below changes device state: only inside a host session.
    if(std::strcmp(type, "PMSTART") == 0 || std::strcmp(type, "PMMODE") == 0
       || std::strcmp(type, "PMRESCAN") == 0) {
        if(!session_active) {
            Error(reply, type, "SESSION");
            return true;
        }
    } else {
        return false;  // unknown PM* command: the session answers UNKNOWN
    }
    if(std::strcmp(type, "PMSTART") == 0) {
        HandleStart(body, now_ms, reply);
        return true;
    }
    if(std::strcmp(type, "PMMODE") == 0) {
        char text[8];
        Mode mode;
        if(!ddp::FindField(body, "m", text, sizeof(text))) {
            Error(reply, type, "ARG");
            return true;
        }
        if(std::strcmp(text, "SELECT") == 0) mode = Mode::Select;
        else if(std::strcmp(text, "DIAG") == 0) mode = Mode::Diagnostics;
        else {
            Error(reply, type, "ARG");
            return true;
        }
        if(!manager_.SetMode(mode, now_ms)) {
            Error(reply, type, "BUSY");
            return true;
        }
        reply.Str("ACK c=PMMODE m=").Str(ModeName(manager_.mode()));
        return true;
    }
    // PMRESCAN
    manager_.RequestRescan();
    reply.Str("ACK c=PMRESCAN");
    return true;
}

} // namespace pm
