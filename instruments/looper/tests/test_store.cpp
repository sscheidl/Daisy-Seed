// SPDX-License-Identifier: MIT
// Persistence and import against an in-memory card: delayed save, abort on
// change, atomic replace, recall after power-up, import order and validation,
// card faults. The engine runs in the same thread; the port answers like the
// audio callback does, optionally with a delay.
#include "loop_store.h"
#include "memory_volume.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace looper;

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)
static int reported = 0;
#define PASS(...)                                                                     \
    do {                                                                              \
        std::printf(failures == reported ? "PASS " : "FAIL ");                         \
        std::printf(__VA_ARGS__);                                                     \
        reported = failures;                                                          \
    } while (0)

constexpr uint32_t kSecond = 48000;
constexpr const char* kLoop = "0:/looper/loop.wav";
constexpr const char* kTemp = "0:/looper/loop.tmp";

struct View : EngineView {
    Engine& e;
    int changeAfter = -1;  // bump the version after this many calls (torn copy)
    uint32_t extra = 0;
    // Tear: once the temporary file holds tearBytes, the next snapshot is
    // taken right before the last chunk is copied. The audio thread writes into
    // the loop at that moment and publishes the new version only afterwards, so
    // this snapshot still shows the old one and the copy is torn.
    const MemoryVolume* card = nullptr;
    size_t tearBytes = 0;
    bool teared = false, bump = false;
    uint32_t tearFrame = 0;
    explicit View(Engine& engine) : e(engine) {}
    EngineSnapshot Get() override {
        if (changeAfter >= 0 && changeAfter-- == 0)
            extra = 1000;
        if (bump) {
            extra += 1;
            bump = false;
        }
        if (card && tearBytes && card->files.count("0:/looper/loop.tmp") &&
            card->files.at("0:/looper/loop.tmp").size() == tearBytes) {
            const_cast<Frame*>(e.ActiveFrames())[tearFrame].left += .125f;
            teared = bump = true;
            tearBytes = 0;
        }
        return {e.ContentVersion() + extra, !e.Busy(), e.ActiveFrames(), e.Length(), e.TailLength()};
    }
};
struct Port : ImportPort {
    Engine& e;
    int delay = 0, wait = 0;
    Reply reply = Reply::Idle;
    Frame* buffer = nullptr;
    enum class Pending { None, Begin, Commit } pending = Pending::None;
    uint32_t a = 0, b = 0;
    explicit Port(Engine& engine) : e(engine) {}
    void Begin(uint32_t frames) override { Request(Pending::Begin, frames, 0); }
    void Commit(uint32_t length, uint32_t tail) override { Request(Pending::Commit, length, tail); }
    void Cancel() override {
        e.CancelImport();
        reply = Reply::Idle;
        pending = Pending::None;
    }
    Reply Poll(Frame*& out) override {
        if (pending != Pending::None && wait-- <= 0) {  // the next audio callback
            if (pending == Pending::Begin) {
                buffer = e.BeginImport(a);
                reply = buffer ? Reply::Granted : Reply::Denied;
            } else {
                reply = e.CommitImport(a, b) ? Reply::Done : Reply::Failed;
            }
            pending = Pending::None;
        }
        out = buffer;
        return reply;
    }
    void Request(Pending p, uint32_t x, uint32_t y) {
        pending = p;
        a = x;
        b = y;
        wait = delay;
        reply = Reply::Pending;
    }
};

struct Rig {
    uint32_t capacity;
    std::vector<Frame> a, b;
    Engine engine;
    std::vector<uint8_t> transfer = std::vector<uint8_t>(8192);
    std::vector<float> table = std::vector<float>(resample::kTableSize);
    LoopStore store;
    View view{engine};
    Port port{engine};
    MemoryVolume& card;
    uint32_t now = 0;
    uint64_t t = 0;

    Rig(MemoryVolume& volume, uint32_t frames, bool resampling = true)
        : capacity(frames), a(frames), b(frames), card(volume) {
        CHECK(engine.Init(48000.f, a.data(), b.data(), frames));
        CHECK(store.Init(&card, transfer.data(), uint32_t(transfer.size()), frames, "0:/looper"));
        if (resampling)
            CHECK(store.EnableResampling(table.data(), uint32_t(table.size())));
    }
    // One 48-frame callback and one main-loop pass per millisecond.
    void Run(uint32_t ms, float level = 0.f, double hz = 220.0) {
        float in[48], out[48];
        for (uint32_t m = 0; m < ms; ++m) {
            for (int i = 0; i < 48; ++i, ++t)
                in[i] = level * float(std::sin(2.0 * 3.14159265358979 * hz * double(t) / 48000.0));
            engine.Process(in, in, out, out, 48);
            store.Service(view, port, now++);
        }
    }
};

static std::vector<Frame> Snapshot(const Engine& e) {
    std::vector<Frame> s(e.Length() + e.TailLength());
    for (uint32_t i = 0; i < s.size(); ++i)
        s[i] = e.StoredFrame(i);
    return s;
}
static bool SameFrames(const std::vector<Frame>& x, const std::vector<Frame>& y) {
    return x.size() == y.size() &&
           (x.empty() || std::memcmp(x.data(), y.data(), x.size() * sizeof(Frame)) == 0);
}
// Frames stored in a file written by the looper (float32 stereo after the header).
static bool FileHolds(MemoryVolume& card, const char* path, const std::vector<Frame>& frames,
                      uint32_t length, uint32_t tail) {
    if (!card.files.count(path))
        return false;
    const auto& f = card.files[path];
    if (f.size() != wav::kHeaderBytes + frames.size() * 8)
        return false;
    uint32_t l, tl;
    std::memcpy(&l, f.data() + 12 + 24 + 12, 4);
    std::memcpy(&tl, f.data() + 12 + 24 + 16, 4);
    return l == length && tl == tail &&
           std::memcmp(f.data() + wav::kHeaderBytes, frames.data(), frames.size() * 8) == 0;
}
static void RunUntilSaved(Rig& r, uint32_t saves, uint32_t limit = 60000) {
    for (uint32_t m = 0; m < limit && r.store.Saves() < saves; ++m)
        r.Run(1);
}

static void TestSaveAndRecall() {
    MemoryVolume card;
    auto rig = std::make_unique<Rig>(card, 4 * kSecond);
    Rig& r = *rig;
    // Nothing to save while empty.
    r.Run(3000);
    CHECK(r.store.Saves() == 0 && !card.files.count(kLoop));
    // First take; no save while recording or before the delay.
    r.engine.PressRecord();
    r.Run(1000, .5f);
    r.engine.PressRecord();
    r.Run(500, .5f);
    CHECK(r.store.Saves() == 0 && !r.store.Active());
    RunUntilSaved(r, 1);
    CHECK(r.store.Saves() == 1);
    const auto take = Snapshot(r.engine);
    CHECK(FileHolds(card, kLoop, take, r.engine.Length(), r.engine.TailLength()));
    CHECK(r.engine.TailLength() == kTailFrames && !card.files.count(kTemp));
    // No further save without a change.
    r.Run(5000);
    CHECK(r.store.Saves() == 1);
    // A change starts a new save only after the engine is idle for 2 s.
    r.engine.PressRecord();
    r.Run(300, .3f, 330.0);
    CHECK(r.store.Saves() == 1 && !r.store.Active());
    r.engine.PressRecord();
    RunUntilSaved(r, 2);
    const auto layer = Snapshot(r.engine);
    CHECK(FileHolds(card, kLoop, layer, r.engine.Length(), r.engine.TailLength()));
    // Undo is persisted too.
    r.engine.Undo();
    RunUntilSaved(r, 3);
    CHECK(FileHolds(card, kLoop, take, r.engine.Length(), r.engine.TailLength()));
    r.engine.Redo();
    RunUntilSaved(r, 4);

    // Power-up: a new engine recalls the loop exactly, not undoable, no re-save.
    auto again = std::make_unique<Rig>(card, 4 * kSecond);
    CHECK(again->store.Recall(again->engine));
    CHECK(again->engine.GetState() == State::Looping && !again->engine.CanUndo());
    CHECK(SameFrames(Snapshot(again->engine), layer));
    again->Run(5000);
    CHECK(again->store.Saves() == 0);

    // Erase removes the file, so the next power-up starts empty.
    r.engine.Erase();
    for (int m = 0; m < 5000 && card.files.count(kLoop); ++m)
        r.Run(1);
    CHECK(!card.files.count(kLoop));
    auto empty = std::make_unique<Rig>(card, 4 * kSecond);
    CHECK(!empty->store.Recall(empty->engine) && empty->engine.GetState() == State::Empty);
    PASS("save after 2 s idle, only on change, exact file, recall after power-up, erase\n");
}

static void TestAbortAndFaults() {
    MemoryVolume card;
    auto rig = std::make_unique<Rig>(card, 4 * kSecond);
    Rig& r = *rig;
    r.engine.PressRecord();
    r.Run(3000, .5f);
    r.engine.PressRecord();
    RunUntilSaved(r, 1);
    const auto first = Snapshot(r.engine);
    const uint32_t firstLength = r.engine.Length();
    // A layer starting in the middle of a save throws the temporary file away.
    r.engine.PressRecord();
    r.Run(200, .3f, 330.0);
    r.engine.PressRecord();
    for (int m = 0; m < 10000 && !r.store.Active(); ++m)
        r.Run(1);
    CHECK(r.store.Active());
    r.Run(3);  // a few chunks of about 3 s of audio
    CHECK(r.store.Active() && card.files.count(kTemp));
    r.engine.PressRecord();
    r.Run(2);
    CHECK(!r.store.Active() && !card.files.count(kTemp));
    CHECK(FileHolds(card, kLoop, first, firstLength, kTailFrames));  // the last good file
    r.engine.PressRecord();
    RunUntilSaved(r, 2);
    CHECK(FileHolds(card, kLoop, Snapshot(r.engine), r.engine.Length(), r.engine.TailLength()));
    // A change between copying a chunk and writing it (torn copy) also aborts.
    r.engine.Undo();
    for (int m = 0; m < 10000 && !r.store.Active(); ++m)
        r.Run(1);
    r.view.changeAfter = 3;
    r.Run(1);
    CHECK(!r.store.Active() && !card.files.count(kTemp));
    r.view.changeAfter = -1;
    r.view.extra = 0;
    RunUntilSaved(r, 3);
    const auto undone = Snapshot(r.engine);
    CHECK(FileHolds(card, kLoop, undone, r.engine.Length(), r.engine.TailLength()));
    // Write failure: loop.wav stays the last good file, retry after 10 s.
    r.engine.Redo();
    card.failWriteAfter = card.written + 20000;
    for (int m = 0; m < 10000 && r.store.LastFault() != LoopStore::Fault::Write; ++m)
        r.Run(1);
    CHECK(r.store.LastFault() == LoopStore::Fault::Write);
    CHECK(r.store.GetStatus(r.now) == LoopStore::Status::Error);
    CHECK(!card.files.count(kTemp) && FileHolds(card, kLoop, undone, undone.size() - kTailFrames, kTailFrames));
    card.failWriteAfter = -1;
    const uint32_t failedAt = r.now;
    RunUntilSaved(r, 4, 30000);
    CHECK(r.store.Saves() == 4 && r.now - failedAt >= LoopStore::kRetryDelayMs);
    // A failed rename keeps the temporary file, which a power-up recalls.
    r.engine.Undo();
    card.failRename = true;
    for (int m = 0; m < 10000 && r.store.LastFault() != LoopStore::Fault::Write; ++m)
        r.Run(1);
    CHECK(!card.files.count(kLoop) && card.files.count(kTemp));
    card.failRename = false;
    auto boot = std::make_unique<Rig>(card, 4 * kSecond);
    CHECK(boot->store.Recall(boot->engine) && SameFrames(Snapshot(boot->engine), undone));
    // A truncated temporary file is not recalled.
    card.files[kTemp].resize(card.files[kTemp].size() / 2);
    auto broken = std::make_unique<Rig>(card, 4 * kSecond);
    CHECK(!broken->store.Recall(broken->engine) && broken->engine.GetState() == State::Empty);
    // Card pulled during a save: abort, no card status, save once it is back.
    for (int m = 0; m < 20000 && !r.store.Active(); ++m)
        r.Run(1);
    CHECK(r.store.Active());
    card.ready = false;
    r.Run(5);
    CHECK(!r.store.Active() && r.store.GetStatus(r.now + 5000) == LoopStore::Status::NoCard);
    card.ready = true;
    card.open.clear();
    const uint32_t saves = r.store.Saves();
    RunUntilSaved(r, saves + 1, 30000);
    CHECK(r.store.Saves() == saves + 1);
    CHECK(FileHolds(card, kLoop, Snapshot(r.engine), r.engine.Length(), r.engine.TailLength()));
    PASS("save aborts on change and torn copy, write and rename faults, card removal\n");
}

// The audio thread changes the loop while the last chunk is being copied: the
// check after copying must catch it, or loop.wav would hold a torn last chunk.
static void TestTornLastChunk() {
    MemoryVolume card;
    auto rig = std::make_unique<Rig>(card, 2 * kSecond);
    Rig& r = *rig;
    // 100 ms + post-roll = 5760 frames: six chunks of 1024, the last two
    // copied in the same main-loop pass (four chunks per pass).
    r.engine.PressRecord();
    r.Run(100, .5f);
    r.engine.PressRecord();
    RunUntilSaved(r, 1);
    const auto saved = Snapshot(r.engine);
    CHECK(saved.size() == 5760);
    // Same length again: overdub, undo; the save of the undone state is torn.
    r.engine.PressRecord();
    r.Run(100, .3f);
    r.engine.PressRecord();
    RunUntilSaved(r, 2);
    r.engine.Undo();
    for (int m = 0; m < 10000 && !r.store.Active(); ++m)
        r.Run(1);
    const auto before = Snapshot(r.engine);
    CHECK(SameFrames(before, saved));
    const uint32_t perChunk = 8192 / wav::kBytesPerFrame;  // the rig's transfer buffer
    const uint32_t lastChunk = uint32_t(before.size()) % perChunk ? uint32_t(before.size()) % perChunk : perChunk;
    r.view.card = &card;
    r.view.tearBytes = wav::kHeaderBytes + (before.size() - lastChunk) * wav::kBytesPerFrame;
    r.view.tearFrame = uint32_t(before.size()) - 1;  // inside the last chunk
    const uint32_t saves = r.store.Saves();
    for (int m = 0; m < 100 && r.store.Active(); ++m)
        r.Run(1);
    CHECK(r.view.teared);
    // The loop changed during the save: it must be thrown away, and loop.wav
    // still holds the previous complete file.
    CHECK(r.store.Saves() == saves && !card.files.count(kTemp));
    RunUntilSaved(r, saves + 1);
    const auto& final = card.files[kLoop];
    std::vector<Frame> last((final.size() - wav::kHeaderBytes) / 8);
    std::memcpy(last.data(), final.data() + wav::kHeaderBytes, last.size() * 8);
    CHECK(SameFrames(last, Snapshot(r.engine)));
    PASS("a change while the last chunk is copied never reaches loop.wav\n");
}

using Bytes = std::vector<uint8_t>;
static void Put32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
static Bytes Pcm16(uint32_t frames, uint16_t channels, uint32_t rate, int16_t value) {
    Bytes b = {'R', 'I', 'F', 'F'};
    Put32(b, 36 + frames * channels * 2);
    for (char c : std::string("WAVEfmt "))
        b.push_back(uint8_t(c));
    Put32(b, 16);
    b.push_back(1), b.push_back(0), b.push_back(uint8_t(channels)), b.push_back(0);
    Put32(b, rate);
    Put32(b, rate * channels * 2);
    b.push_back(uint8_t(channels * 2)), b.push_back(0), b.push_back(16), b.push_back(0);
    for (char c : std::string("data"))
        b.push_back(uint8_t(c));
    Put32(b, frames * channels * 2);
    for (uint32_t i = 0; i < frames * channels; ++i)
        b.push_back(uint8_t(value)), b.push_back(uint8_t(uint16_t(value) >> 8));
    return b;
}

static void TestImport() {
    MemoryVolume card;
    const uint32_t capacity = 3 * kSecond;
    card.files["0:/looper/loops/b.wav"] = Pcm16(kSecond / 2, 1, 48000, 8192);  // .25 mono
    card.files["0:/looper/loops/A.WAV"] = Pcm16(kSecond / 4, 2, 48000, -16384);
    card.files["0:/looper/loops/B2.wav"] = Pcm16(kSecond / 8, 2, 48000, 2048);
    card.files["0:/looper/loops/c.wav"] = Pcm16(capacity, 2, 48000, 4096);      // exactly full
    // 44.1 kHz, converted: exactly full at 48 kHz, and one input frame too long.
    const uint32_t full44 = capacity * resample::kDown / resample::kUp;
    CHECK(resample::OutputFrames(full44) == capacity && resample::OutputFrames(full44 + 1) > capacity);
    card.files["0:/looper/loops/c2.wav"] = Pcm16(full44, 2, 44100, 4096);
    card.files["0:/looper/loops/d.wav"] = Pcm16(1000, 2, 96000, 100);           // wrong rate
    card.files["0:/looper/loops/e.wav"] = Pcm16(capacity + 1, 1, 48000, 100);   // too long
    card.files["0:/looper/loops/f.wav"] = Pcm16(full44 + 1, 1, 44100, 100);     // too long at 48 kHz
    card.files["0:/looper/loops/.hidden.wav"] = Pcm16(100, 1, 48000, 1);
    card.files["0:/looper/loops/notes.txt"] = Bytes(10, 'x');
    auto rig = std::make_unique<Rig>(card, capacity);
    Rig& r = *rig;
    r.port.delay = 3;  // the audio thread answers a few callbacks later
    auto import = [&] {
        r.store.RequestImport();
        r.Run(1);  // the request is taken up by the next main-loop pass
        for (int m = 0; m < 5000 && (r.store.Active() || r.engine.Busy()); ++m)
            r.Run(1);
        r.Run(20);
    };
    struct Expect {
        const char* name;
        uint32_t length;
        float left, right;
    };
    std::vector<std::vector<Frame>> states;
    for (const Expect x : {Expect{"A.WAV", kSecond / 4, -.5f, -.5f}, Expect{"b.wav", kSecond / 2, .25f, .25f},
                           Expect{"B2.wav", kSecond / 8, .0625f, .0625f},
                           Expect{"c.wav", capacity, .125f, .125f}, Expect{"c2.wav", capacity, .125f, .125f}}) {
        import();
        CHECK(std::strcmp(r.store.LastImportName(), x.name) == 0);
        CHECK(r.store.LastFault() == LoopStore::Fault::None);
        CHECK(r.engine.Length() == x.length && r.engine.TailLength() == 0);
        // Exact for 48 kHz; the 44.1 kHz filter has unity DC gain up to float rounding.
        const Frame mid = r.engine.StoredFrame(x.length / 2), end = r.engine.StoredFrame(x.length - 1);
        CHECK(std::fabs(mid.left - x.left) < 1e-6f && std::fabs(mid.right - x.right) < 1e-6f);
        CHECK(std::fabs(end.left - x.left) < 1e-6f);  // the seam holds the level too
        states.push_back(Snapshot(r.engine));
    }
    // Invalid files are reported and change nothing.
    for (wav::Error e : {wav::Error::SampleRate, wav::Error::TooLong, wav::Error::TooLong}) {
        import();
        CHECK(r.store.LastFault() == LoopStore::Fault::File && r.store.LastFileError() == e);
        CHECK(SameFrames(Snapshot(r.engine), states.back()));
    }
    // The list wraps around; an import is undoable like a layer.
    import();
    CHECK(std::strcmp(r.store.LastImportName(), "A.WAV") == 0 && r.engine.Length() == kSecond / 4);
    r.engine.Undo();
    r.Run(100);
    CHECK(SameFrames(Snapshot(r.engine), states.back()));
    r.engine.Redo();
    r.Run(100);
    // The imported loop is saved and recalled.
    RunUntilSaved(r, 1);
    auto boot = std::make_unique<Rig>(card, capacity);
    CHECK(boot->store.Recall(boot->engine) && SameFrames(Snapshot(boot->engine), states.front()));
    // Busy engine: the import is refused and nothing changes.
    r.engine.PressRecord();
    r.Run(50, .2f);
    const uint32_t version = r.engine.ContentVersion();
    r.store.RequestImport();
    r.Run(20, .2f);
    CHECK(r.store.LastFault() == LoopStore::Fault::Busy && r.engine.Overdubbing());
    CHECK(r.engine.ContentVersion() == version && !r.engine.Importing());
    r.engine.PressRecord();
    // An import while saving replaces the save.
    for (int m = 0; m < 10000 && !r.store.Active(); ++m)
        r.Run(1);
    CHECK(r.store.Active());
    import();
    CHECK(r.store.LastFault() == LoopStore::Fault::None && !card.files.count(kTemp));
    // No files: reported.
    MemoryVolume bare;
    auto none = std::make_unique<Rig>(bare, capacity);
    none->store.RequestImport();
    none->Run(5);
    CHECK(none->store.LastFault() == LoopStore::Fault::NoFiles);
    // Engine side: an import blocks record, undo and erase until it ends.
    Frame* target = r.engine.BeginImport(1000);
    CHECK(target != nullptr && r.engine.Busy() && r.engine.BeginImport(10) == nullptr);
    const auto before = Snapshot(r.engine);
    r.engine.PressRecord();
    r.engine.Undo();
    r.engine.Erase();
    r.Run(50, .3f);
    CHECK(SameFrames(Snapshot(r.engine), before) && !r.engine.Overdubbing());
    CHECK(!r.engine.CommitImport(900, kTailFrames + 1) && !r.engine.Importing());  // rejected
    CHECK(SameFrames(Snapshot(r.engine), before));
    CHECK(r.engine.BeginImport(r.capacity + 1) == nullptr && r.engine.BeginImport(0) == nullptr);
    PASS("import: sorted *.wav, wraps, validates, undoable, saved, refused while busy\n");
}

// 44.1 kHz files are converted to 48 kHz on import and on recall, and refused
// when the firmware provides no conversion table.
static void TestImport44k1() {
    MemoryVolume card;
    const uint32_t capacity = 2 * kSecond;
    card.files["0:/looper/loops/sine.wav"] = Pcm16(44100, 1, 44100, 0);
    // A 441 Hz sine: exactly 441 cycles in the file, 441 cycles at 48 kHz.
    {
        auto& f = card.files["0:/looper/loops/sine.wav"];
        for (uint32_t i = 0; i < 44100; ++i) {
            const int16_t v = int16_t(std::lround(16384.0 * std::sin(2.0 * 3.14159265358979 * 441.0 * i / 44100.0)));
            f[44 + 2 * i] = uint8_t(v);
            f[44 + 2 * i + 1] = uint8_t(uint16_t(v) >> 8);
        }
    }
    auto none = std::make_unique<Rig>(card, capacity, false);
    none->store.RequestImport();
    none->Run(5);
    CHECK(none->store.LastFault() == LoopStore::Fault::File &&
          none->store.LastFileError() == wav::Error::SampleRate && none->engine.Length() == 0);

    auto rig = std::make_unique<Rig>(card, capacity);
    Rig& r = *rig;
    r.store.RequestImport();
    r.Run(1);
    uint32_t passes = 0;
    for (; passes < 5000 && (r.store.Active() || r.engine.Busy()); ++passes)
        r.Run(1);
    r.Run(20);
    CHECK(r.store.LastFault() == LoopStore::Fault::None && r.engine.Length() == kSecond);
    // 44100 input frames at 4096 per main-loop pass: the work is spread out.
    CHECK(passes >= 44100 / LoopStore::kResampleFramesPerService);
    double worst = 0.0;
    for (uint32_t i = 0; i < kSecond; ++i) {
        const double ideal = .5 * std::sin(2.0 * 3.14159265358979 * 441.0 * i / 48000.0);
        worst = std::max(worst, std::fabs(double(r.engine.StoredFrame(i).left) - ideal));
    }
    CHECK(worst < 1e-4);  // 16-bit quantization dominates (3e-5)
    // Saved as a 48 kHz loop and recalled unchanged.
    const auto imported = Snapshot(r.engine);
    RunUntilSaved(r, 1);
    auto boot = std::make_unique<Rig>(card, capacity);
    CHECK(boot->store.Recall(boot->engine) && SameFrames(Snapshot(boot->engine), imported));
    // A 44.1 kHz loop.wav (copied onto the card by hand) is converted on recall.
    card.files["0:/looper/loop.wav"] = card.files["0:/looper/loops/sine.wav"];
    auto hand = std::make_unique<Rig>(card, capacity);
    CHECK(hand->store.Recall(hand->engine) && hand->engine.Length() == kSecond);
    CHECK(SameFrames(Snapshot(hand->engine), imported));
    auto plain = std::make_unique<Rig>(card, capacity, false);
    CHECK(!plain->store.Recall(plain->engine) && plain->engine.Length() == 0);
    PASS("44.1 kHz: converted on import and recall, spread over passes, refused without table (max error %.1e)\n",
         worst);
}

// Before a share (docs/SD_MANAGEMENT.md): SaveSoon skips the 2 s delay but still
// waits for an idle engine and after a failure; Halt stops a save and keeps loop.wav.
static void TestSaveSoonAndHalt() {
    MemoryVolume card;
    auto rig = std::make_unique<Rig>(card, 4 * kSecond);
    Rig& r = *rig;
    r.Run(10);
    CHECK(r.store.VersionKnown() && r.store.SavedVersion() == r.engine.ContentVersion());
    r.engine.PressRecord();
    r.Run(1000, .5f);
    r.store.SaveSoon();  // recording: the engine is not idle, nothing starts
    r.Run(100, .5f);
    CHECK(!r.store.Active() && r.store.Saves() == 0);
    r.engine.PressRecord();
    r.Run(30);  // the take ends with its post-roll
    CHECK(r.store.SavedVersion() != r.engine.ContentVersion());
    uint32_t waited = 0;
    for (; waited < 2000 && r.store.Saves() == 0; ++waited)
        r.Run(1);
    CHECK(r.store.Saves() == 1 && waited < 100);  // well before the 2 s delay
    CHECK(r.store.SavedVersion() == r.engine.ContentVersion() && !r.store.SaveFailed());
    const auto take = Snapshot(r.engine);
    CHECK(FileHolds(card, kLoop, take, r.engine.Length(), r.engine.TailLength()));
    // A request without changes is dropped; the next change waits 2 s again.
    r.store.SaveSoon();
    r.Run(10);
    r.engine.PressRecord();
    r.Run(200, .3f, 330.0);
    r.engine.PressRecord();
    r.Run(500);
    CHECK(r.store.Saves() == 1);
    RunUntilSaved(r, 2);
    // Halt in the middle of a save: loop.wav keeps the previous file, no loop.tmp.
    const auto layer = Snapshot(r.engine);
    r.engine.Undo();
    r.Run(50);
    r.store.SaveSoon();
    for (int m = 0; m < 100 && !r.store.Saving(); ++m)
        r.Run(1);
    CHECK(r.store.Saving() && card.files.count(kTemp));
    r.store.Halt();
    CHECK(!r.store.Active() && !card.files.count(kTemp));
    CHECK(FileHolds(card, kLoop, layer, uint32_t(layer.size()) - r.engine.TailLength(), r.engine.TailLength()));
    CHECK(r.store.SavedVersion() != r.engine.ContentVersion());
    // A failed save: SaveFailed until the retry 10 s later succeeds; SaveSoon does
    // not skip the retry delay.
    card.failRename = true;
    r.store.SaveSoon();
    for (int m = 0; m < 200 && !r.store.SaveFailed(); ++m)
        r.Run(1);
    CHECK(r.store.SaveFailed() && r.store.LastFault() == LoopStore::Fault::Write);
    card.failRename = false;
    r.store.SaveSoon();
    r.Run(5000);
    CHECK(r.store.SaveFailed() && r.store.Saves() == 2);
    RunUntilSaved(r, 3, 6000);
    CHECK(!r.store.SaveFailed() && r.store.SavedVersion() == r.engine.ContentVersion());
    CHECK(FileHolds(card, kLoop, take, r.engine.Length(), r.engine.TailLength()));
    PASS("save soon: no delay before a share, idle engine and retry delay kept; halt keeps loop.wav\n");
}

int main() {
    TestSaveAndRecall();
    TestSaveSoonAndHalt();
    TestAbortAndFaults();
    TestTornLastChunk();
    TestImport();
    TestImport44k1();
    if (failures) {
        std::printf("FAILED: %d storage check(s)\n", failures);
        return 1;
    }
    std::printf("All storage tests passed\n");
    return 0;
}
