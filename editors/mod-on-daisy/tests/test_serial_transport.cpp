// Host tests of the threaded serial transport (src/core/SerialTransport.h) with a fake
// backend: open and HELLO through the DDP session, handshake timeout, a port that vanishes,
// reconnect, and the rule that no call on the GUI thread blocks longer than 50 ms even
// while the backend blocks (SD_PAGE_WORK_ORDER.md phase 3). Real time, no JUCE.
//   make -C editors/mod-on-daisy test
#include "../src/core/SdShareSim.h"
#include "../src/core/SerialTransport.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
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
using namespace editor::ddp;
using Clock = std::chrono::steady_clock;

uint32_t NowMs() {
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}
double ElapsedMs(Clock::time_point since) {
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

// The simulated kit in SD management behind a fake OS port. Read()/Write() run on the I/O
// thread; the test thread only flips the fault switches.
struct FakeDevice {
    std::mutex mutex;
    sd::SimInstrument instrument{sd::kNebulae, [] { return NowMs(); }};
    std::atomic<bool> vanished{false};
    std::atomic<int> block_ms{0}; // every Read()/Write() sleeps this long first
    std::atomic<int> backends{0}, closed{0}, writes{0};
    std::atomic<int> open_delay_ms{0}; // the OS takes this long to open the port
    FakeDevice() { instrument.StartInManagement(); }
};

struct FakeBackend : SerialBackend {
    FakeDevice& dev;
    std::atomic<bool> cancelled{false};
    explicit FakeBackend(FakeDevice& d) : dev(d) { ++dev.backends; }
    ~FakeBackend() override { ++dev.closed; }

    void Block() {
        const int total = dev.block_ms;
        for(int t = 0; t < total && !cancelled; t += 5)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    int Read(uint8_t* buffer, std::size_t max, uint32_t timeout_ms) override {
        Block();
        const Clock::time_point start = Clock::now();
        while(!cancelled) {
            if(dev.vanished)
                return -1;
            std::vector<uint8_t> out;
            {
                std::lock_guard<std::mutex> lock(dev.mutex);
                out = dev.instrument.Management().Output(NowMs());
            }
            if(!out.empty()) {
                const std::size_t n = std::min(max, out.size());
                std::copy(out.begin(), out.begin() + long(n), buffer);
                return int(n);
            }
            if(ElapsedMs(start) >= timeout_ms)
                return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return 0;
    }
    bool Write(const uint8_t* data, std::size_t n) override {
        Block();
        if(dev.vanished || cancelled) // a real driver takes nothing after the cancel either
            return false;
        std::lock_guard<std::mutex> lock(dev.mutex);
        dev.instrument.Management().Receive(data, n, NowMs());
        ++dev.writes;
        return true;
    }
    void Cancel() override { cancelled = true; }
};

struct Rig {
    FakeDevice dev;
    ThreadedSerialPorts ports{[this] { return dev.vanished ? std::vector<PortInfo>{} : dev.instrument.CdcPorts(); },
                              [this](const std::string& device, std::string& error) -> std::unique_ptr<SerialBackend> {
                                  if(device != sd::kSimComPort || dev.vanished) {
                                      error = "cannot open " + device;
                                      return nullptr;
                                  }
                                  if(dev.open_delay_ms > 0)
                                      std::this_thread::sleep_for(std::chrono::milliseconds(int(dev.open_delay_ms)));
                                  return std::make_unique<FakeBackend>(dev);
                              }};
    DdpSession session{&ports};
    double max_tick_ms = 0;
    std::vector<std::string> logs;
    Rig() {
        session.log = [this](const std::string& t) { logs.push_back(t); };
    }
    // Ticks like the GUI timer (10 ms) for up to `ms` real milliseconds or until `done`.
    template <class Pred>
    bool RunUntil(Pred done, int ms) {
        const Clock::time_point start = Clock::now();
        while(ElapsedMs(start) < ms) {
            const Clock::time_point t0 = Clock::now();
            session.Tick(NowMs());
            max_tick_ms = std::max(max_tick_ms, ElapsedMs(t0));
            if(done())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return done();
    }
    bool Logged(const char* part) const {
        for(const std::string& l : logs)
            if(l.find(part) != std::string::npos)
                return true;
        return false;
    }
};

void TestOpenHello() {
    Rig r;
    CHECK(r.RunUntil([&] { return r.session.Connected(); }, 4000));
    CHECK(r.session.Device() && r.session.Device()->app == "NebulaeOnDaisy");
    CHECK(r.RunUntil([&] { return r.session.Reader() && r.session.Reader()->state == ReaderState::Local; }, 2000));
    CHECK(r.RunUntil([&] { return r.session.SdCard().has_value(); }, 2000));
    CHECK(r.session.RequestReader("SHARE"));
    CHECK(r.RunUntil([&] { return r.session.Reader() && r.session.Reader()->state == ReaderState::Shared; }, 2000));
    r.RunUntil([&] { return false; }, 1200); // keep-alive: PING/PONG for a second
    CHECK(r.session.Connected() && r.session.FrameErrors() == 0);
    CHECK(r.max_tick_ms < 50.0);
    r.session.Disconnect();
    CHECK(r.dev.backends == 1 && r.dev.closed == 1);
    CHECK(r.dev.instrument.Management().reader_state == ReaderState::Shared); // BYE never returns the card
    std::printf("PASS serial open/HELLO: connect through the I/O thread, READER SHARE, keep-alive, BYE (max tick %.1f ms)\n",
                r.max_tick_ms);
}

void TestHandshakeTimeout() {
    Rig r;
    r.dev.instrument.Management().responsive = false;
    CHECK(!r.RunUntil([&] { return r.session.Connected(); }, 2800)); // async scan (≤ 1 s) + 1 s handshake
    CHECK(r.RunUntil([&] { return r.Logged("no DDP reply to HELLO"); }, 1500) && r.dev.backends == 1);
    CHECK(r.RunUntil([&] { return r.dev.closed == 1; }, 500));
    CHECK(r.max_tick_ms < 50.0);
    std::printf("PASS serial timeout: HELLO without reply closes the port after 1 s (max tick %.1f ms)\n", r.max_tick_ms);
}

void TestPortVanishesAndReconnect() {
    Rig r;
    CHECK(r.RunUntil([&] { return r.session.Connected(); }, 4000));
    CHECK(r.session.RequestReader("SHARE"));
    CHECK(r.RunUntil([&] { return r.session.Reader() && r.session.Reader()->state == ReaderState::Shared; }, 2000));
    r.dev.vanished = true; // USB re-enumeration: the OS port is gone
    CHECK(r.RunUntil([&] { return !r.session.Connected(); }, 1000));
    CHECK(r.Logged("USB connection lost") && r.dev.closed == 1);
    r.RunUntil([&] { return false; }, 300);
    CHECK(r.dev.backends == 1); // no port listed: nothing opened
    r.dev.vanished = false;
    CHECK(r.RunUntil([&] { return r.session.Connected(); }, 4000));
    CHECK(r.session.Reconnects() == 1 && r.dev.backends == 2);
    CHECK(r.RunUntil([&] { return r.session.Reader().has_value(); }, 2000));
    CHECK(r.session.Reader()->state == ReaderState::Shared); // STATUS only: Windows still owns the card
    CHECK(r.max_tick_ms < 50.0);
    std::printf("PASS serial vanish/reconnect: the lost port ends the link without a hang, reconnect asks STATUS only "
                "(max tick %.1f ms)\n",
                r.max_tick_ms);
}

void TestNeverBlocksMessageThread() {
    FakeDevice dev;
    dev.block_ms = 200; // the OS call takes 200 ms every time
    Clock::time_point t0 = Clock::now();
    auto link = std::make_unique<ThreadedSerialLink>(std::make_unique<FakeBackend>(dev));
    const double open_ms = ElapsedMs(t0);
    std::vector<uint8_t> frame;
    BuildFrame("HELLO proto=1", frame);
    double write_ms = 0, read_ms = 0;
    std::vector<uint8_t> received;
    for(int i = 0; i < 20; ++i) {
        t0 = Clock::now();
        CHECK(link->Write(frame.data(), frame.size()));
        write_ms = std::max(write_ms, ElapsedMs(t0));
        t0 = Clock::now();
        CHECK(link->Read(received));
        read_ms = std::max(read_ms, ElapsedMs(t0));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    t0 = Clock::now();
    link->Close(); // cancels the blocked backend call and joins
    const double close_ms = ElapsedMs(t0);
    t0 = Clock::now();
    link.reset();
    const double destroy_ms = ElapsedMs(t0);
    const double close_bound = ThreadedSerialLink::kDrainMs + ThreadedSerialLink::kJoinMs + 20.0;
    CHECK(open_ms < 50.0 && write_ms < 50.0 && read_ms < 50.0 && close_ms < close_bound && destroy_ms < 50.0);
    CHECK(dev.closed == 1);
    // A failed backend makes Read()/Write() report the loss at once, without waiting.
    FakeDevice gone;
    gone.vanished = true;
    ThreadedSerialLink dead(std::make_unique<FakeBackend>(gone));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    t0 = Clock::now();
    CHECK(!dead.Read(received) && !dead.Write(frame.data(), frame.size()));
    CHECK(ElapsedMs(t0) < 50.0 && dead.Failed());
    std::printf("PASS serial never blocks: open %.1f ms, write %.1f ms, read %.1f ms, close %.1f ms, destroy %.1f ms with a "
                "backend that blocks 200 ms per call\n",
                open_ms, write_ms, read_ms, close_ms, destroy_ms);
}

void TestBoundedQueues() {
    // A device that never reads: the send queue fills and Write() reports the dead link
    // instead of growing without bound.
    FakeDevice dev;
    dev.block_ms = 100000;
    ThreadedSerialLink link(std::make_unique<FakeBackend>(dev));
    std::vector<uint8_t> chunk(4096, 'x');
    int accepted = 0;
    for(int i = 0; i < 64; ++i)
        accepted += link.Write(chunk.data(), chunk.size()) ? 1 : 0;
    // 64 KiB queue bound plus whatever the thread had already taken into its blocked write.
    CHECK(accepted >= 16 && accepted < 64);
    const Clock::time_point t0 = Clock::now();
    link.Close();
    CHECK(ElapsedMs(t0) < ThreadedSerialLink::kDrainMs + ThreadedSerialLink::kJoinMs + 20.0);
    std::printf("PASS serial bounded queues: %d x 4 KiB accepted, then refused; close still bounded\n", accepted);
}

// A driver that ignores the cancel (finding 4): Close() still returns within 50 ms, the
// thread is detached and finishes on its own, and the backend is freed afterwards.
void TestCloseWithUncooperativeBackend() {
    struct Stuck : SerialBackend {
        std::atomic<int>& closed;
        std::atomic<bool> cancelled{false};
        explicit Stuck(std::atomic<int>& c) : closed(c) {}
        ~Stuck() override { ++closed; }
        int Read(uint8_t*, std::size_t, uint32_t) override {
            std::this_thread::sleep_for(std::chrono::milliseconds(300)); // blocks regardless of Cancel()
            return 0;
        }
        bool Write(const uint8_t*, std::size_t) override {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return true;
        }
        void Cancel() override { cancelled = true; }
    };
    std::atomic<int> closed{0};
    {
        auto link = std::make_unique<ThreadedSerialLink>(std::make_unique<Stuck>(closed));
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // the thread is inside Read()
        std::vector<uint8_t> frame;
        BuildFrame("BYE", frame);
        CHECK(link->Write(frame.data(), frame.size()));
        const Clock::time_point t0 = Clock::now();
        link->Close();
        const double close_ms = ElapsedMs(t0);
        CHECK(close_ms < ThreadedSerialLink::kDrainMs + ThreadedSerialLink::kJoinMs + 50.0); // both waits bounded
        CHECK(!link->Finished()); // detached: still inside the driver
        link.reset(); // the destructor must not wait for it either
        CHECK(ElapsedMs(t0) < 150.0);
        std::printf("PASS serial uncooperative backend: close %.1f ms, thread detached\n", close_ms);
    }
    for(int i = 0; i < 100 && closed == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(closed == 1); // the detached thread ended and freed the backend
}

// BYE queued right before the close still goes out (finding 9), as a completed write
// before the cancel (finding 13): the fake refuses writes after Cancel().
void TestByeIsSent() {
    Rig r;
    CHECK(r.RunUntil([&] { return r.session.Connected(); }, 4000));
    CHECK(r.dev.instrument.Management().Streaming());
    const int writes_before = r.dev.writes;
    const Clock::time_point t0 = Clock::now();
    r.session.Disconnect();
    CHECK(ElapsedMs(t0) < ThreadedSerialLink::kDrainMs + ThreadedSerialLink::kJoinMs + 20.0);
    CHECK(r.dev.writes == writes_before + 1);                // exactly the BYE frame, written before the cancel
    CHECK(!r.dev.instrument.Management().Streaming()); // the simulated firmware received BYE
    std::printf("PASS serial BYE: written before the cancel during Close() (%.1f ms)\n", ElapsedMs(t0));
}

// Opening runs on the link's thread and enumeration on a worker (finding 9): a port that
// takes 300 ms to open or to list never holds the message thread.
void TestOpenOnWorker() {
    Rig r;
    r.dev.open_delay_ms = 300;
    double max_list_ms = 0, open_ms = 0;
    std::string error;
    Clock::time_point t0 = Clock::now();
    std::vector<PortInfo> first = r.ports.List(); // starts the worker, returns the last result (none yet)
    max_list_ms = ElapsedMs(t0);
    CHECK(first.empty());
    for(int i = 0; i < 50 && r.ports.Refreshes() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t0 = Clock::now();
    const std::vector<PortInfo> second = r.ports.List();
    max_list_ms = std::max(max_list_ms, ElapsedMs(t0));
    CHECK(second.size() == 1 && second[0].device == sd::kSimComPort);
    t0 = Clock::now();
    std::unique_ptr<SerialLink> link = r.ports.Open(sd::kSimComPort, error);
    open_ms = ElapsedMs(t0);
    CHECK(link && open_ms < 50.0 && max_list_ms < 50.0);
    auto* threaded = dynamic_cast<ThreadedSerialLink*>(link.get());
    CHECK(threaded && !threaded->Opened());
    std::vector<uint8_t> frame, received;
    BuildFrame("HELLO proto=1", frame);
    CHECK(link->Write(frame.data(), frame.size())); // queued before the port is open
    for(int i = 0; i < 100 && !(threaded && threaded->Opened()); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(threaded && threaded->Opened());
    bool answered = false;
    for(int i = 0; i < 100 && !answered; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(link->Read(received));
        answered = std::string(received.begin(), received.end()).find("HELLO") != std::string::npos;
    }
    CHECK(answered);
    t0 = Clock::now();
    link->Close();
    CHECK(ElapsedMs(t0) < ThreadedSerialLink::kDrainMs + ThreadedSerialLink::kJoinMs + 20.0);
    // The whole session over the slow port: connect, SHARE, as before.
    Rig s;
    s.dev.open_delay_ms = 300;
    CHECK(s.RunUntil([&] { return s.session.Connected(); }, 6000));
    CHECK(s.max_tick_ms < 50.0);
    // A port whose open fails is reported through the link and backed off; the session keeps scanning.
    Rig f;
    f.dev.vanished = true;
    f.RunUntil([] { return false; }, 1200);
    f.dev.vanished = false;
    CHECK(f.RunUntil([&] { return f.session.Connected(); }, 6000));
    std::printf("PASS serial open on worker: list %.1f ms, open %.1f ms, HELLO answered after the 300 ms open, session connects\n",
                max_list_ms, open_ms);
}
}

int main() {
    TestOpenHello();
    TestHandshakeTimeout();
    TestPortVanishesAndReconnect();
    TestNeverBlocksMessageThread();
    TestBoundedQueues();
    TestCloseWithUncooperativeBackend();
    TestByeIsSent();
    TestOpenOnWorker();
    if(failures) {
        std::fprintf(stderr, "FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: threaded serial transport with a fake port\n");
}
