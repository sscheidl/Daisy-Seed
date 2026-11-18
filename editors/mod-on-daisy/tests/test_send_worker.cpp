// Host tests of the MIDI send worker (src/core/SendWorker.h) with fake drivers that block,
// refuse or throw, as the WinMM driver did once the Nebulae 0.2.1 firmware stalled the kit
// (P3, 2026-11-08: "MidiOutWinMM::sendMessage: error sending sysex message"). The rule under
// test: no call on the GUI thread waits in the driver; a send that throws, is refused or never
// completes marks the link lost; Close() is bounded; a new link works once the kit answers.
// Real time, no JUCE.
//   make -C editors/mod-on-daisy test
#include "../src/core/SendWorker.h"
#include "../src/core/SimInstruments.h"
#include "../src/core/Simulator.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if(!(cond)) {                                                                     \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while(0)

using namespace editor;
using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point since) {
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}
template <class Pred>
bool WaitFor(Pred pred, int ms) {
    const auto start = Clock::now();
    while(!pred()) {
        if(ElapsedMs(start) > ms)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// A MIDI output driver. Send() runs on the worker thread; the test thread flips the faults.
struct FakeDriver {
    enum class Mode { Ok, Refuse, Throw };
    std::mutex mutex;
    std::condition_variable cv;
    bool stalled = false; // Send() waits in the "driver" until un-stalled
    Mode mode = Mode::Ok;
    std::deque<std::vector<uint8_t>> wire; // what reached the device
    std::atomic<int> entered{0};

    bool Send(const std::vector<uint8_t>& m) {
        std::unique_lock<std::mutex> lock(mutex);
        ++entered;
        cv.wait(lock, [this] { return !stalled; });
        if(mode == Mode::Refuse)
            return false;
        if(mode == Mode::Throw)
            throw std::runtime_error("MidiOutWinMM::sendMessage: error sending sysex message.");
        wire.push_back(m);
        return true;
    }
    void Stall(bool on) {
        std::lock_guard<std::mutex> lock(mutex);
        stalled = on;
        cv.notify_all();
    }
    std::size_t Delivered() {
        std::lock_guard<std::mutex> lock(mutex);
        return wire.size();
    }
    std::deque<std::vector<uint8_t>> TakeWire() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(wire, {});
    }
};

// Stands for the JUCE device objects: counts its destruction and records the thread.
struct Devices {
    std::atomic<int>& destroyed;
    std::thread::id& where;
    Devices(std::atomic<int>& d, std::thread::id& w) : destroyed(d), where(w) {}
    ~Devices() {
        where = std::this_thread::get_id();
        ++destroyed;
    }
};

struct Rig {
    FakeDriver driver;
    std::atomic<int> destroyed{0};
    std::thread::id destroyed_on;
    std::mutex disposed_mutex;
    std::vector<SendWorker::Resources> disposed; // the "message thread" queue of disposals
    std::thread::id disposed_from;
    std::unique_ptr<SendWorker> worker;

    explicit Rig(SendWorker::Config config = SendWorker::Config()) {
        auto devices = std::make_shared<Devices>(destroyed, destroyed_on);
        worker = std::make_unique<SendWorker>(
            [this](const std::vector<uint8_t>& m) { return driver.Send(m); }, devices,
            [this](SendWorker::Resources r) {
                std::lock_guard<std::mutex> lock(disposed_mutex);
                disposed_from = std::this_thread::get_id();
                disposed.push_back(std::move(r));
            },
            config);
    }
    std::size_t Disposals() {
        std::lock_guard<std::mutex> lock(disposed_mutex);
        return disposed.size();
    }
};

const uint8_t kMsg[] = {0xF0, 0x7D, 0x4E, 0x42, 0x01, 0x01, 0xF7};

SendWorker::Config Fast() {
    SendWorker::Config c;
    c.max_queued = 16;
    c.send_timeout_ms = 100;
    c.close_wait_ms = 50;
    return c;
}

void TestSendsInOrderAndCloseReturnsResources() {
    Rig rig;
    for(uint8_t i = 0; i < 5; ++i) {
        const uint8_t m[] = {0xF0, i, 0xF7};
        CHECK(rig.worker->Send(m, sizeof(m)));
    }
    CHECK(WaitFor([&] { return rig.driver.Delivered() == 5; }, 1000));
    const auto wire = rig.driver.TakeWire();
    for(uint8_t i = 0; i < wire.size(); ++i)
        CHECK(wire[i].size() == 3 && wire[i][1] == i);
    CHECK(!rig.worker->Lost());
    SendWorker::Resources r = rig.worker->Close();
    CHECK(r != nullptr && rig.destroyed == 0); // the caller destroys them on its own thread
    r.reset();
    CHECK(rig.destroyed == 1 && rig.destroyed_on == std::this_thread::get_id());
    CHECK(!rig.worker->Send(kMsg, sizeof(kMsg))); // closed
    CHECK(rig.worker->Close() == nullptr);         // idempotent
    CHECK(rig.Disposals() == 0 && SendWorker::Abandoned() == 0);
}

// The kit stalls: the driver never returns. Sending stays instant and the queue is bounded.
void TestStalledDriverNeverBlocksTheCaller() {
    SendWorker::Config c = Fast();
    c.send_timeout_ms = 60000; // only the queue limit decides here
    Rig rig(c);
    rig.driver.Stall(true);
    double worst = 0;
    int accepted = 0;
    for(int i = 0; i < 300; ++i) {
        const auto t = Clock::now();
        accepted += rig.worker->Send(kMsg, sizeof(kMsg)) ? 1 : 0;
        worst = std::max(worst, ElapsedMs(t));
    }
    // One message in the driver plus 16 queued; then the link is lost, nothing more queued.
    CHECK(accepted >= 16 && accepted <= 17);
    CHECK(worst < 20.0);
    CHECK(rig.worker->Lost() && rig.worker->LostReason().find("stopped taking") != std::string::npos);
    CHECK(rig.worker->Pending() == 0);
    rig.driver.Stall(false);
    CHECK(WaitFor([&] { return !rig.worker->Busy(); }, 1000));
    CHECK(rig.driver.Delivered() <= 1); // the queue was dropped when the link was lost
}

// A send that never completes marks the link lost after send_timeout_ms.
void TestWatchdogMarksLinkLost() {
    Rig rig(Fast());
    rig.driver.Stall(true);
    CHECK(rig.worker->Send(kMsg, sizeof(kMsg)));
    CHECK(WaitFor([&] { return rig.driver.entered == 1; }, 1000));
    CHECK(!rig.worker->Lost() && rig.worker->Busy());
    const auto t = Clock::now();
    CHECK(WaitFor([&] { return rig.worker->Lost(); }, 2000));
    CHECK(ElapsedMs(t) < 400);
    CHECK(rig.worker->LostReason() == "a MIDI send did not complete within 100 ms");
    CHECK(!rig.worker->Send(kMsg, sizeof(kMsg)));
    rig.driver.Stall(false);
    CHECK(WaitFor([&] { return !rig.worker->Busy(); }, 1000));
    CHECK(rig.worker->Lost()); // sticky: a late completion does not revive the link
}

void TestRefusedAndThrowingDriver() {
    for(const FakeDriver::Mode mode : {FakeDriver::Mode::Refuse, FakeDriver::Mode::Throw}) {
        Rig rig(Fast());
        rig.driver.mode = mode;
        CHECK(rig.worker->Send(kMsg, sizeof(kMsg)));
        CHECK(WaitFor([&] { return rig.worker->Lost(); }, 1000));
        const std::string reason = rig.worker->LostReason();
        if(mode == FakeDriver::Mode::Refuse)
            CHECK(reason == "the MIDI driver refused a message");
        else
            CHECK(reason.find("error sending sysex message") != std::string::npos);
        CHECK(!rig.worker->Send(kMsg, sizeof(kMsg)));
        CHECK(rig.worker->Close() != nullptr); // the thread survived the exception and ended
    }
}

// Closing while the thread is stuck in the driver: bounded; the device objects are handed to
// the dispose callback once the driver returns, never destroyed under the driver call.
void TestCloseWhileStuckIsBoundedAndDisposesLater() {
    Rig rig(Fast());
    rig.driver.Stall(true);
    CHECK(rig.worker->Send(kMsg, sizeof(kMsg)));
    CHECK(WaitFor([&] { return rig.driver.entered == 1; }, 1000));
    const auto t = Clock::now();
    CHECK(rig.worker->Close() == nullptr);
    CHECK(ElapsedMs(t) < 150);
    CHECK(SendWorker::Abandoned() == 1);
    rig.worker.reset(); // the editor's MidiPort is gone; the thread lives on
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(rig.destroyed == 0 && rig.Disposals() == 0);
    rig.driver.Stall(false); // the driver gives up (JUCE: after 5 s) or the kit answers
    CHECK(WaitFor([&] { return rig.Disposals() == 1; }, 1000));
    CHECK(rig.disposed_from != std::this_thread::get_id()); // handed over from the worker
    CHECK(rig.destroyed == 0);                              // not destroyed there
    CHECK(WaitFor([] { return SendWorker::Abandoned() == 0; }, 1000));
    {
        std::lock_guard<std::mutex> lock(rig.disposed_mutex);
        rig.disposed.clear(); // the "message thread" destroys them
    }
    CHECK(rig.destroyed == 1 && rig.destroyed_on == std::this_thread::get_id());
}

// The editor core over a worker: the kit answers, stalls, the link is reported lost while the
// GUI loop keeps running, and a new link connects once the kit answers again.
struct WorkerTransport : Transport {
    SendWorker* worker = nullptr;
    bool Send(const uint8_t* bytes, std::size_t size) override { return worker && worker->Send(bytes, size); }
};

void TestSessionSurvivesStallAndReconnects() {
    const SimInstrument& inst = kSimInstruments[1];
    Simulator sim(inst.make(), 5);
    sim.bases = inst.bases;
    sim.mode = inst.mode;
    sim.EnableExtensions(inst.extensions);
    Model model;
    Session session(model, 77);
    std::vector<std::string> logs;
    session.log = [&](LogKind, const std::string& t) { logs.push_back(t); };
    uint32_t now = 1000;
    double worst_ms = 0;
    SendWorker::Config c = Fast();
    c.send_timeout_ms = 200;

    auto rig = std::make_unique<Rig>(c);
    WorkerTransport link;
    link.worker = rig->worker.get();
    // One GUI tick: what reached the device goes to the simulator, its replies to the session.
    // The session's clock follows real time, as in the editor (1 ms timer steps), so its
    // retries and monitor renewals run at their real rate against the worker's watchdog.
    const auto t0 = Clock::now();
    const uint32_t start = now;
    auto tick = [&](FakeDriver& driver) {
        while(ElapsedMs(t0) < double(now + 1 - start))
            std::this_thread::yield(); // the worker thread runs alongside
        const auto t = Clock::now();
        for(std::vector<uint8_t>& m : driver.TakeWire())
            sim.Send(m.data(), m.size());
        sim.Advance(1, [&](const uint8_t* b, std::size_t n) { session.OnMessage(b, n); });
        session.Tick(++now);
        worst_ms = std::max(worst_ms, ElapsedMs(t));
    };
    session.Connect(link, now);
    for(int i = 0; i < 20000 && !(session.Connected() && session.Idle()); ++i)
        tick(rig->driver);
    CHECK(session.Connected());

    // The firmware stalls: USB-MIDI stops completing SysEx.
    rig->driver.Stall(true);
    bool lost = false;
    const auto stalled = Clock::now();
    while(!lost && ElapsedMs(stalled) < 3000) { // the GUI loop runs on, in real time
        tick(rig->driver);
        lost = rig->worker->Lost();
    }
    if(lost)
        std::printf("stall reported after %.0f ms: %s\n", ElapsedMs(stalled), rig->worker->LostReason().c_str());
    CHECK(lost && rig->worker->LostReason().find("did not complete") != std::string::npos);
    CHECK(worst_ms < 50.0);
    // What EditorComponent does with a lost link: offline, port closed in bounded time.
    session.Disconnect("device not answering");
    link.worker = nullptr;
    const auto t = Clock::now();
    CHECK(rig->worker->Close() == nullptr); // still stuck in the driver
    CHECK(ElapsedMs(t) < 10);               // found stuck by the watchdog: not waited for
    CHECK(!session.Connected());
    std::unique_ptr<Rig> stuck = std::move(rig);

    // The kit answers again (reflashed / power-cycled): a new port connects.
    auto fresh = std::make_unique<Rig>(c);
    link.worker = fresh->worker.get();
    session.Connect(link, now);
    for(int i = 0; i < 20000 && !(session.Connected() && session.Idle()); ++i)
        tick(fresh->driver);
    CHECK(session.Connected() && !fresh->worker->Lost());
    CHECK(worst_ms < 50.0);

    stuck->driver.Stall(false); // the old driver call finally returns
    CHECK(WaitFor([&] { return stuck->Disposals() == 1; }, 1000));
    session.Disconnect("");
    link.worker = nullptr;
    CHECK(fresh->worker->Close() != nullptr);
}

// Application exit: a thread that returns from the driver afterwards leaks its resources
// instead of handing them to a message thread that no longer exists. Runs last (global).
void TestShutdownLeaksInsteadOfDisposing() {
    auto rig = std::make_unique<Rig>(Fast());
    rig->driver.Stall(true);
    CHECK(rig->worker->Send(kMsg, sizeof(kMsg)));
    CHECK(WaitFor([&] { return rig->driver.entered == 1; }, 1000));
    CHECK(rig->worker->Close() == nullptr);
    SendWorker::BeginShutdown();
    rig->driver.Stall(false);
    CHECK(WaitFor([] { return SendWorker::Abandoned() == 0; }, 1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(rig->Disposals() == 0 && rig->destroyed == 0);
    // `rig` stays alive: the leaked Devices refer to its counters.
    static std::unique_ptr<Rig> keep;
    keep = std::move(rig);
}
}

int main() {
    TestSendsInOrderAndCloseReturnsResources();
    TestStalledDriverNeverBlocksTheCaller();
    TestWatchdogMarksLinkLost();
    TestRefusedAndThrowingDriver();
    TestCloseWhileStuckIsBoundedAndDisposesLater();
    TestSessionSurvivesStallAndReconnects();
    TestShutdownLeaksInsteadOfDisposing();
    if(failures) {
        std::fprintf(stderr, "FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: MIDI send worker with stalling, refusing and throwing fake drivers\n");
}
