#include "pm_manager.h"
#include <cstring>

namespace pm {
namespace {

int PopCount(uint16_t v) {
    int n = 0;
    while(v) {
        n += v & 1u;
        v >>= 1;
    }
    return n;
}

uint8_t SingleKeySlot(uint16_t mask) {  // 1..16 for exactly one bit, else 0
    if(mask == 0 || (mask & (mask - 1)) != 0)
        return 0;
    uint8_t slot = 1;
    while(!(mask & 1u)) {
        mask >>= 1;
        ++slot;
    }
    return slot;
}

} // namespace

const char* ModeName(Mode mode) {
    switch(mode) {
        case Mode::Select: return "SELECT";
        case Mode::Diagnostics: return "DIAG";
        case Mode::Loading: return "LOADING";
        case Mode::Service: return "SERVICE";
    }
    return "?";
}

const char* CatalogStateName(CatalogState state) {
    switch(state) {
        case CatalogState::NoCard: return "NOCARD";
        case CatalogState::Scanning: return "SCANNING";
        case CatalogState::Ok: return "OK";
        case CatalogState::Missing: return "MISSING";
        case CatalogState::Invalid: return "INVALID";
        case CatalogState::MountFailed: return "MOUNT";
        case CatalogState::ReadError: return "READ";
        case CatalogState::Unsupported: return "UNSUPPORTED";
    }
    return "?";
}

const char* StartResultName(StartResult result) {
    switch(result) {
        case StartResult::Accepted: return "ACCEPTED";
        case StartResult::Duplicate: return "DUPLICATE";
        case StartResult::WrongMode: return "MODE";
        case StartResult::Busy: return "BUSY";
        case StartResult::NoCatalog: return "NOCATALOG";
        case StartResult::Generation: return "GENERATION";
        case StartResult::NoEntry: return "NOENTRY";
        case StartResult::Hash: return "HASH";
        case StartResult::Unsupported: return "UNSUPPORTED";
    }
    return "?";
}

Manager::Manager(Storage& storage, LoadTarget& target, BootCommit& commit, bool sd_supported)
    : storage_(storage), loader_(storage, target, commit), sd_supported_(sd_supported) {
    catalog_state_ = sd_supported ? CatalogState::NoCard : CatalogState::Unsupported;
    catalog_text_[0] = 0;
}

void Manager::ClearCatalog(CatalogState state) {
    catalog_ = Catalog();
    catalog_state_ = state;
    catalog_error_ = CatalogError::None;
    catalog_detail_ = 0;
    root_bin_ = false;
    ++generation_;  // stale host orders must not match any more
}

void Manager::ScanCatalog(uint32_t now_ms) {
    (void)now_ms;
    scan_due_ = false;
    rescan_pending_ = false;
    if(!sd_supported_) {
        ClearCatalog(CatalogState::Unsupported);
        return;
    }
    storage_.Unmount();
    if(!card_present_) {
        ClearCatalog(CatalogState::NoCard);
        return;
    }
    uint32_t len = 0;
    int detail = 0;
    const Storage::CatalogResult r = storage_.ReadCatalog(catalog_text_, kCatalogMaxBytes, len, detail);
    Catalog parsed;
    CatalogError err = CatalogError::None;
    CatalogState state = CatalogState::Ok;
    switch(r) {
        case Storage::CatalogResult::Ok:
            err = ParseCatalog(catalog_text_, len, parsed);
            if(err != CatalogError::None)
                state = CatalogState::Invalid;
            break;
        case Storage::CatalogResult::NoCard: state = CatalogState::NoCard; break;
        case Storage::CatalogResult::MountFailed: state = CatalogState::MountFailed; break;
        case Storage::CatalogResult::Missing: state = CatalogState::Missing; break;
        case Storage::CatalogResult::TooLarge:
            state = CatalogState::Invalid;
            err = CatalogError::TooLarge;
            break;
        case Storage::CatalogResult::ReadError: state = CatalogState::ReadError; break;
    }
    ClearCatalog(state);
    catalog_error_ = err;
    catalog_detail_ = detail;
    if(state == CatalogState::Ok)
        catalog_ = parsed;
    if(state != CatalogState::NoCard && state != CatalogState::MountFailed)
        root_bin_ = storage_.RootHasBinFile();
}

void Manager::RequestRescan() { rescan_pending_ = true; }

bool Manager::RequestService() {
    if(mode_ == Mode::Loading)
        return false;
    mode_ = Mode::Service;
    return true;
}

bool Manager::SetMode(Mode mode, uint32_t now_ms) {
    if(mode_ == Mode::Loading || mode_ == Mode::Service)
        return false;
    if(mode != Mode::Select && mode != Mode::Diagnostics)
        return false;
    EnterMode(mode, now_ms);
    return true;
}

void Manager::EnterMode(Mode mode, uint32_t now_ms) {
    (void)now_ms;
    mode_ = mode;
    release_gate_ = last_keys_ != 0;  // keys held during the change must be released first
    cycle_keys_ = 0;
    cycle_valid_ = false;
    return_holding_ = false;
}

void Manager::ShowError(UiError error, uint32_t now_ms) {
    ui_error_ = error;
    error_until_ms_ = now_ms + kErrorFlashMs;
}

void Manager::StartSlot(uint8_t slot, uint32_t request_id, uint32_t now_ms) {
    const CatalogEntry* entry = catalog_state_ == CatalogState::Ok ? catalog_.FindSlot(slot) : nullptr;
    last_slot_ = slot;
    if(entry == nullptr) {
        ShowError(catalog_state_ == CatalogState::NoCard ? UiError::NoCard
                  : catalog_state_ == CatalogState::Ok   ? UiError::EmptySlot
                                                          : UiError::Catalog,
                  now_ms);
        return;
    }
    loader_.Reset();
    if(!loader_.Start(*entry, now_ms, request_id)) {
        ShowError(UiError::Load, now_ms);
        return;
    }
    ui_error_ = UiError::None;
    EnterMode(Mode::Loading, now_ms);
}

StartResult Manager::StartRequest(uint8_t slot, const char* id, const uint8_t* hash, size_t hash_len,
                                  uint32_t generation, uint32_t request_id, uint32_t now_ms) {
    if(!sd_supported_)
        return StartResult::Unsupported;
    if(has_last_rid_ && request_id == last_rid_)
        return StartResult::Duplicate;  // replayed order (reconnect, retry): report, do not restart
    if(mode_ == Mode::Loading)
        return StartResult::Busy;
    if(mode_ != Mode::Select)
        return StartResult::WrongMode;
    if(catalog_state_ != CatalogState::Ok)
        return StartResult::NoCatalog;
    if(generation != generation_)
        return StartResult::Generation;
    const CatalogEntry* entry = catalog_.FindSlot(slot);
    if(entry == nullptr || id == nullptr || std::strcmp(entry->id, id) != 0)
        return StartResult::NoEntry;
    if(hash == nullptr || hash_len < 8 || hash_len > 32
       || std::memcmp(entry->payload_sha256, hash, hash_len) != 0)
        return StartResult::Hash;
    last_rid_ = request_id;
    has_last_rid_ = true;
    StartSlot(slot, request_id, now_ms);
    return mode_ == Mode::Loading ? StartResult::Accepted : StartResult::Busy;
}

void Manager::HandleSelectKeys(uint32_t now_ms, uint16_t keys) {
    if(release_gate_) {
        if(keys == 0)
            release_gate_ = false;
        return;
    }
    if(keys != 0) {
        if(cycle_keys_ == 0) {  // press begins
            cycle_since_ms_ = now_ms;
            cycle_valid_ = true;
        }
        cycle_keys_ |= keys;
        if(PopCount(cycle_keys_) > 1 || now_ms - cycle_since_ms_ >= kLongPressMs)
            cycle_valid_ = false;  // chord or long hold: never a selection
        return;
    }
    if(cycle_keys_ == 0)
        return;  // nothing pressed
    const uint8_t slot = cycle_valid_ ? SingleKeySlot(cycle_keys_) : 0;
    cycle_keys_ = 0;
    cycle_valid_ = false;
    if(slot == 0)
        return;
    if(slot == 16) {
        EnterMode(Mode::Diagnostics, now_ms);
        return;
    }
    StartSlot(slot, 0, now_ms);
}

void Manager::HandleDiagKeys(uint32_t now_ms, uint16_t keys) {
    if(release_gate_) {
        if(keys == 0)
            release_gate_ = false;
        return;
    }
    if(keys != kReturnMask) {
        return_holding_ = false;
        return;
    }
    if(!return_holding_) {
        return_holding_ = true;
        return_since_ms_ = now_ms;
        return;
    }
    if(now_ms - return_since_ms_ >= kReturnHoldMs)
        EnterMode(Mode::Select, now_ms);
}

void Manager::Step(uint32_t now_ms, uint16_t keys, bool card_present) {
    last_keys_ = keys;
    // Card state: scan after insertion settles, clear immediately after removal.
    if(!card_seen_) {
        card_seen_ = true;
        card_present_ = card_present;
        card_since_ms_ = now_ms;
        scan_due_ = true;
    } else if(card_present != card_present_) {
        card_present_ = card_present;
        card_since_ms_ = now_ms;
        if(card_present)
            scan_due_ = true;
        else if(sd_supported_) {
            storage_.Unmount();
            ClearCatalog(CatalogState::NoCard);
            scan_due_ = false;
        }
    }
    if(mode_ == Mode::Loading) {
        if(loader_.Active() && !loader_.Step(now_ms)) {
            const LoadStatus& st = loader_.Status();
            if(st.state == LoadState::Failed) {
                EnterMode(Mode::Select, now_ms);
                ShowError(st.error == LoadError::CardAbsent ? UiError::NoCard : UiError::Load, now_ms);
            }
            // Committed: the hardware has reset; the host fake just stays here.
        }
        return;
    }
    if(mode_ == Mode::Service)
        return;
    if((scan_due_ && (!card_present_ || now_ms - card_since_ms_ >= kCardSettleMs)) || rescan_pending_)
        ScanCatalog(now_ms);
    if(ui_error_ != UiError::None && static_cast<int32_t>(error_until_ms_ - now_ms) <= 0)
        ui_error_ = UiError::None;
    if(mode_ == Mode::Select)
        HandleSelectKeys(now_ms, keys);
    else
        HandleDiagKeys(now_ms, keys);
}

LedFrame Manager::Leds(uint32_t now_ms) const {
    LedFrame f;
    switch(mode_) {
        case Mode::Diagnostics:
            f.owner_diag = true;
            return f;
        case Mode::Service:
            f.bright = 0xFFFF;
            return f;
        case Mode::Loading: {
            const uint8_t pct = loader_.Percent();
            const int count = 1 + (pct * 15) / 100;
            f.bright = static_cast<uint16_t>((1u << count) - 1u);
            return f;
        }
        case Mode::Select: break;
    }
    if(ui_error_ != UiError::None) {
        // Error flash: all LEDs blink; the affected slot (if any) stays on.
        const bool on = ((now_ms / kErrorBlinkMs) & 1u) == 0;
        f.bright = on ? 0xFFFF : 0;
        if(last_slot_ >= 1 && last_slot_ <= 15)
            f.bright |= static_cast<uint16_t>(1u << (last_slot_ - 1));
        return f;
    }
    if(catalog_state_ == CatalogState::Ok)
        for(uint8_t i = 0; i < catalog_.count; ++i)
            f.dim |= static_cast<uint16_t>(1u << (catalog_.entries[i].slot - 1));
    f.dim |= kDiagKey;  // slot 16: resident diagnostics, always available
    if(!release_gate_ && cycle_keys_ != 0 && cycle_valid_)
        f.bright |= cycle_keys_;  // the key being pressed lights up fully
    return f;
}

} // namespace pm
