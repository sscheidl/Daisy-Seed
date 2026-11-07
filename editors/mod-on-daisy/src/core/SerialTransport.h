#pragma once
// Serial transport for the DDP session: an I/O thread with bounded queues around a small
// platform backend, so no call from the GUI thread waits for the operating system
// (SD_PAGE_WORK_ORDER.md phase 3: nothing blocks the message thread longer than 50 ms).
//
//   GUI thread:  ThreadedSerialLink::Read() swaps the receive buffer, Write() appends to
//                the send queue and wakes the thread, Close() asks the thread to stop and
//                joins it (the backend's Cancel() aborts a pending OS call first).
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
// which the GUI thread uses to abort a pending Read()/Write() before joining.
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

    // Takes a backend that is already open.
    explicit ThreadedSerialLink(std::unique_ptr<SerialBackend> backend) : backend_(std::move(backend)) {
        thread_ = std::thread([this] { Loop(); });
    }
    ~ThreadedSerialLink() override { Close(); }

    bool Read(std::vector<uint8_t>& out) override {
        std::lock_guard<std::mutex> lock(mutex_);
        out.swap(received_);
        received_.clear();
        return !failed_;
    }

    bool Write(const uint8_t* data, std::size_t n) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(failed_)
                return false;
            if(send_.size() + n > kMaxSend)
                return false; // the device stopped taking data: treat as a failed link
            send_.insert(send_.end(), data, data + n);
        }
        wake_.notify_one();
        return true;
    }

    void Close() override {
        bool was_running;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            was_running = !stop_;
            stop_ = true;
        }
        if(!was_running)
            return;
        wake_.notify_one();
        if(backend_)
            backend_->Cancel();
        if(thread_.joinable())
            thread_.join();
    }

    bool Failed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return failed_;
    }
    std::size_t Dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

  private:
    void Loop() {
        std::vector<uint8_t> buffer(4096), pending;
        while(true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if(stop_)
                    return;
                pending.assign(send_.begin(), send_.end());
                send_.clear();
            }
            if(!pending.empty() && !backend_->Write(pending.data(), pending.size()))
                return Fail();
            const int n = backend_->Read(buffer.data(), buffer.size(), kReadTimeoutMs);
            if(n < 0)
                return Fail();
            if(n > 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(received_.size() + std::size_t(n) > kMaxReceive)
                    dropped_ += std::size_t(n); // the GUI stopped reading: keep the newest, count the loss
                else
                    received_.insert(received_.end(), buffer.begin(), buffer.begin() + n);
            }
        }
    }
    void Fail() {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
    }

    std::unique_ptr<SerialBackend> backend_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<uint8_t> send_;
    std::vector<uint8_t> received_;
    bool stop_ = false, failed_ = false;
    std::size_t dropped_ = 0;
    std::thread thread_;
};

// Opens a backend by port name; platform code provides the factory. Open() itself may
// take a few milliseconds (CreateFile) but never waits for the device.
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
