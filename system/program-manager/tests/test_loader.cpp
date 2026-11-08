// Loader state machine: success path, commit order and every failure keeps JUMP unset.
#include "check.h"
#include "fakes/fake_platform.h"
#include "pm_loader.h"

using namespace pm;
using namespace fakes;

namespace {

struct Rig {
    FakeSource source;
    FakeTarget target;
    FakeCommit commit;
    std::vector<std::string> log;
    Loader loader{source, target, commit};
    ImageHeader header;
    CatalogEntry entry;
    uint32_t now = 1000;

    explicit Rig(uint32_t payload_len = 10000, const char* product = "toy-piano") {
        target.log = &log;
        commit.log = &log;
        source.files["PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy"].bytes = MakePackage(product, "0.4.0", payload_len, &header);
        entry = MakeEntry(1, "toy-piano", "PROGRAMS/toy-piano/ToyPiano-v0.4.0.dsy", header);
    }
    // Runs until the loader stops; returns the number of steps.
    int Run(uint32_t max_steps = 100000) {
        int steps = 0;
        while(loader.Active() && steps < static_cast<int>(max_steps)) {
            loader.Step(now);
            ++steps;
        }
        return steps;
    }
    std::vector<uint8_t>& File() { return source.files.begin()->second.bytes; }
};

void ExpectFailure(Rig& rig, LoadError error, const char* what) {
    rig.Run();
    const bool ok = rig.loader.Status().state == LoadState::Failed && rig.loader.Status().error == error
                    && rig.commit.commits == 0 && rig.target.cleans == 0 && !rig.source.is_open;
    if(!ok)
        std::fprintf(stderr, "  case %s: state=%s error=%s commits=%d cleans=%d open=%d\n", what,
                     LoadStateName(rig.loader.Status().state), LoadErrorName(rig.loader.Status().error),
                     rig.commit.commits, rig.target.cleans, rig.source.is_open ? 1 : 0);
    CHECK(ok);
}

} // namespace

int main() {
    {   // Success: payload lands at the target base, rest zeroed, order quiesce-clean-commit.
        Rig rig(10000);
        CHECK(rig.loader.Start(rig.entry, rig.now, 42));
        CHECK(rig.loader.Active());
        CHECK_EQ(rig.loader.Percent(), 0);
        const int steps = rig.Run();
        CHECK(steps >= 3 + 3 + 1);  // open, header, 3 chunks (4096+4096+1808), eof probe, verify, finalize
        CHECK(rig.loader.Status().state == LoadState::Committed);
        CHECK(rig.loader.Status().error == LoadError::None);
        CHECK_EQ(rig.loader.Percent(), 100);
        CHECK_EQ(rig.commit.commits, 1);
        CHECK_EQ(rig.commit.committed_address, 0x24000000u);
        CHECK_EQ(rig.commit.quiesces, 1);
        CHECK_EQ(rig.target.cleans, 1);
        CHECK_EQ(rig.target.cleaned_len, kTargetSize);
        CHECK(rig.log == std::vector<std::string>({"quiesce", "clean", "commit"}));
        CHECK(std::memcmp(rig.target.Base(), rig.File().data() + kHeaderSize, 10000) == 0);
        bool zero = true;
        for(uint32_t i = 10000; i < kTargetSize; ++i)
            zero = zero && rig.target.Base()[i] == 0;
        CHECK(zero);
        CHECK_EQ(rig.source.opens, 1);
        CHECK_EQ(rig.source.closes, 1);
        CHECK(!rig.source.is_open);
        CHECK_EQ(rig.loader.Status().request_id, 42u);
        CHECK_EQ(rig.loader.Status().slot, 1);
        CHECK(!rig.loader.Active());
        // After Committed a new Start is possible on the host (hardware never returns).
        CHECK(rig.loader.Start(rig.entry, rig.now, 43));
    }
    {   // Odd partial reads (7 bytes at a time) and a small payload (entry 0x100 inside 300 bytes).
        Rig rig(300);
        rig.source.max_read = 7;
        CHECK(rig.loader.Start(rig.entry, rig.now, 1));
        rig.Run(100000);
        CHECK(rig.loader.Status().state == LoadState::Committed);
        CHECK_EQ(rig.commit.commits, 1);
    }
    {   // Largest allowed payload: exactly 480 KiB.
        Rig rig(kTargetSize);
        CHECK(rig.loader.Start(rig.entry, rig.now, 1));
        rig.Run();
        CHECK(rig.loader.Status().state == LoadState::Committed);
        CHECK(std::memcmp(rig.target.Base(), rig.File().data() + kHeaderSize, kTargetSize) == 0);
    }
    {   // Progress reporting.
        Rig rig(40960);
        rig.loader.Start(rig.entry, rig.now, 1);
        rig.loader.Step(rig.now);  // open
        rig.loader.Step(rig.now);  // header
        CHECK(rig.loader.Status().state == LoadState::Payload);
        for(int i = 0; i < 5; ++i)
            rig.loader.Step(rig.now);
        CHECK_EQ(rig.loader.Status().bytes_done, 5u * 4096u);
        CHECK_EQ(rig.loader.Percent(), 49);
        CHECK(rig.loader.Active());
    }
    {   // Busy: a second Start while active is refused and leaves the load untouched.
        Rig rig(10000);
        CHECK(rig.loader.Start(rig.entry, rig.now, 1));
        rig.loader.Step(rig.now);
        CHECK(!rig.loader.Start(rig.entry, rig.now, 2));
        CHECK(rig.loader.Status().error == LoadError::Busy);
        CHECK_EQ(rig.loader.Status().request_id, 1u);
        rig.Run();
        CHECK(rig.loader.Status().state == LoadState::Committed);
    }
    {   // Cancel mid-load: source closed, no commit, Reset returns to Idle.
        Rig rig(40960);
        rig.loader.Start(rig.entry, rig.now, 1);
        for(int i = 0; i < 4; ++i)
            rig.loader.Step(rig.now);
        CHECK(rig.source.is_open);
        rig.loader.Cancel();
        CHECK(rig.loader.Status().state == LoadState::Failed);
        CHECK(rig.loader.Status().error == LoadError::Cancelled);
        CHECK(!rig.source.is_open);
        CHECK_EQ(rig.commit.commits, 0);
        rig.loader.Reset();
        CHECK(rig.loader.Status().state == LoadState::Idle);
        CHECK(rig.loader.Status().error == LoadError::None);
    }
    {   // No card at start.
        Rig rig;
        rig.source.card_present = false;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::CardAbsent, "no card");
        CHECK_EQ(rig.source.opens, 0);
    }
    {   // Card removed during the payload.
        Rig rig(40960);
        rig.source.remove_card_at = 20000;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::CardAbsent, "removed");
        CHECK(rig.loader.Status().bytes_done > 0);
    }
    {   // Missing file.
        Rig rig;
        std::strcpy(rig.entry.path, "PROGRAMS/toy-piano/missing.dsy");
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Open, "missing");
    }
    {   // File too small to hold a header and the minimal payload.
        Rig rig;
        rig.File().resize(kHeaderSize + 7);
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::FileSize, "tiny");
        CHECK_EQ(rig.source.reads, 0);
    }
    {   // File larger than header + 480 KiB.
        Rig rig;
        rig.File().resize(kHeaderSize + kTargetSize + 1);
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::FileSize, "huge");
    }
    {   // Header unreadable (read error on the first bytes).
        Rig rig;
        rig.source.fail_read_at = 0;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::HeaderRead, "header read");
    }
    {   // Header read error after a partial header.
        Rig rig;
        rig.source.max_read = 100;
        rig.source.fail_read_at = 100;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::HeaderRead, "header partial");
    }
    {   // Corrupted header (CRC).
        Rig rig;
        rig.File()[120] ^= 0x40;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Header, "header crc");
        CHECK(rig.loader.Status().image_error == ImageError::Crc);
    }
    {   // Wrong profile in a valid header.
        Rig rig;
        ImageHeader h = rig.header;
        std::strcpy(h.profile, "qspi-xip-v1");
        BuildHeader(h, rig.File().data());
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Header, "profile");
        CHECK(rig.loader.Status().image_error == ImageError::Profile);
    }
    {   // Product id differs from the catalog entry.
        Rig rig(10000, "reverb-fx");  // package says reverb-fx, catalog says toy-piano
        std::memcpy(rig.entry.payload_sha256, rig.header.payload_sha256, 32);
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Product, "product");
    }
    {   // Catalog hash differs from the header hash (stale catalog).
        Rig rig;
        rig.entry.payload_sha256[3] ^= 1;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::HashMismatch, "catalog hash");
        CHECK_EQ(rig.loader.Status().bytes_done, 0u);  // nothing of the payload touched
    }
    {   // Reported file size does not match the header's payload length.
        Rig rig;
        rig.source.reported_size_delta = 16;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::FileSize, "size lie");
    }
    {   // Payload read error after some chunks.
        Rig rig(40960);
        rig.source.fail_read_at = kHeaderSize + 9000;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Read, "payload read");
        CHECK_EQ(rig.loader.Status().bytes_done, 9000u);  // bytes before the fault were taken
    }
    {   // File ends early although Open() reported the full size.
        Rig rig(40960);
        rig.source.truncate_at = kHeaderSize + 30000;
        rig.source.reported_size_delta = 0;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Short, "short");
        CHECK_EQ(rig.loader.Status().bytes_done, 30000u);
    }
    {   // Trailing bytes beyond the declared payload (size reported too small by the header).
        Rig rig(10000);
        rig.File().push_back(0x55);   // file is now 1 byte longer than header says
        rig.source.reported_size_delta = 0xFFFFFFFFu;  // Open() reports the header-consistent size
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Long, "long");
    }
    {   // Payload bit flip: hash fails after the full read; the target holds the bad
        // image but nothing is committed or cleaned.
        Rig rig(10000);
        rig.File()[kHeaderSize + 5000] ^= 0x01;
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Hash, "payload hash");
        CHECK_EQ(rig.loader.Status().bytes_done, 10000u);
    }
    {   // Bad vectors with a consistent hash: SP outside DTCM.
        Rig rig(10000);
        std::vector<uint8_t> payload(rig.File().begin() + kHeaderSize, rig.File().end());
        payload[3] = 0x24;  // SP in AXI
        ImageHeader h = rig.header;
        pm_sha256_digest(payload.data(), payload.size(), h.payload_sha256);
        std::vector<uint8_t> file(kHeaderSize);
        BuildHeader(h, file.data());
        file.insert(file.end(), payload.begin(), payload.end());
        rig.File() = file;
        std::memcpy(rig.entry.payload_sha256, h.payload_sha256, 32);
        rig.loader.Start(rig.entry, rig.now, 1);
        ExpectFailure(rig, LoadError::Vector, "vectors");
        CHECK(rig.loader.Status().vector_error == VectorError::Stack);
    }
    {   // Time budget: the clock jumps past kBudgetMs during the payload.
        Rig rig(40960);
        rig.loader.Start(rig.entry, rig.now, 1);
        rig.loader.Step(rig.now);
        rig.loader.Step(rig.now);
        rig.loader.Step(rig.now);
        rig.now += Loader::kBudgetMs + 1;
        ExpectFailure(rig, LoadError::Budget, "budget");
    }
    {   // Step without Start does nothing.
        Rig rig;
        CHECK(!rig.loader.Step(rig.now));
        CHECK(rig.loader.Status().state == LoadState::Idle);
        CHECK_EQ(rig.source.opens, 0);
    }
    CHECK_STREQ(LoadStateName(LoadState::Committed), "RESTARTING");
    CHECK_STREQ(LoadErrorName(LoadError::HashMismatch), "CATHASH");
    return check::Finish("test_loader");
}
