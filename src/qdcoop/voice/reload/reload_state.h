// Knights' Domain: a plain stand-in for the Fallout co-op project's ManagedThread (there it also took part in hot reloads).
// start() runs body on its own thread; stopAndJoin() asks it to stop and waits for it.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace nvreload {

class ManagedThread {
public:
    ManagedThread() = default;
    ~ManagedThread() { stopAndJoin(); }
    ManagedThread(const ManagedThread&) = delete;
    ManagedThread& operator=(const ManagedThread&) = delete;

    void start(const char* name, std::function<void(const std::function<bool()>& stopRequested)> body) {
        stopAndJoin();
        name_ = name ? name : "";
        stop_ = std::make_shared<std::atomic<bool>>(false);
        auto stop = stop_;
        thread_ = std::thread([stop, body = std::move(body)] {
            const std::function<bool()> requested = [stop] { return stop->load(); };
            body(requested);
        });
    }
    void stopAndJoin() {
        if (stop_) stop_->store(true);
        if (thread_.joinable()) thread_.join();
        stop_.reset();
    }
    bool running() const { return thread_.joinable(); }
    const std::string& name() const { return name_; }

private:
    std::thread thread_;
    std::shared_ptr<std::atomic<bool>> stop_;
    std::string name_;
};

}  // namespace nvreload
