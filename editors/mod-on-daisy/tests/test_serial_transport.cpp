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
    std::atomic<int> backends{0}, closed{0};
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
        if(dev.vanished)
            return false;
        std::lock_guard<std::mutex> lock(dev.mutex);
        dev.instrument.Management().Receive(data, n, NowMs());
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
    CHECK(!r.RunUntil([&] { return r.session.Connected(); }, 1600));
    CHECK(r.Logged("no DDP reply to HELLO") && r.dev.backends == 1 && r.dev.closed == 1);
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
    CHECK(open_ms < 50.0 && write_ms < 50.0 && read_ms < 50.0 && close_ms < 50.0 && destroy_ms < 50.0);
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
    CHECK(accepted >= 15 && accepted <= 17); // 64 KiB bound, the thread took at most one chunk
    const Clock::time_point t0 = Clock::now();
    link.Close();
    CHECK(ElapsedMs(t0) < 50.0);
    std::printf("PASS serial bounded queues: %d x 4 KiB accepted, then refused; close still fast\n", accepted);
}
}

int main() {
    TestOpenHello();
    TestHandshakeTimeout();
    TestPortVanishesAndReconnect();
    TestNeverBlocksMessageThread();
    TestBoundedQueues();
    if(failures) {
        std::fprintf(stderr, "FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: threaded serial transport with a fake port\n");
}
