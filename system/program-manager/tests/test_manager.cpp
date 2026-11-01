// Manager: modes, key selection, catalog scan on card events, host start orders,
// generation/duplicate protection and the LED display.
#include "check.h"
#include "fakes/fake_storage.h"
#include "pm_manager.h"
#include <string>

using namespace pm;
using namespace fakes;

namespace {

std::string HexOf(const uint8_t* d, size_t n) {
    static const char* hx = "0123456789abcdef";
    std::string s;
    for(size_t i = 0; i < n; ++i) {
        s += hx[d[i] >> 4];
        s += hx[d[i] & 15];
    }
    return s;
}

struct Rig {
    FakeStorage storage;
    FakeTarget target;
    FakeCommit commit;
    std::vector<std::string> log;
    Manager manager;
    ImageHeader toy, reverb;
    uint32_t now = 10000;

    explicit Rig(bool sd = true, bool card = true) : manager(storage, target, commit, sd) {
        target.log = &log;
        commit.log = &log;
        storage.source.card_present = card;
        storage.source.files["PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy"].bytes = MakePackage("toy-piano", "0.4.0", 20000, &toy);
        storage.source.files["PROGRAMS/reverb-fx/AmbientReverb-v0.3.1.dsy"].bytes = MakePackage("reverb-fx", "0.3.1", 3000, &reverb);
        storage.catalog = "{\"format\":1,\"generation\":5,\"hardware\":\"seed3-desktop\",\"programs\":["
                          "{\"slot\":1,\"id\":\"toy-piano\",\"name\":\"Toy Piano\",\"category\":\"instrument\",\"version\":\"0.4.0\","
                          "\"path\":\"PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy\",\"payload_sha256\":\"" + HexOf(toy.payload_sha256, 32) + "\"},"
                          "{\"slot\":3,\"id\":\"reverb-fx\",\"name\":\"Ambient Reverb\",\"category\":\"effect\",\"version\":\"0.3.1\","
                          "\"path\":\"PROGRAMS/reverb-fx/AmbientReverb-v0.3.1.dsy\",\"payload_sha256\":\"" + HexOf(reverb.payload_sha256, 32) + "\"},"
                          "{\"slot\":7,\"id\":\"ghost\",\"name\":\"Missing file\",\"version\":\"1\","
                          "\"path\":\"PROGRAMS/ghost/ghost.dsy\",\"payload_sha256\":\"" + std::string(64, '0') + "\"}"
                          "]}";
    }
    // Advances `ms` milliseconds in 1 ms steps with the given keys.
    void Run(uint32_t ms, uint16_t keys = 0) {
        for(uint32_t i = 0; i < ms; ++i) {
            ++now;
            manager.Step(now, keys, storage.source.card_present);
        }
    }
    void Boot() { Run(Manager::kCardSettleMs + 2); }   // first step + settle -> catalog scanned
    void Press(uint16_t keys, uint32_t hold_ms = 50) {
        Run(hold_ms, keys);
        Run(2, 0);
    }
};

} // namespace

int main() {
    {   // Boot with a card: catalog scanned once after the settle time, LEDs show slots.
        Rig rig;
        rig.manager.Step(rig.now, 0, true);
        CHECK(rig.manager.catalog_state() == CatalogState::NoCard);  // not yet scanned
        CHECK_EQ(rig.storage.catalog_reads, 0);
        rig.Run(Manager::kCardSettleMs - 10);
        CHECK_EQ(rig.storage.catalog_reads, 0);
        rig.Run(20);
        CHECK_EQ(rig.storage.catalog_reads, 1);
        CHECK(rig.manager.catalog_state() == CatalogState::Ok);
        CHECK_EQ(rig.manager.catalog().count, 3);
        CHECK_EQ(rig.manager.generation(), 1u);
        CHECK(rig.manager.mode() == Mode::Select);
        const LedFrame leds = rig.manager.Leds(rig.now);
        CHECK_EQ(leds.dim, (1u << 0) | (1u << 2) | (1u << 6) | (1u << 15));
        CHECK_EQ(leds.bright, 0);
        CHECK(!leds.owner_diag);
        CHECK(!rig.manager.root_bin_warning());
    }
    {   // Root .bin warning is surfaced after a scan.
        Rig rig;
        rig.storage.root_bin = true;
        rig.Boot();
        CHECK(rig.manager.root_bin_warning());
    }
    {   // No card at boot, insertion later, removal clears and bumps the generation.
        Rig rig(true, false);
        rig.Boot();
        CHECK(rig.manager.catalog_state() == CatalogState::NoCard);
        CHECK_EQ(rig.storage.catalog_reads, 0);
        const uint32_t gen0 = rig.manager.generation();
        rig.storage.source.card_present = true;
        rig.Run(Manager::kCardSettleMs + 2);
        CHECK(rig.manager.catalog_state() == CatalogState::Ok);
        CHECK(rig.manager.generation() > gen0);
        const uint32_t gen1 = rig.manager.generation();
        rig.storage.source.card_present = false;
        rig.Run(1);
        CHECK(rig.manager.catalog_state() == CatalogState::NoCard);
        CHECK_EQ(rig.manager.catalog().count, 0);
        CHECK(rig.manager.generation() > gen1);
        CHECK(rig.storage.unmounts >= 1);
        CHECK_EQ(rig.manager.Leds(rig.now).dim, Manager::kDiagKey);  // only diagnostics remains
    }
    {   // Catalog problems are classified; a rescan re-reads.
        Rig rig;
        rig.storage.catalog_missing = true;
        rig.Boot();
        CHECK(rig.manager.catalog_state() == CatalogState::Missing);
        CHECK_EQ(rig.manager.catalog_detail(), 4);
        rig.storage.catalog_missing = false;
        rig.storage.catalog = "{bad json";
        rig.manager.RequestRescan();
        rig.Run(1);
        CHECK(rig.manager.catalog_state() == CatalogState::Invalid);
        CHECK(rig.manager.catalog_error() == CatalogError::Syntax);
        rig.storage.catalog = std::string(kCatalogMaxBytes + 1, ' ');
        rig.manager.RequestRescan();
        rig.Run(1);
        CHECK(rig.manager.catalog_state() == CatalogState::Invalid);
        CHECK(rig.manager.catalog_error() == CatalogError::TooLarge);
        rig.storage.mount_fails = true;
        rig.manager.RequestRescan();
        rig.Run(1);
        CHECK(rig.manager.catalog_state() == CatalogState::MountFailed);
        CHECK_EQ(rig.manager.catalog_detail(), 13);
        rig.storage.mount_fails = false;
        rig.storage.read_fails = true;
        rig.manager.RequestRescan();
        rig.Run(1);
        CHECK(rig.manager.catalog_state() == CatalogState::ReadError);
        CHECK_EQ(rig.storage.catalog_reads, 5);
    }
    {   // Key selection: press+release of key 1 starts slot 1 and commits.
        Rig rig;
        rig.Boot();
        rig.Run(5, 1u << 0);
        CHECK_EQ(rig.manager.Leds(rig.now).bright, 1u);  // the pressed key lights fully
        rig.Run(2, 0);
        CHECK(rig.manager.mode() == Mode::Loading);
        rig.Run(50);  // enough steps for 20000 bytes
        CHECK(rig.manager.loader().Status().state == LoadState::Committed);
        CHECK_EQ(rig.commit.commits, 1);
        CHECK_EQ(rig.commit.committed_address, 0x24000000u);
        CHECK(rig.log == std::vector<std::string>({"quiesce", "clean", "commit"}));
        CHECK_EQ(rig.manager.last_slot(), 1);
    }
    {   // Keys held at power-up are ignored until released (release gate).
        Rig rig;
        rig.Run(Manager::kCardSettleMs + 2, 1u << 2);  // key 3 held from the start
        rig.Run(100, 1u << 2);
        CHECK(rig.manager.mode() == Mode::Select);
        rig.Run(2, 0);                                    // release
        CHECK(rig.manager.mode() == Mode::Select);         // no start from the stale press
        rig.Press(1u << 2);
        CHECK(rig.manager.mode() == Mode::Loading);        // a fresh cycle selects
    }
    {   // Chords and long presses never select; a chord's release does nothing.
        Rig rig;
        rig.Boot();
        rig.Run(20, (1u << 0) | (1u << 1));
        rig.Run(2, 0);
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK_EQ(rig.commit.commits, 0);
        rig.Run(20, 1u << 0);
        rig.Run(20, (1u << 0) | (1u << 4));  // second key joins during the press
        rig.Run(20, 1u << 0);
        rig.Run(2, 0);
        CHECK(rig.manager.mode() == Mode::Select);
        rig.Run(Manager::kLongPressMs + 10, 1u << 0);  // long hold
        rig.Run(2, 0);
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK_EQ(rig.storage.source.opens, 0);
    }
    {   // Empty slot: error flash, back to Select, nothing opened.
        Rig rig;
        rig.Boot();
        rig.Press(1u << 1);  // slot 2 is empty
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK(rig.manager.ui_error() == UiError::EmptySlot);
        CHECK_EQ(rig.manager.last_slot(), 2);
        const LedFrame a = rig.manager.Leds(rig.now);
        const LedFrame b = rig.manager.Leds(rig.now + Manager::kErrorBlinkMs);
        CHECK((a.bright & ~(1u << 1)) != (b.bright & ~(1u << 1)));  // blinking
        CHECK((a.bright & (1u << 1)) && (b.bright & (1u << 1)));   // slot LED steady
        rig.Run(Manager::kErrorFlashMs + 5);
        CHECK(rig.manager.ui_error() == UiError::None);
        CHECK_EQ(rig.storage.source.opens, 0);
    }
    {   // Missing program file: load fails, error shown, Select again, no commit.
        Rig rig;
        rig.Boot();
        rig.Run(50, 1u << 6);  // slot 7 -> ghost.dsy
        rig.Run(1, 0);         // release: the load starts in this step
        CHECK(rig.manager.mode() == Mode::Loading);
        rig.Run(5);            // Open() fails in the next step
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK(rig.manager.ui_error() == UiError::Load);
        CHECK(rig.manager.loader().Status().error == LoadError::Open);
        CHECK_EQ(rig.commit.commits, 0);
        // Keys are ignored while the error flashes? No: a new selection is allowed.
        rig.Press(1u << 2);
        CHECK(rig.manager.mode() == Mode::Loading);
        rig.Run(20);
        CHECK_EQ(rig.commit.commits, 1);
    }
    {   // Card removed during a load: back to Select with NoCard, catalog cleared.
        Rig rig;
        rig.Boot();
        rig.storage.source.remove_card_at = kHeaderSize + 5000;
        rig.Press(1u << 0);
        rig.Run(50);
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK(rig.manager.ui_error() == UiError::NoCard);
        CHECK(rig.manager.catalog_state() == CatalogState::NoCard);
        CHECK_EQ(rig.commit.commits, 0);
    }
    {   // Key 16 enters diagnostics; S1+S16 for 3 s returns; keys during the hold do nothing.
        Rig rig;
        rig.Boot();
        rig.Press(Manager::kDiagKey);
        CHECK(rig.manager.mode() == Mode::Diagnostics);
        CHECK(rig.manager.Leds(rig.now).owner_diag);
        rig.Press(1u << 0);  // single keys are diagnostics key tests, no start
        CHECK(rig.manager.mode() == Mode::Diagnostics);
        CHECK_EQ(rig.storage.source.opens, 0);
        rig.Run(Manager::kReturnHoldMs - 100, Manager::kReturnMask);
        CHECK(rig.manager.mode() == Mode::Diagnostics);
        rig.Run(200, Manager::kReturnMask);
        CHECK(rig.manager.mode() == Mode::Select);
        // The still-held return keys must not count as a selection.
        rig.Run(50, Manager::kReturnMask);
        rig.Run(2, 0);
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK_EQ(rig.commit.commits, 0);
        // A third key during the hold aborts the gesture.
        rig.Press(Manager::kDiagKey);
        rig.Run(2000, Manager::kReturnMask);
        rig.Run(10, Manager::kReturnMask | (1u << 3));
        rig.Run(2000, Manager::kReturnMask);
        CHECK(rig.manager.mode() == Mode::Diagnostics);
        rig.Run(1100, Manager::kReturnMask);
        CHECK(rig.manager.mode() == Mode::Select);
    }
    {   // Host mode changes and refusals.
        Rig rig;
        rig.Boot();
        CHECK(rig.manager.SetMode(Mode::Diagnostics, rig.now));
        CHECK(rig.manager.mode() == Mode::Diagnostics);
        CHECK(!rig.manager.SetMode(Mode::Loading, rig.now));
        CHECK(rig.manager.SetMode(Mode::Select, rig.now));
        rig.Press(1u << 0);
        CHECK(rig.manager.mode() == Mode::Loading);
        CHECK(!rig.manager.SetMode(Mode::Diagnostics, rig.now));
        CHECK(!rig.manager.RequestService());
    }
    {   // Host start orders: generation, slot/id, hash prefix, duplicates, busy.
        Rig rig;
        rig.Boot();
        const uint32_t gen = rig.manager.generation();
        const uint8_t* h = rig.toy.payload_sha256;
        CHECK(rig.manager.StartRequest(1, "toy-piano", h, 16, gen + 1, 100, rig.now) == StartResult::Generation);
        CHECK(rig.manager.StartRequest(2, "toy-piano", h, 16, gen, 100, rig.now) == StartResult::NoEntry);
        CHECK(rig.manager.StartRequest(1, "reverb-fx", h, 16, gen, 100, rig.now) == StartResult::NoEntry);
        CHECK(rig.manager.StartRequest(1, nullptr, h, 16, gen, 100, rig.now) == StartResult::NoEntry);
        CHECK(rig.manager.StartRequest(1, "toy-piano", h, 7, gen, 100, rig.now) == StartResult::Hash);   // prefix too short
        CHECK(rig.manager.StartRequest(1, "toy-piano", rig.reverb.payload_sha256, 32, gen, 100, rig.now) == StartResult::Hash);
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK(!rig.manager.has_last_request());
        CHECK(rig.manager.StartRequest(1, "toy-piano", h, 8, gen, 100, rig.now) == StartResult::Accepted);
        CHECK(rig.manager.mode() == Mode::Loading);
        CHECK_EQ(rig.manager.loader().Status().request_id, 100u);
        CHECK(rig.manager.StartRequest(1, "toy-piano", h, 32, gen, 100, rig.now) == StartResult::Duplicate);
        CHECK(rig.manager.StartRequest(3, "reverb-fx", rig.reverb.payload_sha256, 32, gen, 101, rig.now) == StartResult::Busy);
        rig.Run(50);
        CHECK_EQ(rig.commit.commits, 1);
        // Replayed after the (host-visible) restart: still a duplicate, no second load.
        CHECK(rig.manager.StartRequest(1, "toy-piano", h, 32, gen, 100, rig.now) == StartResult::Duplicate);
    }
    {   // A failed host start keeps its request id: a retry needs a new id.
        Rig rig;
        rig.Boot();
        const uint32_t gen = rig.manager.generation();
        uint8_t zero[32] = {};
        CHECK(rig.manager.StartRequest(7, "ghost", zero, 32, gen, 5, rig.now) == StartResult::Accepted);
        rig.Run(5);
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK(rig.manager.loader().Status().error == LoadError::Open);
        CHECK(rig.manager.StartRequest(7, "ghost", zero, 32, gen, 5, rig.now) == StartResult::Duplicate);
        CHECK(rig.manager.StartRequest(7, "ghost", zero, 32, gen, 6, rig.now) == StartResult::Accepted);
    }
    {   // Wrong mode and no catalog.
        Rig rig;
        rig.Boot();
        const uint32_t gen = rig.manager.generation();
        rig.manager.SetMode(Mode::Diagnostics, rig.now);
        CHECK(rig.manager.StartRequest(1, "toy-piano", rig.toy.payload_sha256, 32, gen, 1, rig.now) == StartResult::WrongMode);
        rig.manager.SetMode(Mode::Select, rig.now);
        rig.storage.source.card_present = false;
        rig.Run(1);
        CHECK(rig.manager.StartRequest(1, "toy-piano", rig.toy.payload_sha256, 32, gen, 1, rig.now) == StartResult::NoCatalog);
        CHECK(rig.manager.StartRequest(1, "toy-piano", rig.toy.payload_sha256, 32, rig.manager.generation(), 1, rig.now) == StartResult::NoCatalog);
        // Key 1 with no card: NoCard error, nothing opened.
        rig.Press(1u << 0);
        CHECK(rig.manager.ui_error() == UiError::NoCard);
        CHECK_EQ(rig.storage.source.opens, 0);
    }
    {   // Service request: all LEDs on, no more key handling, no start possible.
        Rig rig;
        rig.Boot();
        CHECK(rig.manager.RequestService());
        CHECK(rig.manager.mode() == Mode::Service);
        CHECK_EQ(rig.manager.Leds(rig.now).bright, 0xFFFF);
        rig.Press(1u << 0);
        CHECK(rig.manager.mode() == Mode::Service);
        CHECK(rig.manager.StartRequest(1, "toy-piano", rig.toy.payload_sha256, 32, rig.manager.generation(), 9, rig.now) == StartResult::WrongMode);
    }
    {   // Probe profile: no SD, no catalog, no starts; diagnostics still reachable.
        Rig rig(false, true);
        rig.Boot();
        CHECK(rig.manager.catalog_state() == CatalogState::Unsupported);
        CHECK_EQ(rig.storage.catalog_reads, 0);
        CHECK(rig.manager.StartRequest(1, "toy-piano", rig.toy.payload_sha256, 32, rig.manager.generation(), 1, rig.now) == StartResult::Unsupported);
        rig.Press(1u << 0);
        CHECK(rig.manager.mode() == Mode::Select);
        CHECK(rig.manager.ui_error() == UiError::Catalog);
        CHECK_EQ(rig.storage.source.opens, 0);
        rig.Press(Manager::kDiagKey);
        CHECK(rig.manager.mode() == Mode::Diagnostics);
        CHECK_EQ(rig.manager.Leds(rig.now).dim, 0);  // nothing while diagnostics owns the LEDs
    }
    {   // Loading LEDs grow with progress.
        Rig rig;
        rig.Boot();
        rig.Press(1u << 0);
        CHECK_EQ(rig.manager.Leds(rig.now).bright, 1u);
        rig.Run(4);  // open, header, 2 chunks
        const uint16_t mid = rig.manager.Leds(rig.now).bright;
        CHECK(mid > 1u && mid < 0xFFFF);
    }
    CHECK_STREQ(ModeName(Mode::Diagnostics), "DIAG");
    CHECK_STREQ(CatalogStateName(CatalogState::Invalid), "INVALID");
    CHECK_STREQ(StartResultName(StartResult::Generation), "GENERATION");
    return check::Finish("test_manager");
}
