#include "session.h"
#include <algorithm>
#include <set>

namespace spacewing::switcher {
void Session::begin(std::vector<Candidate> windows, std::optional<WindowID> focused, bool reverse) {
    cancel();
    std::set<WindowID> seen;
    for (auto &window : windows)
        if (window.id.pid > 0 && window.id.number && seen.insert(window.id).second)
            windows_.push_back(std::move(window));
    if (!active())
        return;
    const auto current = std::find_if(windows_.begin(), windows_.end(),
                                      [&](const auto &window) { return window.id == focused; });
    if (current != windows_.end()) {
        selected_ = current - windows_.begin();
        step(reverse);
    } else {
        selected_ = reverse ? windows_.size() - 1 : 0;
    }
}
void Session::step(bool reverse) {
    if (active())
        selected_ = (selected_ + (reverse ? windows_.size() - 1 : 1)) % windows_.size();
}
void Session::cancel() {
    windows_.clear();
    selected_ = 0;
}
std::optional<WindowID> Session::commit() {
    std::optional<WindowID> result;
    if (active())
        result = windows_[selected_].id;
    cancel();
    return result;
}
Decision Keyboard::handle(Input event, bool blocked) {
    if (event.type == InputType::Up) {
        bool *held = event.key == Key::Tab      ? &tabDown_
                     : event.key == Key::Escape ? &escapeDown_
                                                : nullptr;
        if (held && *held) {
            *held = false;
            return {true, Intent::None};
        }
        return {};
    }
    const bool ownedRepeat =
        event.type == InputType::Down && event.repeat &&
        ((event.key == Key::Tab && tabDown_) || (event.key == Key::Escape && escapeDown_));
    if (active_ && blocked) {
        active_ = false;
        return {ownedRepeat, Intent::Cancel};
    }
    // A swallowed key-down owns its repeats even after modifier release/cancel.
    if (ownedRepeat)
        return {true, Intent::None};
    if (event.type == InputType::Modifiers) {
        if (active_ && event.otherModifier) {
            active_ = false;
            return {false, Intent::Cancel};
        }
        if (active_ && !event.option) {
            active_ = false;
            return {false, Intent::Commit};
        }
        return {};
    }
    if (event.key == Key::Tab && event.option && !event.otherModifier && !blocked) {
        if (event.repeat)
            return {tabDown_, Intent::None};
        tabDown_ = true;
        active_ = true;
        return {true, event.shift ? Intent::Backward : Intent::Forward};
    }
    if (event.key == Key::Escape && (active_ || escapeDown_)) {
        escapeDown_ = true;
        active_ = false;
        return {true, Intent::Cancel};
    }
    if (active_) {
        active_ = false;
        return {false, Intent::Cancel};
    }
    return {};
}
void Keyboard::reset() { active_ = tabDown_ = escapeDown_ = false; }
} // namespace spacewing::switcher
