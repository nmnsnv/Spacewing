#include "command.h"
#include "config/config.h"
#include "executor.h"
#include "ipc/socket.h"
#include <algorithm>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace spacewing;
using namespace spacewing::workspace;
namespace {
int checks = 0;
void check(bool condition, const std::string &message) {
    ++checks;
    if (!condition)
        throw std::runtime_error("FAIL: " + message);
}
template <typename F> void rejects(F function, int code, const std::string &message) {
    try {
        function();
    } catch (const Error &e) {
        check(e.code == code, message + " (exit status)");
        return;
    }
    throw std::runtime_error("FAIL: accepted " + message);
}
struct Temporary {
    std::string path;
    Temporary() {
        char pattern[] = "/private/tmp/spacewing-test-XXXXXX";
        char *result = mkdtemp(pattern);
        if (!result)
            throw std::runtime_error("mkdtemp failed");
        path = result;
    }
    ~Temporary() { std::filesystem::remove_all(path); }
};
void writeConfig(const std::string &path, const std::string &text) {
    PendingSave save(path, text);
    save.commit();
}
void arguments() {
    auto a = parseArguments({"--config", "/tmp/custom config", "left", "--drag-active"});
    auto b = parseArguments({"left", "--drag-active", "--config=/tmp/custom config"});
    check(a.config == b.config && a.action() == b.action() && a.drag,
          "both config forms and positions");
    check(parseArguments({}).name == "run", "default starts resident");
    check(parseArguments({"right", "--verify", "--dry-run"}).dryRun, "dry-run parses");
    for (auto args : std::vector<std::vector<std::string>>{{"--config"},
                                                           {"--config="},
                                                           {"--config", "--verify"},
                                                           {"left", "--smooth-drag"},
                                                           {"list", "--drag-active"},
                                                           {"left", "right"},
                                                           {"left", "--verify", "--verify"},
                                                           {"--config=a", "--config=b"}})
        rejects([&] { parseArguments(args); }, 64, "invalid command");
    rejects([] { resolvedPath(std::string("bad\0path", 8)); }, 64, "NUL path");
}
void configuration() {
    const auto defaults = parseConfig(defaultConfig());
    check(defaults.shortcuts[0].text() == "ctrl+shift+option+left", "switch default");
    check(defaults.shortcuts[3].text() == "ctrl+shift+cmd+right", "carry default");
    check(parseShortcut(" SHIFT + CONTROL + ALT + left ") == defaults.shortcuts[0],
          "case, aliases, order");
    check(parseShortcut("command+right").text() == "cmd+right", "command alias");
    check(!parseShortcut("  ").enabled(), "empty binding disabled");
    for (auto value : {"left", "ctrl", "ctrl+alt+option+left", "ctrl+left+right", "ctrl+tab",
                       "ctrl+left+", "ctrl++left"})
        rejects([&] { parseShortcut(value); }, 78, "invalid chord");
    for (auto text :
         {"[shortcut]\nleft='ctrl+left'", "[shortcuts]\nleft=1",
          "[shortcuts]\nleft='ctrl+left'\nright='control+left'", "[shortcuts]\nlef='ctrl+left'",
          "[shortcuts]\nleft='ctrl+left'\nleft='cmd+left'", "[shortcuts", "unknown=1\n[shortcuts]"})
        rejects([&] { parseConfig(text); }, 78, "invalid config");
    auto partial = parseConfig("[shortcuts]\nleft='ctrl+left'\n");
    check(partial.shortcuts[0].enabled() && !partial.shortcuts[1].enabled(),
          "omitted actions disabled");
    const std::string source = "# Personal shortcuts — שלום\n[shortcuts]\nleft='ctrl+left' # Keep "
                               "this note\nright='ctrl+right'\n";
    std::array<std::string, 4> values{"shift+alt+left", "", "ctrl+cmd+left", "ctrl+cmd+right"};
    auto edited = editedConfig(source, values);
    check(edited.find("Personal shortcuts") != std::string::npos &&
              edited.find("Keep this note") != std::string::npos,
          "comments preserved");
    check(parseConfig(edited).shortcuts[0].text() == "shift+option+left", "GUI normalization");
    check(!parseConfig(edited).shortcuts[1].enabled(), "GUI clear");
    check(defaults.switcher.enabled && defaults.switcher.scope == switcher::Scope::CurrentDesktop,
          "switcher defaults to current desktop");
    check(defaults.switcher.shortcut.text() == "option+tab", "switcher default shortcut");
    check(partial.switcher == switcher::Options{}, "legacy configs inherit switcher defaults");
    auto all = parseConfig("[shortcuts]\n[switcher]\nenabled=false\nscope='all-desktops'\n");
    check(!all.switcher.enabled && all.switcher.scope == switcher::Scope::AllDesktops,
          "switcher file settings");
    for (auto value :
         {"switcher=true\n[shortcuts]", "[shortcuts]\n[switcher]\nenabled='yes'",
          "[shortcuts]\n[switcher]\nscope='all'", "[shortcuts]\n[switcher]\nscope=1",
          "[shortcuts]\n[switcher]\nshortcut=1", "[shortcuts]\n[switcher]\nshortcut=''",
          "[shortcuts]\n[switcher]\nshortcut='tab'",
          "[shortcuts]\n[switcher]\nshortcut='option+up'",
          "[shortcuts]\n[switcher]\nshortcut='option+escape'",
          "[shortcuts]\n[switcher]\nunknown=true"})
        rejects([&] { parseConfig(value); }, 78, "invalid switcher configuration");
    const auto withSwitcher =
        source + "\n[switcher]\n# Keep switcher note\nenabled=false\nscope='all-desktops'\n";
    const auto shortcutEdit = editedConfig(withSwitcher, values);
    check(parseConfig(shortcutEdit).switcher == all.switcher,
          "shortcut-only save preserves switcher settings");
    const auto switcherEdit = editedConfig(withSwitcher, values, defaults.switcher);
    check(parseConfig(switcherEdit).switcher == defaults.switcher &&
              switcherEdit.find("Keep switcher note") != std::string::npos,
          "settings save preserves switcher comments");
    check(parseConfig(editedConfig(source, values, all.switcher)).switcher == all.switcher,
          "settings save adds switcher table to legacy config");
    const auto customSource = source + "\n[switcher]\nshortcut='CONTROL+Space' # Keep chord note\n";
    const auto custom = parseConfig(customSource);
    check(custom.switcher.shortcut.text() == "ctrl+space", "custom switcher shortcut parsed");
    check(parseConfig(editedConfig(customSource, values)).switcher == custom.switcher,
          "desktop-only saves preserve custom switcher shortcut");
    const auto changed = editedConfig(customSource, values, defaults.switcher);
    check(parseConfig(changed).switcher == defaults.switcher &&
              changed.find("Keep chord note") != std::string::npos,
          "switcher shortcut edits preserve comments");
    check(parseConfig(editedConfig(source, values, custom.switcher)).switcher == custom.switcher,
          "settings can add a custom switcher shortcut");
    for (const auto chord : {"ctrl+left", "ctrl+shift+left"})
        rejects(
            [&] {
                parseConfig(std::string("[shortcuts]\nleft='") + chord +
                            "'\n[switcher]\nshortcut='ctrl+left'\n");
            },
            78, "switcher conflicts with desktop shortcut or reverse chord");
    check(!parseConfig("[shortcuts]\nleft='ctrl+left'\n[switcher]\nenabled=false\n"
                       "shortcut='ctrl+left'\n")
               .switcher.enabled,
          "disabled switcher does not reserve its shortcut");
    Temporary temporary;
    const auto path = temporary.path + "/config";
    PendingSave first(path, source);
    first.createIfMissing();
    chmod(path.c_str(), 0640);
    const auto link = temporary.path + "/symlink";
    std::filesystem::create_symlink(path, link);
    {
        PendingSave save(link, edited);
        save.commit(source);
    }
    check(std::filesystem::is_symlink(link) && readFile(path) == edited, "saving follows symlink");
    struct stat metadata{};
    stat(path.c_str(), &metadata);
    check((metadata.st_mode & 0777) == 0640, "preserve mode");
    {
        PendingSave stale(path, source);
        writeConfig(path, defaultConfig());
        rejects([&] { stale.commit(edited); }, 78, "concurrent external edit");
        check(readFile(path) == defaultConfig(), "external edit survives");
    }
    {
        PendingSave create(path, source);
        rejects([&] { create.createIfMissing(); }, 78, "creation cannot overwrite");
    }
    for (auto &item : std::filesystem::directory_iterator(temporary.path))
        check(item.path().filename().string().find(".tmp.") == std::string::npos,
              "temporary files removed");
}
void ipc() {
    Temporary temporary;
    const auto path = temporary.path + "/control.sock";
    {
        Server server([](const auto &args) { return Reply{0, "ok\n", "", args}; }, path);
        auto reply =
            request({"newline\nvalue", "unicode שלום", std::string("NUL\0value", 9)}, path);
        check(reply.values.size() == 3 && reply.values[0] == "newline\nvalue" &&
                  reply.values[2].size() == 9,
              "IPC framing");
        rejects([&] { Server duplicate([](const auto &) { return Reply{}; }, path); }, 69,
                "singleton");
        struct stat metadata{};
        stat(path.c_str(), &metadata);
        check((metadata.st_mode & 0777) == 0600, "private socket mode");
    }
    rejects([&] { request({"left"}, path); }, 69, "resident required");
    {
        Server restarted(
            [](const auto &) {
                throw Error("test", 78);
                return Reply{};
            },
            path);
        check(request({}, path).code == 78, "server error reply");
    }
}
void executor() {
    std::promise<void> entered, release, completed;
    auto released = release.get_future().share();
    auto enteredFuture = entered.get_future();
    auto completedFuture = completed.get_future();
    std::vector<int> order;
    Executor executor;
    executor.enqueue([&] {
        entered.set_value();
        released.wait();
    });
    enteredFuture.get();
    bool wasBusy = executor.busy();
    bool limited = false;
    try {
        for (int i = 0; i < 32; ++i)
            executor.enqueue([&, i] { order.push_back(i); });
        try {
            executor.enqueue([] {});
        } catch (const Error &e) {
            limited = e.code == 75;
        }
        executor.enqueue([&] { order.push_back(-1); }, true);
        executor.enqueue(
            [&] {
                order.push_back(32);
                completed.set_value();
            },
            true);
    } catch (...) {
        release.set_value();
        throw;
    }
    release.set_value();
    check(completedFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
          "executor drains queue");
    check(wasBusy && limited, "executor bounds pending actions at 32");
    check(order.size() == 33, "maintenance jobs coalesce");
    for (int i = 0; i < 33; ++i)
        check(order[i] == i, "FIFO actions precede maintenance");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--ipc") {
            ipc();
            std::cout << "Passed " << checks << " IPC integration checks.\n";
            return 0;
        }
        arguments();
        configuration();
        executor();
        std::cout << "Passed " << checks << " checks; no events posted or windows moved.\n";
        return 0;
    } catch (const Error &e) {
        std::cerr << e.what() << '\n';
        return e.code;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
