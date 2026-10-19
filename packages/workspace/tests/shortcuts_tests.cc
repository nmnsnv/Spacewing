#include "result.h"
#include "shortcuts.h"
#include <iostream>
#include <map>

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
void bindings() {
    uint64_t next = 0;
    std::map<uint64_t, Shortcut> registered;
    std::optional<Shortcut> conflict;
    BindingSet bindings(
        [&](Shortcut chord) {
            if (conflict && chord == *conflict)
                throw Error("conflict", 78);
            auto id = ++next;
            registered[id] = chord;
            return id;
        },
        [&](uint64_t id) { registered.erase(id); });
    Shortcuts config{parseShortcut("ctrl+shift+option+left"),
                     parseShortcut("ctrl+shift+option+right"), parseShortcut("ctrl+shift+cmd+left"),
                     parseShortcut("ctrl+shift+cmd+right")};
    bindings.apply(config);
    check(registered.size() == 4, "four bindings");
    check(bindings.pressed(1) == Action::Left, "initial press");
    check(!bindings.pressed(1), "repeat suppressed");
    bindings.apply(config);
    check(!bindings.pressed(1) && next == 4, "reload preserves pressed state");
    bindings.released(1);
    check(bindings.pressed(1) == Action::Left, "release rearms");
    bindings.resetPressed();
    auto swapped = config;
    std::swap(swapped[0], swapped[1]);
    bindings.apply(swapped);
    check(next == 4 && bindings.pressed(1) == Action::Right, "swap reuses registrations");
    bindings.released(1);
    auto replacement = config;
    replacement[0] = parseShortcut("ctrl+left");
    replacement[1] = parseShortcut("ctrl+right");
    conflict = replacement[1];
    const auto old = registered;
    rejects([&] { bindings.apply(replacement); }, 78, "registration rollback");
    check(registered == old && bindings.pressed(1) == Action::Right, "old bindings survive");
    bindings.released(1);
    conflict.reset();
    rejects([&] { bindings.apply(replacement, [] { throw Error("disk failure", 78); }); }, 78,
            "commit failure");
    check(registered == old, "rollback after disk failure");
    bindings.apply(Shortcuts{});
    check(registered.empty() && !bindings.pressed(1), "disable removes bindings");
}
} // namespace
int main() {
    try {
        bindings();
        std::cout << "Passed " << checks << " shortcut checks.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
