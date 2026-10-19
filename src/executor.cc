#include "executor.h"
#include "result.h"
#include <utility>

namespace spacewing {
Executor::Executor(std::function<void(std::exception_ptr)> error) : error_(std::move(error)) {
    worker_ = std::thread([this] { run(); });
}
Executor::~Executor() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        jobs_.clear();
        maintenance_ = {};
    }
    condition_.notify_one();
    worker_.join();
}
void Executor::enqueue(std::function<void()> job, bool maintenance) {
    std::lock_guard lock(mutex_);
    if (stopping_)
        throw Error("Spacewing is stopping", 69);
    if (maintenance)
        maintenance_ = std::move(job);
    else {
        if (jobs_.size() >= 32)
            throw Error("Workspace request queue is full; try again", 75);
        jobs_.push_back(std::move(job));
    }
    condition_.notify_one();
}
bool Executor::busy() const {
    std::lock_guard lock(mutex_);
    return running_ || !jobs_.empty();
}
void Executor::run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || !jobs_.empty() || maintenance_; });
            if (stopping_)
                return;
            if (!jobs_.empty()) {
                job = std::move(jobs_.front());
                jobs_.pop_front();
            } else
                job = std::exchange(maintenance_, {});
            running_ = true;
        }
        try {
            job();
        } catch (...) {
            if (error_)
                error_(std::current_exception());
        }
        {
            std::lock_guard lock(mutex_);
            running_ = false;
        }
    }
}
} // namespace spacewing
