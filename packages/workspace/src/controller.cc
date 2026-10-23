#include "controller.h"
#include "gesture.h"
#include <chrono>
#include <thread>

namespace spacewing::workspace {
namespace {
double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
void waitObservation() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
} // namespace
std::optional<Window> resolveFocus(bool requireWindow, std::string &lastDisplay,
                                   const FocusQueries &queries) {
    std::optional<Window> window;
    try {
        window = queries.window();
    } catch (const FocusUnavailable &) {
        if (requireWindow)
            throw;
        // Some apps expose AX focus without a native window ID. The desktop
        // gesture needs only a display, so do not let such apps trap navigation.
    }
    if (window)
        lastDisplay = window->display;
    if (lastDisplay.empty())
        lastDisplay = queries.cursor();
    return window;
}
void validateMove(const MovePlan &plan, const std::vector<uint64_t> &membership, bool fullscreen) {
    if (!plan.window || !plan.source || !plan.target || plan.source == plan.target)
        throw Error("Invalid window move");
    if (fullscreen)
        throw Error("Moving fullscreen windows is not supported");
    if (membership != std::vector<uint64_t>{plan.source})
        throw Error("The focused window must belong only to the current desktop; all-desktop "
                    "windows cannot be carried");
}
void carryWindow(const MovePlan &plan, const MoveActions &actions) {
    actions.submit();
    const auto moveDeadline = actions.now() + 1;
    while (actions.membership() != std::vector<uint64_t>{plan.target}) {
        if (actions.now() >= moveDeadline)
            throw Error("Window did not reach the target desktop; no switch gesture posted", 75);
        actions.wait();
    }
    auto current = actions.current();
    if (current == plan.source)
        actions.follow();
    else if (current != plan.target)
        throw Error("Window moved, but another desktop switch intervened; no extra gesture posted",
                    75);
    const auto followDeadline = actions.now() + 1;
    while (actions.current() != plan.target) {
        if (actions.now() >= followDeadline)
            throw Error("Window moved, but the desktop did not follow", 75);
        actions.wait();
    }
    if (actions.membership() != std::vector<uint64_t>{plan.target})
        throw Error("Window membership changed before focus restoration", 75);
    actions.focus();
}
std::string Controller::list() {
    resolveFocus(false, lastDisplay_);
    return snapshot(lastDisplay_).summary() + "\n";
}
Outcome Controller::execute(const Request &request) {
    Outcome outcome;
    const auto start = now();
    auto window = resolveFocus(carries(request.action), lastDisplay_);
    auto before = snapshot(lastDisplay_);
    auto target = before.neighbor(direction(request.action));
    if (!target) {
        if (request.verify || request.dryRun)
            outcome.reply.output = "At boundary; " + before.summary() + "\n";
        return outcome;
    }
    bool fullscreenTarget = false;
    if (carries(request.action)) {
        if (!window)
            throw Error("No focused window to carry");
        validateMove({window->id, before.id(), *target}, windowSpaces(window->id),
                     window->fullscreen);
        fullscreenTarget = !isDesktop(*target);
        if (!canMoveWindows(fullscreenTarget))
            throw Error("Native window move is unavailable for this destination; ordinary "
                        "switching is still available");
    }
    auto events = gestureEvents(direction(request.action), needsAugmentation(osVersion()),
                                naturalScrolling());
    if (request.dryRun) {
        outcome.reply.output =
            "Would " +
            std::string(carries(request.action)
                            ? "carry window " + std::to_string(window->id) + " and switch"
                            : "switch") +
            " from Space " + std::to_string(before.id()) + " to " + std::to_string(*target) +
            " on " + before.display + "; " + std::to_string(events.size()) +
            " events prepared, nothing posted.\n";
        return outcome;
    }
    PointerRoute pointer(before.display);
    auto post = [&] {
        auto current = snapshot(before.display);
        if (current.id() != before.id() || current.neighbor(direction(request.action)) != target)
            throw Error("Desktop order changed before the switch; no gesture posted", 75);
        postGesture(events);
        outcome.postMilliseconds = (now() - start) * 1000;
    };
    if (carries(request.action)) {
        // A window can retain AX focus while being behind the fullscreen app.
        carryWindow({window->id, before.id(), *target},
                    {[&] { submitWindowMove(window->id, *target, fullscreenTarget); },
                     [&] { return windowSpaces(window->id); },
                     [&] { return snapshot(before.display).id(); }, post,
                     [&] { restoreFocus(*window, fullscreenTarget); }, now, waitObservation});
    } else {
        post();
        const auto deadline = now() + 1;
        while (snapshot(before.display).id() != *target) {
            if (now() >= deadline)
                throw Error("Target desktop was not observed within one second", 75);
            waitObservation();
        }
    }
    outcome.landed = snapshot(before.display);
    if (outcome.landed->id() != *target)
        throw Error("Another desktop switch intervened", 75);
    outcome.completeMilliseconds = (now() - start) * 1000;
    if (request.verify)
        outcome.reply.output =
            (carries(request.action) ? "Moved window " + std::to_string(window->id) + "; " : "") +
            outcome.landed->summary() + "\nDispatch: " + std::to_string(outcome.postMilliseconds) +
            " ms; observed completion: " + std::to_string(outcome.completeMilliseconds) +
            " ms (not visual latency)\n";
    return outcome;
}
} // namespace spacewing::workspace
