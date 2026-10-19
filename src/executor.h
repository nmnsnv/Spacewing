#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace spacewing {
// One worker for native operations. A coalesced maintenance job never outranks user work.
class Executor {
public:
    explicit Executor(std::function<void(std::exception_ptr)> error = {});
    ~Executor();
    void enqueue(std::function<void()> job, bool maintenance = false);
    bool busy() const;

private:
    void run();
    std::function<void(std::exception_ptr)> error_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::function<void()>> jobs_;
    std::function<void()> maintenance_;
    bool running_ = false;
    bool stopping_ = false;
    std::thread worker_;
};
} // namespace spacewing
