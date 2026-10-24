#include "runtime.h"
#include <filesystem>
#include <iostream>

namespace spacewing {
using namespace workspace;
namespace {
Reply failure(const std::exception &error) {
    const auto *coded = dynamic_cast<const Error *>(&error);
    return {coded ? coded->code : 1, "", error.what()};
}
} // namespace
Runtime::Runtime(const Command &startup)
    : path_(startup.explicitConfig ? startup.config : resolvedPath(defaultConfigPath())),
      explicitConfig_(startup.explicitConfig), bindings_(registerHotkey, unregisterHotkey) {}

void Runtime::start() {
    // Obtain singleton ownership before config creation or global hotkey registration.
    server_ = std::make_unique<Server>([this](const auto &args) { return handle(args); });
    executor_ = std::make_unique<Executor>([this](std::exception_ptr error) {
        try {
            std::rethrow_exception(error);
        } catch (const std::exception &e) {
            auto message = std::string(e.what());
            mainAsync([this, message] {
                operationError_ = message;
                status();
            });
        }
    });
    installHotkeys([this](uint64_t id, bool down) { hotkey(id, down); });
    setMenu(launchSettings, [this] { reload(); });
    updateDesktopNumber();
    if (!explicitConfig_ && !std::filesystem::exists(path_)) {
        try {
            PendingSave save(path_, defaultConfig());
            save.createIfMissing();
        } catch (const std::exception &e) {
            configError_ = e.what();
        }
    }
    switcher_ = std::make_unique<switcher::Service>(
        switcher::Host{[this] { return recording() || executor_->busy(); },
                       [this](const std::string &error) {
                           switcherError_ = error;
                           status();
                       },
                       [this] { landing_.reset(); }});
    reload();
    watcher_ = watchConfig(path_, [this] { reload(); });
    observeWorkspace(
        [this] {
            updateDesktopNumber();
            if (!switcher_->activating())
                switcher_->cancel();
            guardEmptyDesktop();
        },
        [this] {
            bindings_.resetPressed();
            recordingToken_.clear();
            reload();
            updateDesktopNumber();
        },
        [this] { updateDesktopNumber(); });
    // Only the resident asks for access, once at startup. CLI clients and the
    // separate settings helper must not request their own Accessibility grants.
    if (!trusted())
        requestAccessibility();
}
void Runtime::updateDesktopNumber() {
    try {
        setDesktopNumber(snapshot().current + 1);
    } catch (const std::exception &) {
        // A transient topology failure must not leave a misleading desktop number.
        setDesktopNumber(std::nullopt);
    }
}
void Runtime::status() {
    std::string text = configError_.empty() ? operationError_ : configError_;
    if (text.empty())
        text = switcherError_;
    if (text.empty() && !trusted())
        text = "Accessibility required; enable Spacewing.app in System Settings, then restart.";
    if (text.empty())
        text = "Ready";
    setStatus(text);
    if (text != "Ready")
        std::cerr << "spacewing: " << text << "\n";
}
void Runtime::configureSwitcher() {
    auto options = active_.switcher;
    options.enabled = options.enabled && trusted();
    try {
        switcher_->configure(options);
        switcherError_.clear();
    } catch (const std::exception &e) {
        // A switcher failure must leave desktop shortcuts available.
        switcherError_ = e.what();
    }
}
Reply Runtime::reload() {
    try {
        auto text = readFile(path_);
        auto config = parseConfig(text);
        // Read-only permission checks; the app does not repeatedly prompt or alter settings.
        bindings_.apply(trusted() ? config.shortcuts : Shortcuts{});
        source_ = std::move(text);
        active_ = config;
        loaded_ = true;
        configError_.clear();
        configureSwitcher();
        status();
        return {0, "Loaded " + path_ + "\n"};
    } catch (const std::exception &e) {
        configError_ = e.what();
        status();
        return failure(e);
    }
}
void Runtime::execute(Command command, std::function<void(Reply)> completion) {
    if (command.name == "left" || command.name == "right") {
        if (switcher_->activating()) {
            completion({75, "", "Wait for window activation to finish before switching desktops"});
            return;
        }
        switcher_->cancel();
        landing_.reset();
    }
    if ((command.name == "left" || command.name == "right") && recording()) {
        completion({75, "", "Finish recording the shortcut before switching desktops"});
        return;
    }
    try {
        executor_->enqueue([this, command, completion] {
            Outcome outcome;
            try {
                if (command.name == "list")
                    outcome.reply.output = controller_.list();
                else if (command.name == "doctor")
                    outcome.reply.output = diagnostics();
                else
                    outcome =
                        controller_.execute({command.action(), command.verify, command.dryRun});
            } catch (const std::exception &e) {
                outcome.reply = failure(e);
            }
            mainAsync([this, outcome = std::move(outcome), completion]() mutable {
                if (outcome.landed)
                    landing_ = outcome.landed;
                operationError_ = outcome.reply.error;
                status();
                updateDesktopNumber();
                completion(outcome.reply);
                if (outcome.landed)
                    guardEmptyDesktop();
            });
        });
    } catch (const std::exception &e) {
        completion(failure(e));
    }
}
bool Runtime::recording() {
    if (!recordingToken_.empty() && std::chrono::steady_clock::now() >= recordingDeadline_) {
        recordingToken_.clear();
        recordedChord_.clear();
    }
    return !recordingToken_.empty();
}
void Runtime::hotkey(uint64_t id, bool down) {
    if (!down) {
        bindings_.released(id);
        return;
    }
    auto action = bindings_.pressed(id);
    if (!action)
        return;
    if (recording()) {
        if (auto chord = bindings_.chord(id))
            recordedChord_ = chord->text();
        return;
    }
    Command command;
    command.name = direction(*action) < 0 ? "left" : "right";
    command.drag = carries(*action);
    execute(command, [](Reply) {});
}
void Runtime::guardEmptyDesktop() {
    if (!landing_ || switcher_->busy())
        return;
    const auto landed = *landing_;
    try {
        executor_->enqueue(
            [this, landed] {
                try {
                    if (snapshot(landed.display).id() != landed.id())
                        return;
                    auto populated = spaceHasWindows(landed.id());
                    if (populated && !*populated)
                        mainAsync([this, landed] {
                            if (landing_ && landing_->id() == landed.id() && !switcher_->busy())
                                activateEmptyDesktop(landed.display, landed.id());
                        });
                } catch (...) {
                }
            },
            true);
    } catch (...) {
    } // Never displace a queued user action for a speculative guard.
}
Reply Runtime::settingsRequest(const std::vector<std::string> &args) {
    if (args[0] == "__settings-read-v2" && args.size() == 1) {
        Reply reply;
        std::string disk;
        bool editable = false;
        try {
            disk = readFile(path_);
            parseConfig(disk);
            editable = true;
        } catch (const std::exception &e) {
            reply.error = e.what();
        }
        if (!trusted())
            reply.error += "\nEnable Accessibility for Spacewing.app, then restart it.";
        if (!switcherError_.empty())
            reply.error += "\nWindow switcher: " + switcherError_;
        reply.values = {path_, disk, editable ? "ready" : "invalid", reply.error};
        reply.error.clear(); // Displayable status, not a transport failure.
        return reply;
    }
    if (args[0] == "__settings-save-v2" && args.size() == 3) {
        if (readFile(path_) != args[1])
            throw Error("Config changed outside Settings. Reload before saving.", 78);
        const auto &text = args[2];
        auto config = parseConfig(text);
        PendingSave save(path_, text);
        bindings_.apply(trusted() ? config.shortcuts : Shortcuts{}, [&] { save.commit(args[1]); });
        active_ = config;
        source_ = text;
        loaded_ = true;
        configError_.clear();
        configureSwitcher();
        status();
        return {0, "Saved", "", {text, switcherError_}};
    }
    if (args[0] == "__record-start" && args.size() == 1) {
        if (executor_->busy() || switcher_->activating())
            throw Error("Wait for the current workspace action to finish, then record again.", 75);
        switcher_->cancel();
        recordingToken_ = std::to_string(++nextToken_);
        recordedChord_.clear();
        recordingDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        return {0, "", "", {recordingToken_}};
    }
    if ((args[0] == "__record-poll" || args[0] == "__record-end") && args.size() == 2) {
        if (!recording() || args[1] != recordingToken_)
            throw Error("Shortcut recording expired; click Record again", 75);
        if (args[0] == "__record-end") {
            recordingToken_.clear();
            recordedChord_.clear();
            return {};
        }
        recordingDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        Reply reply{0, "", "", {recordedChord_}};
        recordedChord_.clear();
        return reply;
    }
    throw Error("Invalid settings request", 64);
}
Reply Runtime::handle(const std::vector<std::string> &args) {
    if (!args.empty() && args[0].starts_with("__")) {
        Reply reply;
        mainSync([&] { reply = settingsRequest(args); });
        return reply;
    }
    auto command = parseArguments(args);
    if (command.explicitConfig && command.config != path_)
        throw Error("Running app uses " + path_ + "; restart it to select another config.", 78);
    if (command.name == "run")
        throw Error("Spacewing is already running with " + path_, 69);
    if (command.name == "left" || command.name == "right" || command.name == "list" ||
        command.name == "doctor") {
        auto promise = std::make_shared<std::promise<Reply>>();
        auto result = promise->get_future();
        mainSync([&] {
            execute(command, [promise](Reply reply) { promise->set_value(std::move(reply)); });
        });
        auto reply = result.get();
        if (command.name == "doctor") {
            mainSync([&] {
                reply.output = "Resident: running\nConfig: " + path_ + "\n" + reply.output;
                reply.output +=
                    "Window switcher: " +
                    std::string(switcher_->running()       ? "running"
                                : active_.switcher.enabled ? "unavailable"
                                                           : "disabled") +
                    " (" +
                    (active_.switcher.scope == switcher::Scope::AllDesktops ? "all-desktops"
                                                                            : "current-desktop") +
                    ")\n";
                if (!switcherError_.empty())
                    reply.output += "Switcher error: " + switcherError_ + "\n";
                if (!configError_.empty())
                    reply.output += "Config error: " + configError_ + "\n";
            });
        }
        return reply;
    }
    Reply reply;
    mainSync([&] {
        if (command.name == "reload")
            reply = reload();
        else if (command.name == "settings")
            launchSettings();
        else
            throw Error("Command is not available through the resident app", 64);
    });
    return reply;
}
} // namespace spacewing
