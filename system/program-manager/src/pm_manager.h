// Program manager core: operating modes, key selection, LED display, catalog scan
// and the hand-over to the loader. Portable (host-tested); main.cpp supplies the
// storage, target and boot commit, and feeds keys/card state once per main-loop step.
//
// Modes (docs/CONTRACT.md, section 8):
//   Select      keys 1..15 choose a catalog slot, key 16 opens the resident diagnostics
//   Diagnostics the diagnostics layer owns keys/LEDs; S1+S16 held 3 s returns to Select
//   Loading     a package is being checked and copied; keys are ignored
//   Service     a bootloader (DFU) entry was requested; the caller performs the reset
#pragma once
#include "pm_catalog.h"
#include "pm_loader.h"

namespace pm {

// Card access beyond single program files: catalog and root listing. Main loop only.
class Storage : public ImageSource {
  public:
    enum class CatalogResult : uint8_t { Ok, NoCard, MountFailed, Missing, TooLarge, ReadError };
    // Mounts the card if necessary and reads /DAISY/catalog.json into buf[cap].
    // `len` receives the byte count; `detail` an implementation code (FRESULT).
    virtual CatalogResult ReadCatalog(char* buf, uint32_t cap, uint32_t& len, int& detail) = 0;
    // True when a root directory entry contains ".bin" in any case (the stock
    // bootloader would flash it before this manager even starts; README/Kartenregel).
    virtual bool RootHasBinFile() = 0;
    // Releases the card (after removal or before a rescan).
    virtual void Unmount() = 0;

  protected:
    ~Storage() = default;
};

enum class Mode : uint8_t { Select, Diagnostics, Loading, Service };
const char* ModeName(Mode mode);

enum class CatalogState : uint8_t { NoCard, Scanning, Ok, Missing, Invalid, MountFailed, ReadError, Unsupported };
const char* CatalogStateName(CatalogState state);

// What the panel LEDs should show. `owner_diag` means the diagnostics layer drives them.
struct LedFrame {
    uint16_t dim = 0;     // bit i = LED i+1 at low brightness
    uint16_t bright = 0;  // bit i = LED i+1 at full brightness
    bool owner_diag = false;
};

enum class StartResult : uint8_t {
    Accepted = 0,
    Duplicate,      // same request id as the last accepted request: not started again
    WrongMode,      // only in Select
    Busy,           // a load is active
    NoCatalog,      // no valid catalog (no card, missing, invalid)
    Generation,     // catalog generation does not match the current one
    NoEntry,        // slot empty or id differs from the slot's entry
    Hash,           // expected hash (prefix) differs from the catalog entry
    Unsupported,    // probe profile: no SD start
};
const char* StartResultName(StartResult result);

enum class UiError : uint8_t { None = 0, EmptySlot, NoCard, Catalog, Load };

class Manager {
  public:
    static constexpr uint16_t kDiagKey = 1u << 15;                 // S16
    static constexpr uint16_t kReturnMask = (1u << 0) | (1u << 15);  // S1+S16
    static constexpr uint32_t kReturnHoldMs = 3000;
    static constexpr uint32_t kLongPressMs = 3000;   // a held key is no selection
    static constexpr uint32_t kCardSettleMs = 300;   // after insertion, before mounting
    static constexpr uint32_t kErrorFlashMs = 1500;
    static constexpr uint32_t kErrorBlinkMs = 125;

    // `sd_supported` is false in the probe profile: no catalog, no starts.
    Manager(Storage& storage, LoadTarget& target, BootCommit& commit, bool sd_supported);

    // One main-loop step. `keys` is the debounced held-key mask (bit 0 = S1).
    void Step(uint32_t now_ms, uint16_t keys, bool card_present);

    Mode mode() const { return mode_; }
    // Host-initiated mode change between Select and Diagnostics. False while loading/service.
    bool SetMode(Mode mode, uint32_t now_ms);
    // Re-read the catalog at the next step (new generation).
    void RequestRescan();
    // Bootloader/DFU service: the caller flushes its replies and resets. False while loading.
    bool RequestService();

    // Host start order. `hash` is a prefix (8..32 bytes) of the expected payload hash.
    StartResult StartRequest(uint8_t slot, const char* id, const uint8_t* hash, size_t hash_len,
                             uint32_t generation, uint32_t request_id, uint32_t now_ms);

    const Catalog& catalog() const { return catalog_; }
    CatalogState catalog_state() const { return catalog_state_; }
    CatalogError catalog_error() const { return catalog_error_; }
    int catalog_detail() const { return catalog_detail_; }
    uint32_t generation() const { return generation_; }
    bool card_present() const { return card_present_; }
    bool root_bin_warning() const { return root_bin_; }
    const Loader& loader() const { return loader_; }
    UiError ui_error() const { return ui_error_; }
    // Slot that was last started or failed (for status frames), 0 = none.
    uint8_t last_slot() const { return last_slot_; }
    uint32_t last_request_id() const { return last_rid_; }
    bool has_last_request() const { return has_last_rid_; }
    bool sd_supported() const { return sd_supported_; }

    LedFrame Leds(uint32_t now_ms) const;

  private:
    void ScanCatalog(uint32_t now_ms);
    void ClearCatalog(CatalogState state);
    void HandleSelectKeys(uint32_t now_ms, uint16_t keys);
    void HandleDiagKeys(uint32_t now_ms, uint16_t keys);
    void EnterMode(Mode mode, uint32_t now_ms);
    void StartSlot(uint8_t slot, uint32_t request_id, uint32_t now_ms);
    void ShowError(UiError error, uint32_t now_ms);

    Storage& storage_;
    Loader loader_;
    bool sd_supported_;
    Mode mode_ = Mode::Select;

    Catalog catalog_;
    CatalogState catalog_state_ = CatalogState::NoCard;
    CatalogError catalog_error_ = CatalogError::None;
    int catalog_detail_ = 0;
    uint32_t generation_ = 0;
    bool root_bin_ = false;
    bool card_present_ = false;
    bool card_seen_ = false;        // first Step() happened
    uint32_t card_since_ms_ = 0;
    bool rescan_pending_ = false;
    bool scan_due_ = false;

    // Key cycle in Select: one key pressed and released alone selects.
    uint16_t last_keys_ = 0;        // keys of the latest Step()
    bool release_gate_ = true;      // wait for all keys up after init or a mode change with keys down
    uint16_t cycle_keys_ = 0;       // keys seen during the current press
    uint32_t cycle_since_ms_ = 0;
    bool cycle_valid_ = false;
    // Return gesture in Diagnostics.
    bool return_holding_ = false;
    uint32_t return_since_ms_ = 0;

    UiError ui_error_ = UiError::None;
    uint32_t error_until_ms_ = 0;
    uint8_t last_slot_ = 0;
    uint32_t last_rid_ = 0;
    bool has_last_rid_ = false;

    char catalog_text_[kCatalogMaxBytes + 1];
};

} // namespace pm
