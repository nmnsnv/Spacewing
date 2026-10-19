#pragma once
#include "session.h"
#include <functional>
#include <memory>

namespace spacewing::switcher {
// Main-thread, in-process ports. No dependency on Spacewing's controller, IPC,
// config parser, or action types. An extracted app supplies its own host adapter.
struct Host {
    std::function<bool()> unavailable = [] { return false; };
    std::function<void(const std::string &)> reportError = [](const auto &) {};
    std::function<void()> began = [] {};
};
class Service {
public:
    explicit Service(Host host = {});
    ~Service();
    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;
    void configure(Options options);
    // Host announces desktop changes, recording, sleep, or a desktop action.
    void cancel();
    bool activating() const;
    bool busy() const;
    bool running() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace spacewing::switcher
