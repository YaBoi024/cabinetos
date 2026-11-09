// One worker, one result, polled once a frame: first run's five jobs
// (setup.cpp). Moved out of setup.cpp in the restructure (#398, Part 3
// section 22) so later code can use it rather than write the thread handling
// again.
//
// Its destructor joins. An owner that also has a cancel flag the worker
// reads (setup::Flow's btCancel_) declares that flag BEFORE its Jobs, so the
// flag still exists while they are destroyed and joined. start() joins any
// previous worker first, so a caller without a busy() guard waits for it.

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace cab {

// --- Blocking work, moved off the frame ------------------------------------
//
// One worker, one result, polled once a frame. The same shape as LaunchJob
// (launch.h) and StateLoad (states.h), generalised because first run needs
// five of them and writing the thread handling five times is how one of them
// ends up subtly different.
template <class T>
class Job {
public:
    ~Job() { join(); }
    Job() = default;
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    bool busy() const { return running_.load(); }

    // `fn` runs on the worker and must touch nothing else in this program.
    void start(std::function<bool(T&, std::string&)> fn) {
        join();
        result_ = T{};
        error_.clear();
        ok_ = false;
        done_.store(false);
        running_.store(true);
        worker_ = std::thread([this, fn = std::move(fn)] {
            T r{};
            std::string e;
            const bool good = fn(r, e);
            result_ = std::move(r);
            error_ = std::move(e);
            ok_ = good;
            running_.store(false);
            done_.store(true);
        });
    }

    // Waits for a worker that is still going, so the next take() has its
    // result. For the one moment a caller cannot go on without it.
    void wait() { join(); }

    // Consumes a finished result. Returns false while the worker is still
    // going, so a caller can poll this every frame and act on the one frame it
    // becomes true.
    bool take(T* out, std::string* err, bool* ok) {
        if (!done_.load()) return false;
        join();                      // the worker has finished; reap it
        done_.store(false);
        if (out) *out = std::move(result_);
        if (err) *err = error_;
        if (ok) *ok = ok_;
        return true;
    }

private:
    void join() {
        if (worker_.joinable()) worker_.join();
    }
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> done_{false};
    T result_{};
    std::string error_;
    bool ok_ = false;
};

}  // namespace cab
