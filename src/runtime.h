#pragma once
#include "command.h"
#include "config/config.h"
#include "executor.h"
#include "ipc/socket.h"
#include "service.h"
#include "controller.h"
#include "hotkeys.h"
#include "platform/application.h"
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace spacewing {
// State except the worker queue/controller belongs to the AppKit main thread.
class Runtime {
public:
    explicit Runtime(const Command &startup);
    void start();

private:
    Reply handle(const std::vector<std::string> &args);
    Reply settingsRequest(const std::vector<std::string> &args);
    Reply reload();
    void execute(Command command, std::function<void(Reply)> completion);
    void hotkey(uint64_t id, bool pressed);
    void guardEmptyDesktop();
    void updateDesktopNumber();
    void status();
    void configureSwitcher();
    bool recording();
    std::string path_;
    bool explicitConfig_;
    Config active_{};
    std::string source_, configError_, operationError_, switcherError_;
    bool loaded_ = false;
    workspace::BindingSet bindings_;
    std::unique_ptr<Watcher> watcher_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<Executor> executor_;
    workspace::Controller controller_;
    std::unique_ptr<switcher::Service> switcher_;
    std::optional<workspace::Snapshot> landing_;
    std::string recordingToken_, recordedChord_;
    uint64_t nextToken_ = 0;
    std::chrono::steady_clock::time_point recordingDeadline_;
};
} // namespace spacewing
