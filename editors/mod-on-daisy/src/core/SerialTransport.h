#pragma once
// Serial transport for the DDP session: an I/O thread with bounded queues around a small
// platform backend, so no call from the GUI thread waits for the operating system
// (SD_PAGE_WORK_ORDER.md phase 3: nothing blocks the message thread longer than 50 ms).
//
//   GUI thread:  ThreadedSerialLink::Read() swaps the receive buffer, Write() appends to
//                the send queue, Close() asks the thread to stop, lets it send what is still
//                queued (BYE) for a few milliseconds, cancels the backend's pending OS call
//                and waits a bounded time for the thread; a thread that is still stuck in the
//                driver is detached and finishes on its own (it owns the shared state).
//   I/O thread:  drains the send queue with backend.Write(), then backend.Read() with a
//                short timeout; any backend error ends the link (Read()/Write() return
//                false on the GUI thread afterwards).
//
// The backend interface is platform-free; ui/SerialPortWin.h implements it with Win32
// overlapped I/O, tests use a fake that blocks and vanishes on purpose. No JUCE.
#include "DdpSession.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace editor::ddp {
// What a platform has to provide. All calls come from the I/O thread except Cancel(),
// which the GUI thread uses to abort a pending Read()/Write(); after Cancel() every call
// must return promptly (and may return 0 / false).
class SerialBackend {
  public:
    virtual ~SerialBackend() = default;
    // Up to `max` bytes, waiting at most `timeout_ms`. Returns the count, 0 for a timeout,
    // -1 for a failure (port gone).
    virtual int Read(uint8_t* buffer, std::size_t max, uint32_t timeout_ms) = 0;
    // The whole buffer, or false for a failure.
    virtual bool Write(const uint8_t* data, std::size_t n) = 0;
    // Abort a pending Read()/Write() as fast as possible (called from another thread).
    virtual void Cancel() = 0;
};

class ThreadedSerialLink : public SerialLink {
  public:
    static constexpr std::size_t kMaxReceive = 64 * 1024, kMaxSend = 64 * 1024;
    static constexpr uint32_t kReadTimeoutMs = 20;
    // Close(): wait this long for queued bytes (BYE) to go out, then this long for the thread.
    static constexpr uint32_t kDrainMs = 15, kJoinMs = 30;

    // Takes a backend that is already open.
    explicit ThreadedSerialLink(std::unique_ptr<SerialBackend> backend) : shared_(std::make_shared<Shared>()) {
        shared_->backend = std::move(backend);
        std::shared_ptr<Shared> s = shared_;
        thread_ = std::thread([s] { Loop(*s); });
    }
    ~ThreadedSerialLink() override { Close(); }

    bool Read(std::vector<uint8_t>& out) override {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        out.swap(shared_->received);
        shared_->received.clear();
        return !shared_->failed;
    }

    bool Write(const uint8_t* data, std::size_t n) override {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if(shared_->failed || shared_->stop)
            return false;
        if(shared_->send.size() + n > kMaxSend)
            return false; // the device stopped taking data: treat as a failed link
        shared_->send.insert(shared_->send.end(), data, data + n);
        return true;
    }

    void Close() override {
        if(closed_)
            return;
        closed_ = true;
        Shared& s = *shared_;
        {
            // Let the thread send what is queued (BYE), bounded; it checks `stop` after that.
            std::unique_lock<std::mutex> lock(s.mutex);
            s.cv.wait_for(lock, std::chrono::milliseconds(kDrainMs), [&s] { return s.send.empty() || s.failed || s.finished; });
            s.stop = true;
        }
        s.backend->Cancel();
        {
            std::unique_lock<std::mutex> lock(s.mutex);
            s.cv.wait_for(lock, std::chrono::milliseconds(kJoinMs), [&s] { return s.finished; });
        }
        if(thread_.joinable()) {
            std::lock_guard<std::mutex> lock(s.mutex);
            if(s.finished)
                thread_.join();
            else
                thread_.detach(); // still inside the driver: it ends on its own and frees the backend
        }
    }

    bool Failed() const {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        return shared_->failed;
    }
    std::size_t Dropped() const {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        return shared_->dropped;
    }
    // Whether the I/O thread has ended (after Close(): false means it was detached).
    bool Finished() const {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        return shared_->finished;
    }

  private:
    // Everything the I/O thread touches, kept alive by the thread itself after a detach.
    struct Shared {
        std::unique_ptr<SerialBackend> backend;
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<uint8_t> send;
        std::vector<uint8_t> received;
        bool stop = false, failed = false, finished = false;
        std::size_t dropped = 0;
    };

    static void Loop(Shared& s) {
        std::vector<uint8_t> buffer(4096), pending;
        while(true) {
            {
                std::unique_lock<std::mutex> lock(s.mutex);
                pending.assign(s.send.begin(), s.send.end());
                s.send.clear();
                if(pending.empty() && s.stop)
                    break;
            }
            if(!pending.empty()) {
                const bool ok = s.backend->Write(pending.data(), pending.size());
                s.cv.notify_all(); // Close() waits for the drain
                if(!ok) {
                    Fail(s);
                    break;
                }
                std::lock_guard<std::mutex> lock(s.mutex);
                if(s.stop)
                    break;
            }
            const int n = s.backend->Read(buffer.data(), buffer.size(), kReadTimeoutMs);
            if(n < 0) {
                Fail(s);
                break;
            }
            if(n > 0) {
                std::lock_guard<std::mutex> lock(s.mutex);
                s.received.insert(s.received.end(), buffer.begin(), buffer.begin() + n);
                if(s.received.size() > kMaxReceive) { // the GUI stopped reading: keep the newest
                    const std::size_t excess = s.received.size() - kMaxReceive;
                    s.received.erase(s.received.begin(), s.received.begin() + long(excess));
                    s.dropped += excess;
                }
            }
            std::lock_guard<std::mutex> lock(s.mutex);
            if(s.stop && s.send.empty())
                break;
        }
        std::lock_guard<std::mutex> lock(s.mutex);
        s.finished = true;
        s.cv.notify_all();
    }
    static void Fail(Shared& s) {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.failed = true;
    }

    std::shared_ptr<Shared> shared_;
    std::thread thread_;
    bool closed_ = false;
};

// Opens a backend by port name; platform code provides the factory. Open() itself may
// take a few milliseconds (CreateFile) but should not wait for the device.
using BackendFactory = std::function<std::unique_ptr<SerialBackend>(const std::string& device, std::string& error)>;

class ThreadedSerialPorts : public SerialPorts {
  public:
    ThreadedSerialPorts(std::function<std::vector<PortInfo>()> list, BackendFactory open)
        : list_(std::move(list)), open_(std::move(open)) {}
    std::vector<PortInfo> List() override { return list_ ? list_() : std::vector<PortInfo>{}; }
    std::unique_ptr<SerialLink> Open(const std::string& device, std::string& error) override {
        std::unique_ptr<SerialBackend> backend = open_ ? open_(device, error) : nullptr;
        if(!backend)
            return nullptr;
        return std::make_unique<ThreadedSerialLink>(std::move(backend));
    }

  private:
    std::function<std::vector<PortInfo>()> list_;
    BackendFactory open_;
};
}
