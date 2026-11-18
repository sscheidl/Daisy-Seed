#pragma once
// Hands MIDI messages to a driver on an own thread, so the GUI thread never waits in it.
//
// Why (P3 of the Nebulae first-contact test, 2026-11-08): once a kit's firmware stalls, the
// USB-MIDI driver stops completing SysEx. JUCE 9 then waits up to 5 s per SysEx inside
// sendMessageNow; other drivers wait forever. A stalled device must never take the
// application down, so:
//
// * Send() only queues (bounded) and never waits.
// * A message that stays in the driver longer than `send_timeout_ms`, a driver that refuses a
//   message or throws, marks the link lost (sticky; Lost() and LostReason() report it).
// * Close() waits at most `close_wait_ms` (not at all for a thread the watchdog already found
//   stuck). A thread still stuck in the driver is detached; it
//   owns the device objects (`Resources`) and, once the driver returns, hands them to `dispose`
//   (the JUCE port posts their destruction to the message thread, as JUCE requires).
// * After BeginShutdown() (application exit) such a thread leaks its resources instead: the
//   static objects of the MIDI library may already be gone.
//
// No JUCE here: the host tests drive it with fake drivers that block, refuse or throw.
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace editor {
struct SendWorkerConfig {
    std::size_t max_queued = 256;
    uint32_t send_timeout_ms = 2000; // one message longer in the driver: the device is not answering
    uint32_t close_wait_ms = 50;     // Close() waits at most this long for the thread
};

class SendWorker {
  public:
    using Config = SendWorkerConfig;
    // Hands one complete message to the driver; may block and may throw. false: refused.
    using SendFn = std::function<bool(const std::vector<uint8_t>&)>;
    // The device objects the driver calls use; they live as long as the thread needs them.
    using Resources = std::shared_ptr<void>;
    // Receives the resources of an abandoned thread once the driver returned (worker thread).
    using DisposeFn = std::function<void(Resources)>;

    SendWorker(SendFn send, Resources resources, DisposeFn dispose, Config config = Config())
        : state_(std::make_shared<State>()) {
        state_->send = std::move(send);
        state_->resources = std::move(resources);
        state_->dispose = std::move(dispose);
        state_->config = config;
        std::shared_ptr<State> s = state_;
        thread_ = std::thread([s] { Loop(s); });
    }
    ~SendWorker() { Close(); }
    SendWorker(const SendWorker&) = delete;
    SendWorker& operator=(const SendWorker&) = delete;

    // Queues one message. false: closed, lost, or the queue is full (which marks the link
    // lost: the driver stopped taking data).
    bool Send(const uint8_t* bytes, std::size_t size) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        State& s = *state_;
        if(s.stop || s.lost)
            return false;
        if(s.queue.size() >= s.config.max_queued) {
            MarkLost(s, "the MIDI driver stopped taking messages (" + std::to_string(s.queue.size()) + " queued)");
            return false;
        }
        s.queue.emplace_back(bytes, bytes + size);
        s.cv.notify_all();
        return true;
    }

    // True once the link is lost; also checks whether the message in the driver overran.
    bool Lost() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        State& s = *state_;
        if(!s.lost && s.busy) {
            const auto in_driver = std::chrono::steady_clock::now() - s.busy_since;
            if(in_driver >= std::chrono::milliseconds(s.config.send_timeout_ms))
                MarkLost(s, "a MIDI send did not complete within " + std::to_string(s.config.send_timeout_ms) + " ms");
        }
        return s.lost;
    }
    std::string LostReason() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->reason;
    }
    // Messages queued but not yet handed to the driver.
    std::size_t Pending() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->queue.size();
    }
    // A message is in the driver right now.
    bool Busy() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->busy;
    }

    // Stops the thread (queued messages are dropped). Returns the resources when the thread
    // ended within close_wait_ms: the caller destroys them on its own thread. Otherwise
    // returns null: the thread is detached and hands them to `dispose` later. Idempotent.
    Resources Close() {
        if(!thread_.joinable())
            return nullptr;
        State& s = *state_;
        std::unique_lock<std::mutex> lock(s.mutex);
        s.stop = true;
        s.queue.clear();
        s.cv.notify_all();
        // A thread the watchdog already found stuck in the driver is not waited for at all.
        const bool stuck = s.lost && s.busy;
        if(!stuck)
            s.cv.wait_for(lock, std::chrono::milliseconds(s.config.close_wait_ms), [&s] { return s.finished; });
        if(s.finished) {
            lock.unlock();
            thread_.join();
            return std::exchange(s.resources, nullptr);
        }
        s.abandoned = true; // the thread disposes of the resources itself
        {
            std::lock_guard<std::mutex> global(Global().mutex);
            ++Global().abandoned;
        }
        lock.unlock();
        thread_.detach();
        return nullptr;
    }

    // Application exit: abandoned threads that return from the driver after this leak their
    // resources instead of calling `dispose`.
    static void BeginShutdown() {
        std::lock_guard<std::mutex> lock(Global().mutex);
        Global().shutting_down = true;
    }
    // Threads detached in a driver that have not returned yet.
    static std::size_t Abandoned() {
        std::lock_guard<std::mutex> lock(Global().mutex);
        return Global().abandoned;
    }

  private:
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::vector<uint8_t>> queue;
        SendFn send;
        Resources resources;
        DisposeFn dispose;
        Config config;
        std::chrono::steady_clock::time_point busy_since;
        std::string reason;
        bool stop = false, lost = false, busy = false, finished = false, abandoned = false;
    };
    struct Shared {
        std::mutex mutex;
        std::size_t abandoned = 0;
        bool shutting_down = false;
        std::vector<Resources> leaked; // kept reachable, never destroyed (process exit)
    };
    static Shared& Global() {
        static Shared* shared = new Shared(); // never destroyed: detached threads use it at exit
        return *shared;
    }

    static void MarkLost(State& s, const std::string& reason) {
        if(s.lost)
            return;
        s.lost = true;
        s.reason = reason;
        s.queue.clear();
    }

    static void Loop(std::shared_ptr<State> state) {
        State& s = *state;
        while(true) {
            std::vector<uint8_t> m;
            {
                std::unique_lock<std::mutex> lock(s.mutex);
                s.cv.wait(lock, [&s] { return s.stop || (!s.lost && !s.queue.empty()); });
                if(s.stop)
                    break;
                m = std::move(s.queue.front());
                s.queue.pop_front();
                s.busy = true;
                s.busy_since = std::chrono::steady_clock::now();
            }
            bool ok = false;
            std::string error;
            try {
                ok = s.send(m);
                if(!ok)
                    error = "the MIDI driver refused a message";
            } catch(const std::exception& e) {
                error = std::string("the MIDI driver failed: ") + e.what();
            } catch(...) {
                error = "the MIDI driver failed";
            }
            std::lock_guard<std::mutex> lock(s.mutex);
            s.busy = false;
            if(!ok)
                MarkLost(s, error);
        }
        Resources leftover;
        DisposeFn dispose;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            s.finished = true;
            s.cv.notify_all();
            if(!s.abandoned)
                return; // Close() takes the resources (or the worker object is gone with them)
            leftover = std::exchange(s.resources, nullptr);
            dispose = std::move(s.dispose);
        }
        {
            std::lock_guard<std::mutex> lock(Global().mutex);
            --Global().abandoned;
            if(Global().shutting_down) {
                Global().leaked.push_back(std::move(leftover)); // the process is exiting
                return;
            }
        }
        if(dispose)
            dispose(std::move(leftover));
    }

    std::shared_ptr<State> state_;
    std::thread thread_;
};
}
