#include "controller.h"
#include "gesture.h"
#include <iostream>
#include <sstream>

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
std::vector<uint8_t> hex(const std::string &text) {
    std::istringstream stream(text);
    std::vector<uint8_t> bytes;
    std::string value;
    while (stream >> value)
        bytes.push_back(static_cast<uint8_t>(std::stoul(value, nullptr, 16)));
    return bytes;
}
void gestures() {
    auto began = dockEvent(1, 1, true, true);
    CGEventSetTimestamp(began.get(), 0x0102030405060708);
    check(gesturePayload(began.get()) == hex("08 07 06 05 04 03 02 01 00 00 00 00 00 00 00 00"
                                             " 00 00 00 00 00 00 00 00 01 00 00 00"
                                             " 28 00 00 00 17 00 00 00 00 00 00 01 00 00 00 00"
                                             " 99 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
                                             " 01 00 03 00 fa ff ff ff"),
          "27 began golden bytes");
    auto ended = dockEvent(4, 1, true, true);
    CGEventSetTimestamp(ended.get(), 0x0102030405060708);
    check(gesturePayload(ended.get()) == hex("08 07 06 05 04 03 02 01 00 00 00 00 00 00 00 00"
                                             " 00 00 00 00 00 00 00 00 02 00 00 00"
                                             " 28 00 00 00 17 00 00 00 00 00 00 04 00 00 00 00"
                                             " 99 19 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
                                             " 01 00 03 00 fa ff ff ff"
                                             " 1c 00 00 00 09 00 00 00 00 00 00 00 01 00 00 00"
                                             " 00 00 f1 d8 00 00 00 00 00 00 00 00"),
          "27 ended golden bytes");
    check(fixed1616(1e-12) == 1 && fixed1616(-1e-12) == -1, "subnormal sign");
    for (bool augmented : {false, true})
        for (bool natural : {false, true})
            for (int direction : {-1, 1}) {
                auto events = gestureEvents(direction, augmented, natural);
                check(events.size() == (augmented ? 6 : 3), "sequence size");
                std::array phases{1, 2, 4};
                for (size_t i = 0; i < 3; ++i) {
                    auto event = events[i * (augmented ? 2 : 1)].get();
                    check(CGEventGetIntegerValueField(event, static_cast<CGEventField>(55)) == 30,
                          "Dock type");
                    check(CGEventGetIntegerValueField(event, static_cast<CGEventField>(132)) ==
                              phases[i],
                          "phase order");
                    auto progress =
                        CGEventGetDoubleValueField(event, static_cast<CGEventField>(124));
                    check(progress * direction * (augmented && natural ? -1 : 1) > 0 &&
                              std::abs(progress) < .001,
                          "signed near-zero progress");
                    if (augmented)
                        check(CGEventGetIntegerValueField(events[i * 2 + 1].get(),
                                                          static_cast<CGEventField>(55)) == 29,
                              "companion type");
                }
                for (const auto &event : events)
                    check(CGEventGetIntegerValueField(event.get(), kCGEventSourceUserData) ==
                              eventTag,
                          "synthetic tag");
            }
    check(!needsAugmentation("26.5.2") && needsAugmentation("27.0") && needsAugmentation("28.1"),
          "OS paths without block");
    rejects([] { needsAugmentation("garbage"); }, 1, "invalid OS");
}
void focusFallback() {
    std::string display = "desktop-5-display";
    int windowReads = 0, cursorReads = 0;
    FocusQueries queries{[&]() -> std::optional<Window> {
                             ++windowReads;
                             throw FocusUnavailable("Cannot identify focused window");
                         },
                         [&] {
                             ++cursorReads;
                             return "pointer-display";
                         }};
    check(!resolveFocus(false, display, queries) && display == "desktop-5-display",
          "missing native window ID preserves navigation on the last focused display");
    check(windowReads == 1 && cursorReads == 0, "focus fallback adds no retry or pointer query");
    display.clear();
    check(!resolveFocus(false, display, queries) && display == "pointer-display",
          "first navigation with unsupported focus uses pointer display");
    check(cursorReads == 1, "pointer fallback resolves once");
    rejects([&] { resolveFocus(true, display, queries); }, 1,
            "carry must not fall back from an unidentified window");
    check(display == "pointer-display" && cursorReads == 1,
          "failed carry does not change display or query pointer");
    queries.window = []() -> std::optional<Window> {
        throw FocusUnavailable("Cannot query focused window", 75);
    };
    check(!resolveFocus(false, display, queries), "unresponsive AX app does not trap navigation");
    rejects([&] { resolveFocus(true, display, queries); }, 75, "carry preserves AX query failure");
    queries.window = [] { return std::optional<Window>{}; };
    check(!resolveFocus(false, display, queries) && display == "pointer-display",
          "empty desktop retains its display");
    check(!resolveFocus(true, display, queries),
          "empty desktop returns no carry target, preserving boundary no-ops");
    queries.window = [] {
        Window window;
        window.id = 123;
        window.display = "new-focused-display";
        return std::optional{window};
    };
    auto window = resolveFocus(false, display, queries);
    check(window && window->id == 123 && display == "new-focused-display",
          "usable focus supersedes the fallback display");
    window = resolveFocus(true, display, queries);
    check(window && window->id == 123, "carry keeps the exact focused window");
    for (int code : {77, 1, 75}) {
        queries.window = [code]() -> std::optional<Window> {
            throw Error("permission or native API failure", code);
        };
        rejects([&] { resolveFocus(false, display, queries); }, code,
                "navigation does not hide permission or unrelated platform failures");
    }
    check(display == "new-focused-display" && cursorReads == 1,
          "fatal failures leave display routing untouched");
}
void spacesAndMoves() {
    std::vector<Snapshot> displays{{"focused", {11, 12}, 0}, {"pointer", {21, 22, 23}, 1}};
    check(selectSnapshot(displays, "focused").id() == 11, "focused display");
    check(!displays[0].neighbor(-1) && displays[1].neighbor(1) == 23, "boundaries");
    check(selectSnapshot({{"Main", {31, 32}, 1}}, "physical-uuid").id() == 32, "shared Spaces");
    rejects([&] { selectSnapshot(displays, "disconnected"); }, 75, "missing display");
    rejects([&] { selectSnapshot({displays[0]}, "disconnected"); }, 75,
            "do not switch remaining display");
    MovePlan plan{100, 11, 12};
    validateMove(plan, {11}, true, true, false);
    for (auto membership : std::vector<std::vector<uint64_t>>{{11, 12}, {13}, {}})
        rejects([&] { validateMove(plan, membership, true, true, false); }, 1, "membership");
    rejects([&] { validateMove(plan, {11}, true, false, false); }, 1, "fullscreen destination");
    rejects([&] { validateMove(plan, {11}, true, true, true); }, 1, "fullscreen window");
    double time = 0;
    uint64_t current = 11;
    bool following = false;
    std::vector<std::string> log;
    MoveActions actions{[&] { log.push_back("move"); },
                        [&] { return std::vector<uint64_t>{time >= .02 ? 12u : 11u}; },
                        [&] { return current; },
                        [&] {
                            following = true;
                            log.push_back("follow");
                        },
                        [&] { log.push_back("focus"); },
                        [&] { return time; },
                        [&] {
                            time += .01;
                            if (following)
                                current = 12;
                        }};
    carryWindow(plan, actions);
    check(log == std::vector<std::string>{"move", "follow", "focus"}, "move, follow, focus order");
    log.clear();
    time = 0;
    actions.membership = [] { return std::vector<uint64_t>{12}; };
    actions.current = [] { return 12; };
    carryWindow(plan, actions);
    check(log == std::vector<std::string>{"move", "focus"},
          "automatic follow avoids double-switch");
    log.clear();
    time = 0;
    actions.membership = [] { return std::vector<uint64_t>{11}; };
    rejects([&] { carryWindow(plan, actions); }, 75, "move timeout");
    check(log == std::vector<std::string>{"move"}, "failed move never switches");
    log.clear();
    time = 0;
    actions.membership = [] { return std::vector<uint64_t>{12}; };
    actions.current = [] { return 99; };
    rejects([&] { carryWindow(plan, actions); }, 75, "intervening switch");
    check(log == std::vector<std::string>{"move"}, "interruption never follows or focuses");
    log.clear();
    time = 0;
    actions.current = [] { return 11; };
    rejects([&] { carryWindow(plan, actions); }, 75, "follow timeout");
    check(log == std::vector<std::string>{"move", "follow"}, "failed follow never focuses");
    log.clear();
    int reads = 0;
    actions.membership = [&] { return std::vector<uint64_t>{++reads == 1 ? 12u : 11u}; };
    actions.current = [] { return 12; };
    rejects([&] { carryWindow(plan, actions); }, 75, "membership revalidation");
    check(log == std::vector<std::string>{"move"}, "revalidation prevents focus");
    log.clear();
    actions.submit = [] { throw Error("submission failed"); };
    rejects([&] { carryWindow(plan, actions); }, 1, "submission failure");
    check(log.empty(), "failed submission no effects");
}
} // namespace
int main() {
    try {
        gestures();
        focusFallback();
        spacesAndMoves();
        std::cout << "Passed " << checks << " workspace checks; no windows moved.\n";
        std::cout << "Read-only move API probe: "
                  << (canMoveWindows() ? "available" : "unavailable") << "\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
